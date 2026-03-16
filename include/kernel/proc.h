#ifndef RIVERIX_PROC_H
#define RIVERIX_PROC_H

#include <stddef.h>
#include <stdint.h>

#include "kernel/idt.h"

typedef void (*task_entry_t)(void *arg);

void proc_init(void);
void proc_start_boot_tasks(void);
uint32_t proc_schedule(interrupt_frame_t *frame);
uint32_t proc_current_pid(void);
int32_t proc_write_fd(uint32_t fd, const char *buffer, uint32_t length);
uint32_t proc_sys_exit(interrupt_frame_t *frame, int32_t status);
uint32_t proc_sys_waitpid(interrupt_frame_t *frame, int32_t pid, uint32_t status_user);
uint32_t proc_sys_exec(interrupt_frame_t *frame, uint32_t path_user);
uint32_t proc_sys_fork(interrupt_frame_t *frame);

#endif
