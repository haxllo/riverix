#include <stdint.h>

#include "user/boot.h"
#include "user/stdio.h"
#include "user/unistd.h"

static void print_status(const char *prefix, int32_t status) {
    putstr_fd(1, prefix);
    puthex32((uint32_t)status);
    putstr_fd(1, "\n");
}

static int run_program(const char *path, const char *const *argv, int32_t *status) {
    int32_t pid = fork();

    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        execv(path, argv);
        putstr_fd(2, "init: exec failed ");
        putstr_fd(2, path);
        putstr_fd(2, "\n");
        exit(0x7Fu);
    }

    return waitpid(pid, status);
}

static int detect_writable_root(void) {
    if (mkdir("/var/phase5-probe") != 0) {
        return 0;
    }

    (void)unlink("/var/phase5-probe");
    return 1;
}

int main(int argc, char **argv) {
    bootinfo_t info;
    int have_bootinfo = 0;
    static const char *selftest_argv[] = { "selftest", 0 };
    static const char *ro_argv[] = { "sh", "/etc/rc-ro", 0 };
    static const char *disk_argv[] = { "sh", "/etc/rc-disk", 0 };
    static const char *recovery_argv[] = { "sh", "/etc/rc-recovery", 0 };
    static const char *reinstall_argv[] = { "sh", "/etc/rc-reinstall", 0 };
    static const char *soak_argv[] = { "sh", "/etc/rc-soak", 0 };
    static const char *shell_argv[] = { "sh", 0 };
    const char *const *script_argv;
    int32_t status = 0;

    (void)argc;
    (void)argv;

    putstr_fd(1, "init: online\n");
    have_bootinfo = getbootinfo(&info) == 0;

    if (run_program("/bin/selftest", selftest_argv, &status) < 0) {
        putstr_fd(2, "init: selftest launch failed\n");
        return 1;
    }

    print_status("init: selftest exit 0x", status);
    if (status != 0) {
        return status;
    }

    if (have_bootinfo && (info.flags & BOOT_FLAG_RECOVERY) != 0u) {
        putstr_fd(1, "init: recovery mode\n");
        if ((info.flags & BOOT_FLAG_REINSTALL) != 0u) {
            putstr_fd(1, "init: reinstall mode\n");
            script_argv = reinstall_argv;
        } else {
            script_argv = recovery_argv;
        }
    } else if (have_bootinfo && (info.flags & BOOT_FLAG_SOAK) != 0u) {
        putstr_fd(1, "init: soak mode\n");
        script_argv = soak_argv;
    } else {
        script_argv = detect_writable_root() ? disk_argv : ro_argv;
    }

    if (run_program("/bin/sh", script_argv, &status) < 0) {
        putstr_fd(2, "init: shell script launch failed\n");
        return 1;
    }

    print_status("init: rc exit 0x", status);
    putstr_fd(1, "init: shell handoff\n");

    execv("/bin/sh", shell_argv);
    putstr_fd(2, "init: interactive shell exec failed\n");
    return 1;
}
