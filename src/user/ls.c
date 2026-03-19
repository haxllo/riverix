#include <stdint.h>

#include "shared/syscall_abi.h"
#include "user/dirent.h"
#include "user/stdio.h"
#include "user/string.h"

enum {
    LS_INODE_DIR = 3u,
};

static char mode_char(uint32_t kind) {
    return kind == LS_INODE_DIR ? 'd' : '-';
}

static void append_mode_bits(char *buffer, uint32_t mode) {
    buffer[1] = (mode & SYS_MODE_IRUSR) != 0u ? 'r' : '-';
    buffer[2] = (mode & SYS_MODE_IWUSR) != 0u ? 'w' : '-';
    buffer[3] = (mode & SYS_MODE_IXUSR) != 0u ? 'x' : '-';
    buffer[4] = (mode & SYS_MODE_IRGRP) != 0u ? 'r' : '-';
    buffer[5] = (mode & SYS_MODE_IWGRP) != 0u ? 'w' : '-';
    buffer[6] = (mode & SYS_MODE_IXGRP) != 0u ? 'x' : '-';
    buffer[7] = (mode & SYS_MODE_IROTH) != 0u ? 'r' : '-';
    buffer[8] = (mode & SYS_MODE_IWOTH) != 0u ? 'w' : '-';
    buffer[9] = (mode & SYS_MODE_IXOTH) != 0u ? 'x' : '-';
    buffer[10] = '\0';
}

static void print_long_entry(const dirent_t *entry) {
    char mode[11];

    mode[0] = mode_char(entry->kind);
    append_mode_bits(mode, entry->mode);
    putstr_fd(1, mode);
    putstr_fd(1, " ");
    puthex32(entry->uid);
    putstr_fd(1, " ");
    puthex32(entry->gid);
    putstr_fd(1, " ");
    puthex32(entry->size);
    putstr_fd(1, " ");
    putstr_fd(1, entry->name);
    putstr_fd(1, "\n");
}

static int list_path(const char *path, int long_mode) {
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

        if (long_mode) {
            print_long_entry(&entry);
        } else {
            putstr_fd(1, entry.name);
            putstr_fd(1, "\n");
        }
        index++;
    }
}

int main(int argc, char **argv) {
    int index = 1;
    int exit_code = 0;
    int long_mode = 0;

    if (argc > 1 && strcmp(argv[1], "-l") == 0) {
        long_mode = 1;
        index = 2;
    }

    if (index >= argc) {
        return list_path(".", long_mode);
    }

    for (; index < argc; index++) {
        if ((argc - index) > 1) {
            putstr_fd(1, argv[index]);
            putstr_fd(1, ":\n");
        }

        if (list_path(argv[index], long_mode) != 0) {
            exit_code = 1;
        }
    }

    return exit_code;
}
