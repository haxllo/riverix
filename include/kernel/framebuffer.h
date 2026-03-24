#ifndef RIVERIX_FRAMEBUFFER_H
#define RIVERIX_FRAMEBUFFER_H

#include <stdint.h>

#include "kernel/bootinfo.h"

void framebuffer_init(const boot_framebuffer_info_t *info);
int framebuffer_available(void);
void framebuffer_write_char(char ch);

#endif
