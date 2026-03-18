#include <stdint.h>

#include "shared/syscall_abi.h"
#include "user/boot.h"
#include "user/dirent.h"
#include "user/proc.h"
#include "user/sys/stat.h"
#include "user/unistd.h"

int32_t write(int32_t fd, const void *buffer, uint32_t length) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_WRITE), "b"(fd), "c"(buffer), "d"(length) : "memory");
    return result;
}

int32_t read(int32_t fd, void *buffer, uint32_t length) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_READ), "b"(fd), "c"(buffer), "d"(length) : "memory");
    return result;
}

uint32_t getpid(void) {
    uint32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_GETPID) : "memory");
    return result;
}

int32_t fork(void) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_FORK) : "memory");
    return result;
}

int32_t waitpid(int32_t pid, int32_t *status) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_WAITPID), "b"(pid), "c"(status) : "memory");
    return result;
}

int32_t exec(const char *path) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_EXEC), "b"(path) : "memory");
    return result;
}

int32_t execv(const char *path, const char *const *argv) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_EXECV), "b"(path), "c"(argv) : "memory");
    return result;
}

void exit(int32_t status) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_EXIT), "b"(status) : "memory");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

int32_t open(const char *path, uint32_t flags) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_OPEN), "b"(path), "c"(flags) : "memory");
    return result;
}

int32_t close(int32_t fd) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_CLOSE), "b"(fd) : "memory");
    return result;
}

int32_t lseek(int32_t fd, int32_t offset, uint32_t whence) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_LSEEK), "b"(fd), "c"(offset), "d"(whence) : "memory");
    return result;
}

int32_t getcwd(char *buffer, uint32_t length) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_GETCWD), "b"(buffer), "c"(length) : "memory");
    return result;
}

int32_t mkdir(const char *path) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_MKDIR), "b"(path) : "memory");
    return result;
}

int32_t unlink(const char *path) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_UNLINK), "b"(path) : "memory");
    return result;
}

int32_t stat(const char *path, stat_t *stat_buffer) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_STAT), "b"(path), "c"(stat_buffer) : "memory");
    return result;
}

int32_t chdir(const char *path) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_CHDIR), "b"(path) : "memory");
    return result;
}

int32_t dup(int32_t fd) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_DUP), "b"(fd) : "memory");
    return result;
}

int32_t dup2(int32_t oldfd, int32_t newfd) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_DUP2), "b"(oldfd), "c"(newfd) : "memory");
    return result;
}

int32_t sleep(uint32_t tick_count) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_SLEEP), "b"(tick_count) : "memory");
    return result;
}

uint32_t ticks(void) {
    uint32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_TICKS) : "memory");
    return result;
}

int32_t readdir(const char *path, uint32_t index, dirent_t *entry) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_READDIR), "b"(path), "c"(index), "d"(entry) : "memory");
    return result;
}

int32_t procinfo(uint32_t index, procinfo_t *info) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_PROCINFO), "b"(index), "c"(info) : "memory");
    return result;
}

int32_t getbootinfo(bootinfo_t *info) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_BOOTINFO), "b"(info) : "memory");
    return result;
}

int32_t reinstall_rootfs(void) {
    int32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result) : "a"(SYS_REINSTALL_ROOTFS) : "memory");
    return result;
}
