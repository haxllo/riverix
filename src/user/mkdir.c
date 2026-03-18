#include "user/stdio.h"
#include "user/unistd.h"

int main(int argc, char **argv) {
    int index;
    int exit_code = 0;

    if (argc < 2) {
        putstr_fd(2, "mkdir: missing operand\n");
        return 1;
    }

    for (index = 1; index < argc; index++) {
        if (mkdir(argv[index]) != 0) {
            putstr_fd(2, "mkdir: failed ");
            putstr_fd(2, argv[index]);
            putstr_fd(2, "\n");
            exit_code = 1;
        }
    }

    return exit_code;
}
