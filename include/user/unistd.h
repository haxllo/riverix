#ifndef RIVERIX_USER_UNISTD_H
#define RIVERIX_USER_UNISTD_H

#include <stdint.h>

int32_t write(int32_t fd, const void *buffer, uint32_t length);
int32_t read(int32_t fd, void *buffer, uint32_t length);
uint32_t getpid(void);
int32_t fork(void);
int32_t waitpid(int32_t pid, int32_t *status);
int32_t exec(const char *path);
int32_t execv(const char *path, const char *const *argv);
void exit(int32_t status) __attribute__((noreturn));
int32_t open(const char *path, uint32_t flags);
int32_t close(int32_t fd);
int32_t lseek(int32_t fd, int32_t offset, uint32_t whence);
int32_t getcwd(char *buffer, uint32_t length);
int32_t mkdir(const char *path);
int32_t unlink(const char *path);
int32_t chdir(const char *path);
int32_t dup(int32_t fd);
int32_t dup2(int32_t oldfd, int32_t newfd);
int32_t sleep(uint32_t ticks);
uint32_t ticks(void);

#endif
