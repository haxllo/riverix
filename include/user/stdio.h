#ifndef RIVERIX_USER_STDIO_H
#define RIVERIX_USER_STDIO_H

#include <stdint.h>

int32_t putstr_fd(int32_t fd, const char *text);
int32_t puts(const char *text);
int32_t puthex32_fd(int32_t fd, uint32_t value);
int32_t puthex32(uint32_t value);
int32_t readline_fd(int32_t fd, char *buffer, uint32_t capacity);

#endif
