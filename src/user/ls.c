#include <stdint.h>

#include "user/dirent.h"
#include "user/stdio.h"

static int list_path(const char *path) {
    uint32_t index = 0u;

    for (;;) {
        dirent_t entry;
        int32_t result = readdir(path, index, &entry);

        if (result == 1) {
            return 0;
        }

        if (result != 0) {
            putstr_fd(2, "ls: read failed ");
            putstr_fd(2, path);
            putstr_fd(2, "\n");
            return 1;
        }

        putstr_fd(1, entry.name);
        putstr_fd(1, "\n");
        index++;
    }
}

int main(int argc, char **argv) {
    int index;
    int exit_code = 0;

    if (argc < 2) {
        return list_path(".");
    }

    for (index = 1; index < argc; index++) {
        if (argc > 2) {
            putstr_fd(1, argv[index]);
            putstr_fd(1, ":\n");
        }

        if (list_path(argv[index]) != 0) {
            exit_code = 1;
        }
    }

    return exit_code;
}
