#ifndef RIVERIX_PROC_H
#define RIVERIX_PROC_H

#include <stddef.h>
#include <stdint.h>

#include "kernel/idt.h"
#include "kernel/vfs.h"
#include "shared/syscall_abi.h"

typedef void (*task_entry_t)(void *arg);

void proc_init(void);
void proc_start_boot_tasks(void);
uint32_t proc_schedule(interrupt_frame_t *frame);
uint32_t proc_current_pid(void);
uint32_t proc_current_ticks(void);
int proc_preemptible_from_interrupt(const interrupt_frame_t *frame);
int32_t proc_write_fd(uint32_t fd, const char *buffer, uint32_t length);
int32_t proc_read_fd(uint32_t fd, void *buffer, uint32_t length);
int32_t proc_open_fd(const char *path, uint32_t flags);
int32_t proc_close_fd(uint32_t fd);
int32_t proc_seek_fd(uint32_t fd, int32_t offset, uint32_t whence);
int32_t proc_mkdir(const char *path);
int32_t proc_unlink(const char *path);
int32_t proc_stat(const char *path, vfs_stat_t *stat);
int32_t proc_readdir(const char *path, uint32_t index, sys_dirent_t *entry);
int32_t proc_procinfo(uint32_t index, sys_procinfo_t *info);
int32_t proc_getcwd(char *buffer, uint32_t length);
int32_t proc_chdir(const char *path);
int32_t proc_dup(uint32_t fd);
int32_t proc_dup2(uint32_t oldfd, uint32_t newfd);
uint32_t proc_getuid(void);
uint32_t proc_getgid(void);
int32_t proc_setuid(uint32_t uid);
int32_t proc_setgid(uint32_t gid);
int32_t proc_setsid(void);
int32_t proc_gettty(char *buffer, uint32_t length);
int32_t proc_pipe(int32_t *fds);
uint32_t proc_sys_ping4(interrupt_frame_t *frame, uint32_t destination_ipv4, uint32_t timeout_ticks);
uint32_t proc_sys_read(interrupt_frame_t *frame, uint32_t fd, uint32_t buffer_user, uint32_t length);
uint32_t proc_sys_write(interrupt_frame_t *frame, uint32_t fd, uint32_t buffer_user, uint32_t length);
uint32_t proc_sys_pipe(interrupt_frame_t *frame, uint32_t fds_user);
uint32_t proc_sys_exit(interrupt_frame_t *frame, int32_t status);
uint32_t proc_sys_waitpid(interrupt_frame_t *frame, int32_t pid, uint32_t status_user);
uint32_t proc_sys_exec(interrupt_frame_t *frame, uint32_t path_user);
uint32_t proc_sys_execv(interrupt_frame_t *frame, uint32_t path_user, uint32_t argv_user);
uint32_t proc_sys_fork(interrupt_frame_t *frame);
uint32_t proc_sys_sleep(interrupt_frame_t *frame, uint32_t ticks);

#endif
