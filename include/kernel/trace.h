#ifndef RIVERIX_TRACE_H
#define RIVERIX_TRACE_H

#include <stdint.h>

#include "shared/syscall_abi.h"

#define TRACE_CAPACITY 256u

void trace_init(void);
void trace_log(uint32_t category, uint32_t event, uint32_t arg0, uint32_t arg1, uint32_t arg2);
int32_t trace_get_info(sys_trace_info_t *info);
int32_t trace_read(uint32_t sequence, sys_trace_record_t *record);

#endif
