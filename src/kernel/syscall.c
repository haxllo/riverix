#include "kernel/syscall.h"

#include <stdint.h>

#include "kernel/idt.h"
#include "kernel/proc.h"
#include "kernel/usercopy.h"
#include "kernel/vfs.h"

#define SYSCALL_IO_CHUNK 256u

_Static_assert((int)SYS_O_RDONLY == (int)VFS_O_RDONLY, "syscall and VFS read flag mismatch");
_Static_assert((int)SYS_O_WRONLY == (int)VFS_O_WRONLY, "syscall and VFS write flag mismatch");
_Static_assert((int)SYS_O_CREATE == (int)VFS_O_CREATE, "syscall and VFS create flag mismatch");
_Static_assert((int)SYS_O_TRUNC == (int)VFS_O_TRUNC, "syscall and VFS trunc flag mismatch");
_Static_assert((int)SYS_SEEK_SET == (int)VFS_SEEK_SET, "syscall and VFS seek-set mismatch");
_Static_assert((int)SYS_SEEK_CUR == (int)VFS_SEEK_CUR, "syscall and VFS seek-cur mismatch");
_Static_assert((int)SYS_SEEK_END == (int)VFS_SEEK_END, "syscall and VFS seek-end mismatch");

static void copy_bytes(void *destination, const void *source, uint32_t length) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        dst[index] = src[index];
    }
}

static int copy_path_from_frame(const interrupt_frame_t *frame, uint32_t path_address, char *path, uint32_t path_length) {
    if (path == 0 || path_length == 0u || path_address == 0u) {
        return -1;
    }

    if (interrupt_from_user(frame)) {
        return user_copy_string_from(path, path_address, path_length);
    }

    copy_bytes(path, (const void *)(uintptr_t)path_address, path_length);
    path[path_length - 1u] = '\0';
    return 0;
}

static int32_t sys_write_handler(const interrupt_frame_t *frame, uint32_t fd, uint32_t buffer_address, uint32_t length) {
    char staging[SYSCALL_IO_CHUNK];
    const char *buffer = (const char *)(uintptr_t)buffer_address;
    uint32_t written = 0u;

    if (buffer == 0) {
        return -1;
    }

    while (written < length) {
        uint32_t chunk = length - written;
        int32_t write_result;

        if (chunk > sizeof(staging)) {
            chunk = sizeof(staging);
        }

        if (interrupt_from_user(frame)) {
            if (user_copy_from(staging, buffer_address + written, chunk) != 0) {
                return written != 0u ? (int32_t)written : -1;
            }
        } else {
            copy_bytes(staging, &buffer[written], chunk);
        }

        write_result = proc_write_fd(fd, staging, chunk);
        if (write_result < 0) {
            return written != 0u ? (int32_t)written : -1;
        }

        written += (uint32_t)write_result;
        if ((uint32_t)write_result < chunk) {
            break;
        }
    }

    return (int32_t)written;
}

static int32_t sys_read_handler(const interrupt_frame_t *frame, uint32_t fd, uint32_t buffer_address, uint32_t length) {
    char staging[SYSCALL_IO_CHUNK];
    char *buffer = (char *)(uintptr_t)buffer_address;
    uint32_t read_total = 0u;

    if (buffer == 0) {
        return -1;
    }

    while (read_total < length) {
        uint32_t chunk = length - read_total;
        int32_t read_result;

        if (chunk > sizeof(staging)) {
            chunk = sizeof(staging);
        }

        read_result = proc_read_fd(fd, staging, chunk);
        if (read_result < 0) {
            return read_total != 0u ? (int32_t)read_total : -1;
        }

        if (read_result == 0) {
            break;
        }

        if (interrupt_from_user(frame)) {
            if (user_copy_to(buffer_address + read_total, staging, (uint32_t)read_result) != 0) {
                return read_total != 0u ? (int32_t)read_total : -1;
            }
        } else {
            copy_bytes(&buffer[read_total], staging, (uint32_t)read_result);
        }

        read_total += (uint32_t)read_result;
        if ((uint32_t)read_result < chunk) {
            break;
        }
    }

    return (int32_t)read_total;
}

