#ifndef RIVERIX_HYPERV_H
#define RIVERIX_HYPERV_H

#include <stdint.h>

void hyperv_init(void);
int hyperv_present(void);
int hyperv_hypercall_ready(void);
uint64_t hyperv_guest_os_id(void);
uint32_t hyperv_vp_index(void);
uint64_t hyperv_do_hypercall(uint64_t control, uint64_t input_physical, uint64_t output_physical);

#endif
