#include <stdint.h>

#include "user/stdio.h"
#include "user/unistd.h"

int main(int argc, char **argv) {
    char path[128];

    (void)argc;
    (void)argv;

    if (getcwd(path, sizeof(path)) < 0) {
        putstr_fd(2, "pwd: failed\n");
        return 1;
    }

    putstr_fd(1, path);
    putstr_fd(1, "\n");
    return 0;
}
