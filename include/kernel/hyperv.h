#ifndef RIVERIX_HYPERV_H
#define RIVERIX_HYPERV_H

#include <stdint.h>

#define HYPERV_VMBUS_SINT_INDEX 2u

void hyperv_init(void);
int hyperv_present(void);
int hyperv_hypercall_ready(void);
int hyperv_synic_ready(void);
uint64_t hyperv_guest_os_id(void);
uint32_t hyperv_vp_index(void);
uint64_t hyperv_do_hypercall(uint64_t control, uint64_t input_physical, uint64_t output_physical);
int hyperv_post_message(uint32_t connection_id, uint32_t message_type, const void *payload, uint32_t payload_size);
int hyperv_poll_message(uint32_t sint_index,
                        void *payload_buffer,
                        uint32_t payload_capacity,
                        uint32_t *message_type_out,
                        uint32_t *payload_size_out);
int hyperv_signal_event(uint32_t connection_id);
uint32_t hyperv_monitor_page(uint32_t page_index);
void hyperv_set_monitor_pending(uint32_t page_index, uint32_t monitor_id);

#endif
