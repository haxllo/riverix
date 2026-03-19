#include <stdint.h>

#include "user/boot.h"
#include "user/fcntl.h"
#include "user/stdio.h"
#include "user/string.h"
#include "user/unistd.h"

#define PHASE8_DROP_UID 0x000003E8u
#define PHASE8_DROP_GID 0x000003E8u

static int write_all(int32_t fd, const char *buffer, uint32_t length) {
    uint32_t written = 0u;

    while (written < length) {
        int32_t result = write(fd, buffer + written, length - written);

        if (result <= 0) {
            return -1;
        }

        written += (uint32_t)result;
    }

    return 0;
}

static int deny_protected_write(void) {
    int32_t fd = open("/etc/phase8-denied", O_WRONLY | O_CREATE | O_TRUNC);

    if (fd >= 0) {
        (void)close(fd);
        (void)unlink("/etc/phase8-denied");
        return -1;
    }

    putstr_fd(1, "phase8: denied ok\n");
    return 0;
}

static int exercise_tmp(void) {
    static const char payload[] = "phase8-tmp\n";
    char buffer[sizeof(payload)];
    int32_t fd;
    int32_t read_result;

    fd = open("/tmp/phase8-note", O_WRONLY | O_CREATE | O_TRUNC);
    if (fd < 0) {
        return -1;
    }

    if (write_all(fd, payload, (uint32_t)(sizeof(payload) - 1u)) != 0) {
        (void)close(fd);
        return -1;
    }

    (void)close(fd);

    fd = open("/tmp/phase8-note", O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    read_result = read(fd, buffer, (uint32_t)(sizeof(buffer) - 1u));
    (void)close(fd);
    if (read_result != (int32_t)(sizeof(payload) - 1u)) {
        return -1;
    }

    buffer[(uint32_t)read_result] = '\0';
    if (strcmp(buffer, payload) != 0) {
        return -1;
    }

    if (unlink("/tmp/phase8-note") != 0) {
        return -1;
    }

    putstr_fd(1, "phase8: tmp ok\n");
    return 0;
}

int main(int argc, char **argv) {
    bootinfo_t info;
    char tty[64];

    (void)argc;
    (void)argv;

    putstr_fd(1, "phase8: online\n");

    if (gettty(tty, sizeof(tty)) < 0) {
        putstr_fd(2, "phase8: tty missing\n");
        return 1;
    }

    putstr_fd(1, "phase8: tty ");
    putstr_fd(1, tty);
    putstr_fd(1, "\n");

    if (setsid() < 0 || gettty(tty, sizeof(tty)) >= 0) {
        putstr_fd(2, "phase8: setsid failed\n");
        return 1;
    }

    putstr_fd(1, "phase8: setsid ok\n");

    if (setgid(PHASE8_DROP_GID) != 0 || setuid(PHASE8_DROP_UID) != 0) {
        putstr_fd(2, "phase8: drop failed\n");
        return 1;
    }

    putstr_fd(1, "phase8: drop ok uid 0x");
    puthex32(getuid());
    putstr_fd(1, " gid 0x");
    puthex32(getgid());
    putstr_fd(1, "\n");

    if (getbootinfo(&info) != 0) {
        putstr_fd(2, "phase8: bootinfo failed\n");
        return 1;
    }

    if (info.root_policy == BOOT_ROOT_RAMDISK) {
        putstr_fd(1, "phase8: writable skipped\n");
        return 0;
    }

    if (deny_protected_write() != 0) {
        putstr_fd(2, "phase8: denied failed\n");
        return 1;
    }

    if (exercise_tmp() != 0) {
        putstr_fd(2, "phase8: tmp failed\n");
        return 1;
    }

    return 0;
}
