#ifndef RIVERIX_CONSOLE_H
#define RIVERIX_CONSOLE_H

#include <stdint.h>

void console_init(void);
void console_write(const char *message);
void console_write_len(const char *message, uint32_t length);
void console_write_hex32(uint32_t value);
void console_write_hex64(uint64_t value);

#endif
