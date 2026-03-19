#ifndef RIVERIX_NET_H
#define RIVERIX_NET_H

#include <stdint.h>

#include "shared/syscall_abi.h"

int32_t net_init(void);
void net_poll(void);
int net_ready(void);
int32_t net_get_info(sys_netinfo_t *info);
int32_t net_ping4_start(uint32_t requester_pid, uint32_t destination_ipv4, uint32_t timeout_ticks);
int32_t net_ping4_poll_result(uint32_t requester_pid, int32_t *result);

#endif
