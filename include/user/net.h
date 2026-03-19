#ifndef RIVERIX_USER_NET_H
#define RIVERIX_USER_NET_H

#include <stdint.h>

#include "shared/syscall_abi.h"

typedef sys_netinfo_t netinfo_t;

#define PING_OK SYS_PING_OK
#define PING_ERR_TIMEOUT SYS_PING_ERR_TIMEOUT
#define PING_ERR_UNREACHABLE SYS_PING_ERR_UNREACHABLE
#define PING_ERR_NOT_READY SYS_PING_ERR_NOT_READY
#define PING_ERR_BUSY SYS_PING_ERR_BUSY
#define PING_ERR_INVALID SYS_PING_ERR_INVALID

int32_t getnetinfo(netinfo_t *info);
int32_t ping4(uint32_t ipv4_address, uint32_t timeout_ticks);

#endif
