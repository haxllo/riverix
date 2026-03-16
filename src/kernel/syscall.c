#include "kernel/syscall.h"

#include <stdint.h>

#include "kernel/idt.h"
#include "kernel/proc.h"
#include "kernel/usercopy.h"

static int32_t sys_write_handler(const interrupt_frame_t *frame, uint32_t fd, uint32_t buffer_address, uint32_t length) {
    char staging[256];
    const char *buffer = (const char *)(uintptr_t)buffer_address;
    uint32_t index;

    if (length == 0u) {
        return 0;
    }

    if (buffer == 0) {
        return -1;
    }

    if (length > 256u) {
        length = 256u;
    }

    if (interrupt_from_user(frame)) {
        if (user_copy_from(staging, buffer_address, length) != 0) {
            return -1;
        }
    } else {
        for (index = 0u; index < length; index++) {
            staging[index] = buffer[index];
        }
    }

    return proc_write_fd(fd, staging, length);
}

uint32_t syscall_dispatch(interrupt_frame_t *frame) {
    switch (frame->eax) {
    case SYS_WRITE:
        frame->eax = (uint32_t)sys_write_handler(frame, frame->ebx, frame->ecx, frame->edx);
        return (uint32_t)(uintptr_t)frame;
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
    case SYS_FORK:
        return proc_sys_fork(frame);
    default:
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }
}
