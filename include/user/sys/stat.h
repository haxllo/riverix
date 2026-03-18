#ifndef RIVERIX_USER_SYS_STAT_H
#define RIVERIX_USER_SYS_STAT_H

#include "shared/syscall_abi.h"

typedef sys_stat_t stat_t;

int32_t stat(const char *path, stat_t *stat_buffer);

#endif
