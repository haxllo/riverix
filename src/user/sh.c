#include <stdint.h>

#include "user/fcntl.h"
#include "user/stdio.h"
#include "user/string.h"
#include "user/unistd.h"

#define SH_LINE_MAX 128u
#define SH_MAX_ARGS 8u
#define SH_MAX_STAGES 4u
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

static void trim_spaces(char **start) {
    char *cursor = *start;
    char *end;

    while (is_space(*cursor)) {
        cursor++;
    }

    *start = cursor;
    end = cursor + strlen(cursor);
    while (end > cursor && is_space(end[-1])) {
        end--;
    }
    *end = '\0';
}

static int split_pipeline(char *line, char **stages, uint32_t max_stages) {
    char *cursor = line;
    uint32_t stage_count = 0u;

    while (*cursor != '\0') {
        char *stage_start = cursor;

        while (*cursor != '\0' && *cursor != '|') {
            cursor++;
        }

        if (*cursor == '|') {
            *cursor++ = '\0';
        }

        trim_spaces(&stage_start);
        if (*stage_start == '\0' || *stage_start == '#') {
            if (*stage_start == '#') {
                break;
            }
            return -1;
        }

        if (stage_count >= max_stages) {
            return -1;
        }

        stages[stage_count++] = stage_start;
    }

    return (int)stage_count;
}

static int tokenize_stage(char *stage, char **argv, uint32_t max_args, char **redirect_path) {
    char *cursor = stage;
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

static int is_builtin_name(const char *name) {
    return strcmp(name, "cd") == 0 || strcmp(name, "exit") == 0;
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

static int wait_children(int32_t *pids, uint32_t count) {
    uint32_t index;
    int exit_code = 0;

    for (index = 0u; index < count; index++) {
        int32_t status = 0;

        if (waitpid(pids[index], &status) < 0) {
            putstr_fd(2, "sh: waitpid failed\n");
            exit_code = 1;
            continue;
        }

        if (status != 0) {
            putstr_fd(2, "sh: status 0x");
            puthex32_fd(2, (uint32_t)status);
            putstr_fd(2, "\n");
            exit_code = 1;
        }
    }

    return exit_code;
}

static int run_pipeline(uint32_t stage_count, char *argvs[SH_MAX_STAGES][SH_MAX_ARGS], const char *redirect_path) {
    int32_t pids[SH_MAX_STAGES];
    int32_t pipefds[SH_MAX_STAGES - 1u][2];
    uint32_t stage_index;
    uint32_t pipe_index;

    for (pipe_index = 0u; pipe_index + 1u < stage_count; pipe_index++) {
        if (pipe(pipefds[pipe_index]) != 0) {
            putstr_fd(2, "sh: pipe failed\n");
            return 1;
        }
    }

    for (stage_index = 0u; stage_index < stage_count; stage_index++) {
        char path[SH_PATH_MAX];
        int32_t pid;

        resolve_command_path(path, sizeof(path), argvs[stage_index][0]);
        pid = fork();
        if (pid < 0) {
            putstr_fd(2, "sh: fork failed\n");
            return 1;
        }

        if (pid == 0) {
            if (stage_index != 0u) {
                if (dup2(pipefds[stage_index - 1u][0], 0) < 0) {
                    putstr_fd(2, "sh: dup2 stdin failed\n");
                    exit(1);
                }
            }

            if ((stage_index + 1u) < stage_count) {
                if (dup2(pipefds[stage_index][1], 1) < 0) {
                    putstr_fd(2, "sh: dup2 stdout failed\n");
                    exit(1);
                }
            } else if (redirect_path != 0) {
                int32_t fd = open(redirect_path, O_WRONLY | O_CREATE | O_TRUNC);

                if (fd < 0 || dup2(fd, 1) < 0) {
                    putstr_fd(2, "sh: redirect failed\n");
                    exit(1);
                }

                (void)close(fd);
            }

            for (pipe_index = 0u; pipe_index + 1u < stage_count; pipe_index++) {
                (void)close(pipefds[pipe_index][0]);
                (void)close(pipefds[pipe_index][1]);
            }

            execv(path, (const char *const *)argvs[stage_index]);
            putstr_fd(2, "sh: exec failed ");
            putstr_fd(2, path);
            putstr_fd(2, "\n");
            exit(0x7Fu);
        }

        pids[stage_index] = pid;
    }

    for (pipe_index = 0u; pipe_index + 1u < stage_count; pipe_index++) {
        (void)close(pipefds[pipe_index][0]);
        (void)close(pipefds[pipe_index][1]);
    }

    return wait_children(pids, stage_count);
}

static void print_prompt(void) {
    char cwd[SH_PATH_MAX];
    uint32_t uid = getuid();

    if (getcwd(cwd, sizeof(cwd)) != 0) {
        strcpy(cwd, "?");
    }

    putstr_fd(1, "riverix:");
    putstr_fd(1, cwd);
    putstr_fd(1, uid == 0u ? "# " : "$ ");
}

static int run_shell(int fd, int interactive) {
    char line[SH_LINE_MAX];

    for (;;) {
        char *stages[SH_MAX_STAGES];
        char *argvs[SH_MAX_STAGES][SH_MAX_ARGS];
        int argcs[SH_MAX_STAGES];
        char *redirect_path = 0;
        uint32_t stage_index;
        int stage_count;
        int argc;
        int builtin_status;
        int32_t read_result;

        if (interactive) {
            print_prompt();
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
        stage_count = split_pipeline(line, stages, SH_MAX_STAGES);
        if (stage_count < 0) {
            putstr_fd(2, "sh: parse failed\n");
            continue;
        }

        if (stage_count == 0) {
            continue;
        }

        for (stage_index = 0u; stage_index < (uint32_t)stage_count; stage_index++) {
            char *stage_redirect = 0;

            argc = tokenize_stage(stages[stage_index], argvs[stage_index], SH_MAX_ARGS, &stage_redirect);
            if (argc <= 0) {
                putstr_fd(2, "sh: parse failed\n");
                break;
            }
            argcs[stage_index] = argc;

            if ((stage_index + 1u) < (uint32_t)stage_count && stage_redirect != 0) {
                putstr_fd(2, "sh: redirect only allowed on final stage\n");
                argc = -1;
                break;
            }

            if ((stage_index + 1u) == (uint32_t)stage_count) {
                redirect_path = stage_redirect;
            }
        }

        if (argc <= 0) {
            continue;
        }

        if (stage_count == 1) {
            builtin_status = run_builtin(argcs[0], argvs[0]);
            if (builtin_status >= 0) {
                if (redirect_path != 0) {
                    putstr_fd(2, "sh: builtin redirect unsupported\n");
                    continue;
                }

                continue;
            }
        } else {
            for (stage_index = 0u; stage_index < (uint32_t)stage_count; stage_index++) {
                if (is_builtin_name(argvs[stage_index][0])) {
                    putstr_fd(2, "sh: builtin pipelines unsupported\n");
                    argc = -1;
                    break;
                }
            }
            if (argc < 0) {
                continue;
            }
        }

        (void)run_pipeline((uint32_t)stage_count, argvs, redirect_path);
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
