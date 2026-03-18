#include <stdint.h>

#include "user/boot.h"
#include "user/stdio.h"

int main(int argc, char **argv) {
    int32_t result;

    (void)argc;
    (void)argv;

    putstr_fd(1, "reinstall: begin\n");
    result = reinstall_rootfs();
    if (result != 0) {
        putstr_fd(2, "reinstall: failed\n");
        return 1;
    }

    putstr_fd(1, "reinstall: ok\n");
    return 0;
}
