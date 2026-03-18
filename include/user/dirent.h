#ifndef RIVERIX_USER_DIRENT_H
#define RIVERIX_USER_DIRENT_H

#include <stdint.h>

#include "shared/syscall_abi.h"

typedef sys_dirent_t dirent_t;

int32_t readdir(const char *path, uint32_t index, dirent_t *entry);

#endif
