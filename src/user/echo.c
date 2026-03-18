#include <stdint.h>

#include "user/stdio.h"

int main(int argc, char **argv) {
    int index;

    for (index = 1; index < argc; index++) {
        if (index > 1) {
            putstr_fd(1, " ");
        }

        putstr_fd(1, argv[index]);
    }

    putstr_fd(1, "\n");
    return 0;
}
