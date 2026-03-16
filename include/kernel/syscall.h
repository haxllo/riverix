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
};

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

#endif
