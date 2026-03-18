#include <stdint.h>

#include "user/fcntl.h"
#include "user/stdio.h"
#include "user/string.h"
#include "user/unistd.h"

#define SH_LINE_MAX 128u
#define SH_MAX_ARGS 8u
#define SH_PATH_MAX 128u

static int is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int contains_slash(const char *text) {
    uint32_t index = 0u;

    while (text[index] != '\0') {
        if (text[index] == '/') {
            return 1;
        }

        index++;
    }

    return 0;
}

static void strip_line_end(char *line) {
    uint32_t index = 0u;

    while (line[index] != '\0') {
        if (line[index] == '\r' || line[index] == '\n') {
            line[index] = '\0';
            return;
        }

        index++;
    }
}

static int tokenize(char *line, char **argv, uint32_t max_args, char **redirect_path) {
    char *cursor = line;
    uint32_t argc = 0u;

    *redirect_path = 0;

    while (*cursor != '\0') {
        while (is_space(*cursor)) {
            cursor++;
        }

        if (*cursor == '\0' || *cursor == '#') {
            break;
        }

        if (*cursor == '>') {
            cursor++;
            while (is_space(*cursor)) {
                cursor++;
            }

            if (*cursor == '\0') {
                return -1;
            }

            *redirect_path = cursor;
            while (*cursor != '\0' && !is_space(*cursor)) {
                cursor++;
            }

            if (*cursor != '\0') {
                *cursor++ = '\0';
            }

            continue;
        }

        if (argc + 1u >= max_args) {
            return -1;
        }

        argv[argc++] = cursor;
        while (*cursor != '\0' && !is_space(*cursor) && *cursor != '>') {
            cursor++;
        }

        if (*cursor == '>') {
            *cursor = '\0';
            continue;
        }

        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
    }

    argv[argc] = 0;
    return (int)argc;
}

static int run_builtin(int argc, char **argv) {
    if (strcmp(argv[0], "cd") == 0) {
        if (argc < 2) {
            putstr_fd(2, "sh: cd needs a path\n");
            return 1;
        }

        if (chdir(argv[1]) != 0) {
            putstr_fd(2, "sh: cd failed\n");
            return 1;
        }

        return 0;
    }

    if (strcmp(argv[0], "exit") == 0) {
        exit(0);
    }

    return -1;
}

static void resolve_command_path(char *buffer, uint32_t capacity, const char *command) {
    if (contains_slash(command)) {
        strcpy(buffer, command);
        return;
    }

    strcpy(buffer, "/bin/");
    strcpy(buffer + 5, command);
    (void)capacity;
}

static int run_external(int argc, char **argv, const char *redirect_path) {
    char path[SH_PATH_MAX];
    int32_t pid;
    int32_t status = 0;

    resolve_command_path(path, sizeof(path), argv[0]);
    pid = fork();
    if (pid < 0) {
        putstr_fd(2, "sh: fork failed\n");
        return 1;
    }

    if (pid == 0) {
        if (redirect_path != 0) {
            int32_t fd = open(redirect_path, O_WRONLY | O_CREATE | O_TRUNC);

            if (fd < 0 || dup2(fd, 1) < 0) {
                putstr_fd(2, "sh: redirect failed\n");
                exit(1);
            }

            (void)close(fd);
        }

        execv(path, (const char *const *)argv);
        putstr_fd(2, "sh: exec failed ");
        putstr_fd(2, path);
        putstr_fd(2, "\n");
        exit(0x7Fu);
    }

    if (waitpid(pid, &status) < 0) {
        putstr_fd(2, "sh: waitpid failed\n");
        return 1;
    }

    if (status != 0) {
        putstr_fd(2, "sh: status 0x");
        puthex32_fd(2, (uint32_t)status);
        putstr_fd(2, "\n");
    }

    (void)argc;
    return status == 0 ? 0 : 1;
}

static int run_shell(int fd, int interactive) {
    char line[SH_LINE_MAX];

    for (;;) {
        int argc;
        char *argv[SH_MAX_ARGS];
        char *redirect_path;
        int builtin_status;
        int32_t read_result;

        if (interactive) {
            putstr_fd(1, "$ ");
        }

        read_result = readline_fd(fd, line, sizeof(line));
        if (read_result < 0) {
            putstr_fd(2, "sh: read failed\n");
            return 1;
        }

        if (read_result == 0) {
            return 0;
        }

        strip_line_end(line);
        argc = tokenize(line, argv, SH_MAX_ARGS, &redirect_path);
        if (argc < 0) {
            putstr_fd(2, "sh: parse failed\n");
            continue;
        }

        if (argc == 0) {
            continue;
        }

        builtin_status = run_builtin(argc, argv);
        if (builtin_status >= 0) {
            continue;
        }

        (void)run_external(argc, argv, redirect_path);
    }
}

int main(int argc, char **argv) {
    if (argc > 2) {
        putstr_fd(2, "sh: usage: sh [script]\n");
        return 1;
    }

    if (argc == 2) {
        int32_t fd = open(argv[1], O_RDONLY);
        int result;

        if (fd < 0) {
            putstr_fd(2, "sh: open failed\n");
            return 1;
        }

        result = run_shell(fd, 0);
        (void)close(fd);
        return result;
    }

    return run_shell(0, 1);
}