static int32_t sys_stat_handler(const interrupt_frame_t *frame, uint32_t path_address, uint32_t stat_address) {
    char path[VFS_PATH_MAX];
    vfs_stat_t kernel_stat;
    sys_stat_t user_stat;

    if (stat_address == 0u || copy_path_from_frame(frame, path_address, path, sizeof(path)) != 0) {
        return -1;
    }

    if (proc_stat(path, &kernel_stat) != 0) {
        return -1;
    }

    user_stat.kind = kernel_stat.kind;
    user_stat.size = kernel_stat.size;
    user_stat.child_count = kernel_stat.child_count;

    if (interrupt_from_user(frame)) {
        return user_copy_to(stat_address, &user_stat, sizeof(user_stat)) == 0 ? 0 : -1;
    }

    copy_bytes((void *)(uintptr_t)stat_address, &user_stat, sizeof(user_stat));
    return 0;
}

uint32_t syscall_dispatch(interrupt_frame_t *frame) {
    char path[VFS_PATH_MAX];

    switch (frame->eax) {
    case SYS_WRITE:
        frame->eax = (uint32_t)sys_write_handler(frame, frame->ebx, frame->ecx, frame->edx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_GETPID:
        frame->eax = proc_current_pid();
        return (uint32_t)(uintptr_t)frame;
    case SYS_YIELD:
        return 0;
        frame->eax = 0u;
        return proc_schedule(frame);
    case SYS_EXIT:
        return proc_sys_exit(frame, (int32_t)frame->ebx);
    case SYS_WAITPID:
        return proc_sys_waitpid(frame, (int32_t)frame->ebx, frame->ecx);
    case SYS_EXEC:
        return proc_sys_exec(frame, frame->ebx);
    case SYS_FORK:
        return proc_sys_fork(frame);
    case SYS_OPEN:
        if (copy_path_from_frame(frame, frame->ebx, path, sizeof(path)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            frame->eax = (uint32_t)proc_open_fd(path, frame->ecx);
        }
        return (uint32_t)(uintptr_t)frame;
    case SYS_CLOSE:
        frame->eax = (uint32_t)proc_close_fd(frame->ebx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_READ:
        frame->eax = (uint32_t)sys_read_handler(frame, frame->ebx, frame->ecx, frame->edx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_LSEEK:
        frame->eax = (uint32_t)proc_seek_fd(frame->ebx, (int32_t)frame->ecx, frame->edx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_MKDIR:
        if (copy_path_from_frame(frame, frame->ebx, path, sizeof(path)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            frame->eax = (uint32_t)proc_mkdir(path);
        }
        return (uint32_t)(uintptr_t)frame;
    case SYS_UNLINK:
        if (copy_path_from_frame(frame, frame->ebx, path, sizeof(path)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            frame->eax = (uint32_t)proc_unlink(path);
        }
        return (uint32_t)(uintptr_t)frame;
    case SYS_STAT:
        frame->eax = (uint32_t)sys_stat_handler(frame, frame->ebx, frame->ecx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_CHDIR:
        if (copy_path_from_frame(frame, frame->ebx, path, sizeof(path)) != 0) {
            frame->eax = (uint32_t)-1;
        } else {
            frame->eax = (uint32_t)proc_chdir(path);
        }
        return (uint32_t)(uintptr_t)frame;
    case SYS_DUP:
        frame->eax = (uint32_t)proc_dup(frame->ebx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_DUP2:
        frame->eax = (uint32_t)proc_dup2(frame->ebx, frame->ecx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_SLEEP:
        return proc_sys_sleep(frame, frame->ebx);
    case SYS_TICKS:
        frame->eax = proc_current_ticks();
        return (uint32_t)(uintptr_t)frame;
    default:
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }
}
