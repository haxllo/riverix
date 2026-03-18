#include "user/stdio.h"
#include "user/unistd.h"

int main(int argc, char **argv) {
    int index;
    int exit_code = 0;

    if (argc < 2) {
        putstr_fd(2, "rm: missing operand\n");
        return 1;
    }

    for (index = 1; index < argc; index++) {
        if (unlink(argv[index]) != 0) {
            putstr_fd(2, "rm: failed ");
            putstr_fd(2, argv[index]);
            putstr_fd(2, "\n");
            exit_code = 1;
        }
    }

    return exit_code;
}
