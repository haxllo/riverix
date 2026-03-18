#ifndef RIVERIX_SYSCALL_H
#define RIVERIX_SYSCALL_H

#include <stdint.h>

#include "kernel/idt.h"

enum {
    SYS_WRITE = 1u,
    SYS_GETPID = 2u,
    SYS_YIELD = 3u,
    SYS_EXIT = 4u,
    SYS_WAITPID = 5u,
    SYS_EXEC = 6u,
    SYS_FORK = 7u,
    SYS_OPEN = 8u,
    SYS_CLOSE = 9u,
    SYS_READ = 10u,
    SYS_LSEEK = 11u,
    SYS_MKDIR = 12u,
    SYS_UNLINK = 13u,
    SYS_STAT = 14u,
    SYS_CHDIR = 15u,
    SYS_DUP = 16u,
    SYS_DUP2 = 17u,
    SYS_SLEEP = 18u,
    SYS_TICKS = 19u,
};

enum {
    SYS_O_RDONLY = 1u << 0,
    SYS_O_WRONLY = 1u << 1,
    SYS_O_RDWR = SYS_O_RDONLY | SYS_O_WRONLY,
    SYS_O_CREATE = 1u << 2,
    SYS_O_TRUNC = 1u << 3,
};

enum {
    SYS_SEEK_SET = 0u,
    SYS_SEEK_CUR = 1u,
    SYS_SEEK_END = 2u,
};

typedef struct sys_stat {
    uint32_t kind;
    uint32_t size;
    uint32_t child_count;
} sys_stat_t;

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

static inline int32_t sys_exec(const char *path) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_EXEC), "b"(path) : "memory");
    return result;
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

#endif
