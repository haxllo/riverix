#ifndef RIVERIX_USER_PROC_H
#define RIVERIX_USER_PROC_H

#include <stdint.h>

#include "shared/syscall_abi.h"

typedef sys_procinfo_t procinfo_t;

int32_t procinfo(uint32_t index, procinfo_t *info);

#endif
