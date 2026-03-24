#ifndef RIVERIX_SHARED_SYSCALL_ABI_H
#define RIVERIX_SHARED_SYSCALL_ABI_H

#include <stdint.h>

#define SYS_NAME_MAX 32u
#define SYS_ERR_WOULD_BLOCK (-2)
#define SYS_UID_ROOT 0u
#define SYS_GID_ROOT 0u

#define SYS_MODE_IXOTH 0x001u
#define SYS_MODE_IWOTH 0x002u
#define SYS_MODE_IROTH 0x004u
#define SYS_MODE_IXGRP 0x008u
#define SYS_MODE_IWGRP 0x010u
#define SYS_MODE_IRGRP 0x020u
#define SYS_MODE_IXUSR 0x040u
#define SYS_MODE_IWUSR 0x080u
#define SYS_MODE_IRUSR 0x100u
#define SYS_MODE_ISVTX 0x200u
#define SYS_MODE_FILE_DEFAULT 0644u
#define SYS_MODE_EXEC_DEFAULT 0755u
#define SYS_MODE_DIR_DEFAULT 0755u
#define SYS_MODE_TMP_DEFAULT 01777u

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
    SYS_BOOTINFO = 23u,
    SYS_GETCWD = 24u,
    SYS_REINSTALL_ROOTFS = 25u,
    SYS_GETUID = 26u,
    SYS_GETGID = 27u,
    SYS_SETUID = 28u,
    SYS_SETGID = 29u,
    SYS_SETSID = 30u,
    SYS_GETTTY = 31u,
    SYS_PIPE = 32u,
    SYS_NETINFO = 33u,
    SYS_PING4 = 34u,
    SYS_TRACEINFO = 35u,
    SYS_TRACEREAD = 36u,
};

enum {
    SYS_BOOT_ROOT_AUTO = 0u,
    SYS_BOOT_ROOT_DISK = 1u,
    SYS_BOOT_ROOT_RAMDISK = 2u,
};

enum {
    SYS_BOOT_FLAG_RECOVERY = 1u << 0,
    SYS_BOOT_FLAG_REINSTALL = 1u << 1,
    SYS_BOOT_FLAG_SOAK = 1u << 2,
};

enum {
    SYS_TRACE_CATEGORY_BOOT = 1u,
    SYS_TRACE_CATEGORY_MEMORY = 2u,
    SYS_TRACE_CATEGORY_PROC = 3u,
    SYS_TRACE_CATEGORY_BLOCK = 4u,
    SYS_TRACE_CATEGORY_NET = 5u,
    SYS_TRACE_CATEGORY_TRAP = 6u,
    SYS_TRACE_CATEGORY_PANIC = 7u,
};

enum {
    SYS_TRACE_EVENT_BOOT_ROOT = 1u,
    SYS_TRACE_EVENT_MEMORY_STATE = 2u,
    SYS_TRACE_EVENT_PROC_FORK = 3u,
    SYS_TRACE_EVENT_PROC_EXEC = 4u,
    SYS_TRACE_EVENT_PROC_EXIT = 5u,
    SYS_TRACE_EVENT_PROC_PING_BLOCK = 6u,
    SYS_TRACE_EVENT_PROC_PING_WAKE = 7u,
    SYS_TRACE_EVENT_BLOCK_STORAGE = 8u,
    SYS_TRACE_EVENT_BLOCK_ROOTFS = 9u,
    SYS_TRACE_EVENT_NET_READY = 10u,
    SYS_TRACE_EVENT_NET_ARP = 11u,
    SYS_TRACE_EVENT_NET_PING_START = 12u,
    SYS_TRACE_EVENT_NET_PING_REPLY = 13u,
    SYS_TRACE_EVENT_NET_PING_TIMEOUT = 14u,
    SYS_TRACE_EVENT_TRAP_USER = 15u,
    SYS_TRACE_EVENT_TRAP_KERNEL = 16u,
    SYS_TRACE_EVENT_PANIC_FATAL = 17u,
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
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t links;
} sys_stat_t;

typedef struct sys_dirent {
    uint32_t kind;
    uint32_t size;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t links;
    char name[SYS_NAME_MAX];
} sys_dirent_t;

typedef struct sys_procinfo {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t sid;
    uint32_t pgid;
    uint32_t state;
    uint32_t kind;
    uint32_t run_ticks;
    uint32_t uid;
    uint32_t gid;
    char name[SYS_NAME_MAX];
    char tty[SYS_NAME_MAX];
} sys_procinfo_t;

typedef struct sys_bootinfo {
    uint32_t root_policy;
    uint32_t flags;
} sys_bootinfo_t;

#define SYS_PING_OK 0
#define SYS_PING_ERR_TIMEOUT (-1)
#define SYS_PING_ERR_UNREACHABLE (-2)
#define SYS_PING_ERR_NOT_READY (-3)
#define SYS_PING_ERR_BUSY (-4)
#define SYS_PING_ERR_INVALID (-5)

typedef struct sys_netinfo {
    uint32_t ready;
    uint8_t mac[6];
    uint8_t reserved0[2];
    uint32_t ipv4_addr;
    uint32_t ipv4_netmask;
    uint32_t ipv4_gateway;
    uint32_t arp_valid;
    uint8_t arp_mac[6];
    uint8_t reserved1[2];
    uint32_t arp_ipv4;
    uint32_t rx_packets;
    uint32_t tx_packets;
} sys_netinfo_t;

typedef struct sys_trace_info {
    uint32_t capacity;
    uint32_t count;
    uint32_t next_sequence;
} sys_trace_info_t;

typedef struct sys_trace_record {
    uint32_t sequence;
    uint32_t ticks;
    uint32_t category;
    uint32_t event;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
} sys_trace_record_t;

#endif
