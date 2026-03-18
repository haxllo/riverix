#include <stdint.h>

#include "shared/syscall_abi.h"
#include "user/proc.h"
#include "user/stdio.h"

static const char *state_name(uint32_t state) {
    switch (state) {
    case SYS_TASK_RUNNABLE:
        return "RUNNABLE";
    case SYS_TASK_RUNNING:
        return "RUNNING";
    case SYS_TASK_BLOCKED:
        return "BLOCKED";
    case SYS_TASK_SLEEPING:
        return "SLEEPING";
    case SYS_TASK_ZOMBIE:
        return "ZOMBIE";
    default:
        return "UNKNOWN";
    }
}

static const char *kind_name(uint32_t kind) {
    return kind == SYS_TASK_KIND_USER ? "user" : "kern";
}

int main(int argc, char **argv) {
    uint32_t index = 0u;

    (void)argc;
    (void)argv;

    putstr_fd(1, "PID      PPID     STATE     KIND NAME\n");
    for (;;) {
        procinfo_t info;
        int32_t result = procinfo(index, &info);

        if (result == 1) {
            return 0;
        }

        if (result != 0) {
            putstr_fd(2, "ps: procinfo failed\n");
            return 1;
        }

        puthex32(info.pid);
        putstr_fd(1, " ");
        puthex32(info.parent_pid);
        putstr_fd(1, " ");
        putstr_fd(1, state_name(info.state));
        putstr_fd(1, " ");
        putstr_fd(1, kind_name(info.kind));
        putstr_fd(1, " ");
        putstr_fd(1, info.name);
        putstr_fd(1, "\n");
        index++;
    }
}
