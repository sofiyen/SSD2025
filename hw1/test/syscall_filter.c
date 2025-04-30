/* This test program tests the syscall filter feature of the rootkit.
 * - Check if rootkit can filter read/write syscall
 * - Check if rootkit can filter other processes' open syscall
 * - Check if rootkit can filter many syscalls simultaneously
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/rootkit.h"

#define DEVICE_PATH "/dev/rootkit"

int test_child_open(int dev_null) {
    int pid = fork();

    if (pid > 0) {
        // parent
        int ret;
        waitpid(pid, &ret, 0);
        if (ret < 0) {
            perror("waitpid: Failed to wait for child\n");
            return EXIT_FAILURE;
        }
    } else if (pid == 0) {
        // child
        dup2(dev_null, STDERR_FILENO);
        // exec check_open
        char *args[] = {"./check_open", NULL};
        execv(args[0], args);
        perror("execvp: Failed to execute check_open\n");
        exit(EXIT_FAILURE);
    } else {
        perror("fork: Failed to fork\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open " DEVICE_PATH ", is rootkit loaded?\n");
        return EXIT_FAILURE;
    }
    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null < 0) {
        perror("Failed to open /dev/null\n");
        close(fd);
        return EXIT_FAILURE;
    }

    fcntl(fd, F_SETFD, FD_CLOEXEC);

    // Test syscall, should work now
    char buf[16];
    int ret;

    ret = read(dev_null, buf, sizeof(buf));
    if (ret < 0) {
        perror("read: Failed to read from /dev/null\n");
        return EXIT_FAILURE;
    }

    ret = test_child_open(dev_null);
    if (ret != EXIT_SUCCESS) {
        perror("test_child_open: child failed to call open\n");
        return EXIT_FAILURE;
    }

    // filter syscall
    struct filter_info finfo_read = {
        .syscall_nr = __NR_read,
        .comm = "syscall_filter"
    };

    struct filter_info finfo_open = {
        .syscall_nr = __NR_openat,
        .comm = "check_open"
    };

    ret = ioctl(fd, IOCTL_ADD_FILTER, &finfo_read);
    if (ret < 0) {
        perror("ioctl: Failed to add read filter\n");
        return EXIT_FAILURE;
    }
    ret = ioctl(fd, IOCTL_ADD_FILTER, &finfo_open);
    if (ret < 0) {
        perror("ioctl: Failed to add open filter\n");
        return EXIT_FAILURE;
    }

    // test syscall, should be blocked
    ret = read(dev_null, buf, sizeof(buf));
    if (ret != -1) {
        perror("read: read is not blocked\n");
        return EXIT_FAILURE;
    }

    ret = test_child_open(dev_null);
    if (ret != EXIT_SUCCESS) {
        perror("test_child_open: child's open is not blocked\n");
        return EXIT_FAILURE;
    }

    // try unfilted syscall, should work
    ret = write(dev_null, buf, sizeof(buf));
    if (ret < 0) {
        perror("write: Failed to write to /dev/null\n");
        return EXIT_FAILURE;
    }

    // unfilter syscall
    ret = ioctl(fd, IOCTL_REMOVE_FILTER, &finfo_read);
    if (ret < 0) {
        perror("ioctl: Failed to remove read filter\n");
        return EXIT_FAILURE;
    }
    ret = ioctl(fd, IOCTL_REMOVE_FILTER, &finfo_open);
    if (ret < 0) {
        perror("ioctl: Failed to remove open filter\n");
        return EXIT_FAILURE;
    }

    // test syscall, should work now
    ret = read(dev_null, buf, sizeof(buf));
    if (ret < 0) {
        perror("read: Failed to read from /dev/null\n");
        return EXIT_FAILURE;
    }

    ret = test_child_open(dev_null);
    if (ret != EXIT_SUCCESS) {
        perror("test_child_open: child failed to call open\n");
        return EXIT_FAILURE;
    }

    printf("syscall filter test passed.\n");

    return EXIT_SUCCESS;
}