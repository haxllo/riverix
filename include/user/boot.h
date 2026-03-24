#ifndef RIVERIX_USER_BOOT_H
#define RIVERIX_USER_BOOT_H

#include <stdint.h>

#include "shared/syscall_abi.h"

typedef sys_bootinfo_t bootinfo_t;

#define BOOT_ROOT_AUTO SYS_BOOT_ROOT_AUTO
#define BOOT_ROOT_DISK SYS_BOOT_ROOT_DISK
#define BOOT_ROOT_RAMDISK SYS_BOOT_ROOT_RAMDISK

#define BOOT_FLAG_RECOVERY SYS_BOOT_FLAG_RECOVERY
#define BOOT_FLAG_REINSTALL SYS_BOOT_FLAG_REINSTALL
#define BOOT_FLAG_SOAK SYS_BOOT_FLAG_SOAK

int32_t getbootinfo(bootinfo_t *info);
int32_t reinstall_rootfs(void);

#endif
