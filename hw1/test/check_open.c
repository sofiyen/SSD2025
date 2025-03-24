#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        return 1;
    }
    close(fd);
    return 0;
}