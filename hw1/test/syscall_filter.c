#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../src/rootkit.h"

#define DEVICE_PATH "/dev/rootkit"

int main(int argc, char *argv[]) {
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open " DEVICE_PATH ", is rootkit loaded?\n");
        return EXIT_FAILURE;
    }
    int test_fd = open("/dev/null", O_RDWR);
    if (test_fd < 0) {
        perror("Failed to open /dev/null\n");
        close(fd);
        return EXIT_FAILURE;
    }

    // Test read/write syscall, should work now
    char buf[16];
    int ret;

    ret = read(test_fd, buf, sizeof(buf));
    if (ret < 0) {
        perror("read: Failed to read from /dev/null\n");
        close(fd);
        close(test_fd);
        return EXIT_FAILURE;
    }
    ret = write(test_fd, buf, sizeof(buf));
    if (ret < 0) {
        perror("write: Failed to write to /dev/null\n");
        close(fd);
        close(test_fd);
        return EXIT_FAILURE;
    }

    // filter read syscall
    struct filter_info finfo_read = {
        .syscall_nr = __NR_read,
        .comm = "syscall_filter"
    };

    ret = ioctl(fd, IOCTL_ADD_FILTER, &finfo_read);
    if (ret < 0) {
        perror("ioctl: Failed to add read filter\n");
        close(fd);
        close(test_fd);
        return EXIT_FAILURE;
    }

    // test read syscall, should be blocked
    ret = read(test_fd, buf, sizeof(buf));
    if (ret != -1) {
        perror("read: read is not blocked\n");
        close(fd);
        close(test_fd);
        return EXIT_FAILURE;
    }

    // unfilter read syscall
    ret = ioctl(fd, IOCTL_REMOVE_FILTER, &finfo_read);
    if (ret < 0) {
        perror("ioctl: Failed to remove read filter\n");
        close(fd);
        close(test_fd);
        return EXIT_FAILURE;
    }

    // test read syscall, should work now
    ret = read(test_fd, buf, sizeof(buf));
    if (ret < 0) {
        perror("read: Failed to read from /dev/null\n");
        close(fd);
        close(test_fd);
        return EXIT_FAILURE;
    }

    printf("syscall filter test passed.\n");

    close(fd);
    close(test_fd);

    return EXIT_SUCCESS;
}