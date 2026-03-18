#include <stdint.h>

#include "user/fcntl.h"
#include "user/stdio.h"
#include "user/unistd.h"

static int cat_fd(int32_t fd) {
    char buffer[64];

    for (;;) {
        int32_t result = read(fd, buffer, sizeof(buffer));

        if (result < 0) {
            return 1;
        }

        if (result == 0) {
            return 0;
        }

        if (write(1, buffer, (uint32_t)result) != result) {
            return 1;
        }
    }
}

int main(int argc, char **argv) {
    int index;
    int exit_code = 0;

    if (argc < 2) {
        return cat_fd(0);
    }

    for (index = 1; index < argc; index++) {
        int32_t fd = open(argv[index], O_RDONLY);

        if (fd < 0) {
            putstr_fd(2, "cat: open failed ");
            putstr_fd(2, argv[index]);
            putstr_fd(2, "\n");
            exit_code = 1;
            continue;
        }

        if (cat_fd(fd) != 0) {
            putstr_fd(2, "cat: read failed ");
            putstr_fd(2, argv[index]);
            putstr_fd(2, "\n");
            exit_code = 1;
        }

        (void)close(fd);
    }

    return exit_code;
}
