#ifndef RIVERIX_SHARED_SYSCALL_ABI_H
#define RIVERIX_SHARED_SYSCALL_ABI_H

#include <stdint.h>

#define SYS_NAME_MAX 32u

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
    SYS_EXECV = 20u,
    SYS_READDIR = 21u,
    SYS_PROCINFO = 22u,
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

enum {
    SYS_TASK_UNUSED = 0u,
    SYS_TASK_RUNNABLE = 1u,
    SYS_TASK_RUNNING = 2u,
    SYS_TASK_BLOCKED = 3u,
    SYS_TASK_SLEEPING = 4u,
    SYS_TASK_ZOMBIE = 5u,
};

enum {
    SYS_TASK_KIND_KERNEL = 0u,
    SYS_TASK_KIND_USER = 1u,
};

typedef struct sys_stat {
    uint32_t kind;
    uint32_t size;
    uint32_t child_count;
} sys_stat_t;

typedef struct sys_dirent {
    uint32_t kind;
    uint32_t size;
    char name[SYS_NAME_MAX];
} sys_dirent_t;

typedef struct sys_procinfo {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t state;
    uint32_t kind;
    uint32_t run_ticks;
    char name[SYS_NAME_MAX];
} sys_procinfo_t;

#endif
