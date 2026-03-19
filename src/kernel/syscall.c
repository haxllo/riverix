#include "kernel/syscall.h"

#include <stdint.h>

#include "kernel/bootinfo.h"
#include "kernel/idt.h"
#include "kernel/proc.h"
#include "kernel/usercopy.h"
#include "kernel/vfs.h"

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
    user_stat.mode = kernel_stat.mode;
    user_stat.uid = kernel_stat.uid;
    user_stat.gid = kernel_stat.gid;
    user_stat.links = kernel_stat.links;

    if (interrupt_from_user(frame)) {
        return user_copy_to(stat_address, &user_stat, sizeof(user_stat)) == 0 ? 0 : -1;
    }

    copy_bytes((void *)(uintptr_t)stat_address, &user_stat, sizeof(user_stat));
    return 0;
}

static int32_t sys_readdir_handler(const interrupt_frame_t *frame, uint32_t path_address, uint32_t index, uint32_t entry_address) {
    char path[VFS_PATH_MAX];
    sys_dirent_t entry;
    int32_t result;

    if (entry_address == 0u || copy_path_from_frame(frame, path_address, path, sizeof(path)) != 0) {
        return -1;
    }

    result = proc_readdir(path, index, &entry);
    if (result != 0) {
        return result;
    }

    if (interrupt_from_user(frame)) {
        return user_copy_to(entry_address, &entry, sizeof(entry)) == 0 ? 0 : -1;
    }

    copy_bytes((void *)(uintptr_t)entry_address, &entry, sizeof(entry));
    return 0;
}

static int32_t sys_procinfo_handler(const interrupt_frame_t *frame, uint32_t index, uint32_t info_address) {
    sys_procinfo_t info;
    int32_t result;

    if (info_address == 0u) {
        return -1;
    }

    result = proc_procinfo(index, &info);
    if (result != 0) {
        return result;
    }

    if (interrupt_from_user(frame)) {
        return user_copy_to(info_address, &info, sizeof(info)) == 0 ? 0 : -1;
    }

    copy_bytes((void *)(uintptr_t)info_address, &info, sizeof(info));
    return 0;
}

static int32_t sys_bootinfo_handler(const interrupt_frame_t *frame, uint32_t info_address) {
    sys_bootinfo_t info;

    if (info_address == 0u) {
        return -1;
    }

    info.root_policy = (uint32_t)bootinfo_root_policy();
    info.flags = bootinfo_flags();

    if (interrupt_from_user(frame)) {
        return user_copy_to(info_address, &info, sizeof(info)) == 0 ? 0 : -1;
    }

    copy_bytes((void *)(uintptr_t)info_address, &info, sizeof(info));
    return 0;
}

static int32_t sys_getcwd_handler(const interrupt_frame_t *frame, uint32_t buffer_address, uint32_t buffer_length) {
    char path[VFS_PATH_MAX];
    int32_t result;

    if (buffer_address == 0u || buffer_length == 0u) {
        return -1;
    }

    result = proc_getcwd(path, sizeof(path));
    if (result < 0 || ((uint32_t)result + 1u) > buffer_length) {
        return -1;
    }

    if (interrupt_from_user(frame)) {
        return user_copy_to(buffer_address, path, (uint32_t)result + 1u) == 0 ? result : -1;
    }

    copy_bytes((void *)(uintptr_t)buffer_address, path, (uint32_t)result + 1u);
    return result;
}

static int32_t sys_gettty_handler(const interrupt_frame_t *frame, uint32_t buffer_address, uint32_t buffer_length) {
    char tty[SYS_NAME_MAX];
    int32_t result;

    if (buffer_address == 0u || buffer_length == 0u) {
        return -1;
    }

    result = proc_gettty(tty, sizeof(tty));
    if (result < 0 || ((uint32_t)result + 1u) > buffer_length) {
        return -1;
    }

    if (interrupt_from_user(frame)) {
        return user_copy_to(buffer_address, tty, (uint32_t)result + 1u) == 0 ? result : -1;
    }

    copy_bytes((void *)(uintptr_t)buffer_address, tty, (uint32_t)result + 1u);
    return result;
}

uint32_t syscall_dispatch(interrupt_frame_t *frame) {
    char path[VFS_PATH_MAX];

    switch (frame->eax) {
    case SYS_WRITE:
        return proc_sys_write(frame, frame->ebx, frame->ecx, frame->edx);
    case SYS_GETPID:
        frame->eax = proc_current_pid();
        return (uint32_t)(uintptr_t)frame;
    case SYS_YIELD:
        frame->eax = 0u;
        return proc_schedule(frame);
    case SYS_EXIT:
        return proc_sys_exit(frame, (int32_t)frame->ebx);
    case SYS_WAITPID:
        return proc_sys_waitpid(frame, (int32_t)frame->ebx, frame->ecx);
    case SYS_EXEC:
        return proc_sys_exec(frame, frame->ebx);
    case SYS_EXECV:
        return proc_sys_execv(frame, frame->ebx, frame->ecx);
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
        return proc_sys_read(frame, frame->ebx, frame->ecx, frame->edx);
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
    case SYS_READDIR:
        frame->eax = (uint32_t)sys_readdir_handler(frame, frame->ebx, frame->ecx, frame->edx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_PROCINFO:
        frame->eax = (uint32_t)sys_procinfo_handler(frame, frame->ebx, frame->ecx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_BOOTINFO:
        frame->eax = (uint32_t)sys_bootinfo_handler(frame, frame->ebx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_GETCWD:
        frame->eax = (uint32_t)sys_getcwd_handler(frame, frame->ebx, frame->ecx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_REINSTALL_ROOTFS:
        frame->eax = (uint32_t)vfs_reinstall_rootfs();
        return (uint32_t)(uintptr_t)frame;
    case SYS_GETUID:
        frame->eax = proc_getuid();
        return (uint32_t)(uintptr_t)frame;
    case SYS_GETGID:
        frame->eax = proc_getgid();
        return (uint32_t)(uintptr_t)frame;
    case SYS_SETUID:
        frame->eax = (uint32_t)proc_setuid(frame->ebx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_SETGID:
        frame->eax = (uint32_t)proc_setgid(frame->ebx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_SETSID:
        frame->eax = (uint32_t)proc_setsid();
        return (uint32_t)(uintptr_t)frame;
    case SYS_GETTTY:
        frame->eax = (uint32_t)sys_gettty_handler(frame, frame->ebx, frame->ecx);
        return (uint32_t)(uintptr_t)frame;
    case SYS_PIPE:
        return proc_sys_pipe(frame, frame->ebx);
    default:
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }
}
