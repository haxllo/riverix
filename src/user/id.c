#include <stdint.h>

#include "user/stdio.h"
#include "user/unistd.h"

int main(int argc, char **argv) {
    char tty[64];
    int32_t tty_result;

    (void)argc;
    (void)argv;

    putstr_fd(1, "uid=");
    puthex32(getuid());
    putstr_fd(1, " gid=");
    puthex32(getgid());
    tty_result = gettty(tty, sizeof(tty));
    if (tty_result >= 0) {
        putstr_fd(1, " tty=");
        putstr_fd(1, tty);
    }
    putstr_fd(1, "\n");
    return 0;
}
