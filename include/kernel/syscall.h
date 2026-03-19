#ifndef RIVERIX_SYSCALL_H
#define RIVERIX_SYSCALL_H

#include <stdint.h>

#include "kernel/idt.h"
#include "shared/syscall_abi.h"

uint32_t syscall_dispatch(interrupt_frame_t *frame);

static inline int32_t sys_write(uint32_t fd, const char *buffer, uint32_t length) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_WRITE), "b"(fd), "c"(buffer), "d"(length) : "memory");
    return result;
}

static inline uint32_t sys_getpid(void) {
    uint32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_GETPID) : "memory");
    return result;
}

static inline uint32_t sys_yield(void) {
    uint32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_YIELD) : "memory");
    return result;
}

static inline void sys_exit(int32_t status) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_EXIT), "b"(status) : "memory");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static inline int32_t sys_waitpid(int32_t pid, int32_t *status) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_WAITPID), "b"(pid), "c"(status) : "memory");
    return result;
}

static inline int32_t sys_execv(const char *path, const char *const *argv) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_EXECV), "b"(path), "c"(argv) : "memory");
    return result;
}

static inline int32_t sys_exec(const char *path) {
    return sys_execv(path, 0);
}

static inline int32_t sys_fork(void) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_FORK) : "memory");
    return result;
}

static inline int32_t sys_open(const char *path, uint32_t flags) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_OPEN), "b"(path), "c"(flags) : "memory");
    return result;
}

static inline int32_t sys_close(uint32_t fd) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_CLOSE), "b"(fd) : "memory");
    return result;
}

static inline int32_t sys_read(uint32_t fd, void *buffer, uint32_t length) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_READ), "b"(fd), "c"(buffer), "d"(length) : "memory");
    return result;
}

static inline uint32_t sys_getuid(void) {
    uint32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_GETUID) : "memory");
    return result;
}

static inline uint32_t sys_getgid(void) {
    uint32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_GETGID) : "memory");
    return result;
}

static inline int32_t sys_setuid(uint32_t uid) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_SETUID), "b"(uid) : "memory");
    return result;
}

static inline int32_t sys_setgid(uint32_t gid) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_SETGID), "b"(gid) : "memory");
    return result;
}

static inline int32_t sys_setsid(void) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_SETSID) : "memory");
    return result;
}

static inline int32_t sys_gettty(char *buffer, uint32_t length) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_GETTTY), "b"(buffer), "c"(length) : "memory");
    return result;
}

static inline int32_t sys_pipe(int32_t *fds) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_PIPE), "b"(fds) : "memory");
    return result;
}

static inline int32_t sys_lseek(uint32_t fd, int32_t offset, uint32_t whence) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_LSEEK), "b"(fd), "c"(offset), "d"(whence) : "memory");
    return result;
}

static inline int32_t sys_mkdir(const char *path) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_MKDIR), "b"(path) : "memory");
    return result;
}

static inline int32_t sys_unlink(const char *path) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_UNLINK), "b"(path) : "memory");
    return result;
}

static inline int32_t sys_stat(const char *path, sys_stat_t *stat) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_STAT), "b"(path), "c"(stat) : "memory");
    return result;
}

static inline int32_t sys_chdir(const char *path) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_CHDIR), "b"(path) : "memory");
    return result;
}

static inline int32_t sys_dup(uint32_t fd) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_DUP), "b"(fd) : "memory");
    return result;
}

static inline int32_t sys_dup2(uint32_t oldfd, uint32_t newfd) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_DUP2), "b"(oldfd), "c"(newfd) : "memory");
    return result;
}

static inline uint32_t sys_sleep(uint32_t ticks) {
    uint32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_SLEEP), "b"(ticks) : "memory");
    return result;
}

static inline uint32_t sys_ticks(void) {
    uint32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_TICKS) : "memory");
    return result;
}

static inline int32_t sys_readdir(const char *path, uint32_t index, sys_dirent_t *entry) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_READDIR), "b"(path), "c"(index), "d"(entry) : "memory");
    return result;
}

static inline int32_t sys_procinfo(uint32_t index, sys_procinfo_t *info) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_PROCINFO), "b"(index), "c"(info) : "memory");
    return result;
}

#endif
