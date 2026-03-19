#include <stdint.h>

#include "user/stdio.h"
#include "user/unistd.h"

int main(int argc, char **argv) {
    char tty[64];

    (void)argc;
    (void)argv;

    if (gettty(tty, sizeof(tty)) < 0) {
        putstr_fd(2, "tty: not attached\n");
        return 1;
    }

    putstr_fd(1, tty);
    putstr_fd(1, "\n");
    return 0;
}
