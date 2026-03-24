#ifndef RIVERIX_VMBUS_H
#define RIVERIX_VMBUS_H

#include <stdint.h>

#define VMBUS_PACKET_DATA_INBAND 0x0006u
#define VMBUS_PACKET_COMPLETION 0x000Bu
#define VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED 0x0001u

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} vmbus_guid_t;

typedef struct {
    vmbus_guid_t interface_type;
    vmbus_guid_t interface_instance;
    uint32_t child_relid;
    uint32_t signal_connection_id;
    uint8_t monitor_id;
    uint8_t monitor_allocated;
    uint16_t is_dedicated_interrupt;
} vmbus_offer_t;

typedef struct {
    int open;
    uint32_t child_relid;
    uint32_t signal_connection_id;
    uint8_t monitor_id;
    uint8_t monitor_allocated;
    uint16_t is_dedicated_interrupt;
    uint32_t ring_pages_phys;
    void *ring_pages_virt;
    uint32_t ring_page_count;
    uint32_t send_page_count;
    uint32_t gpadl_handle;
    void *outbound_ring;
    void *inbound_ring;
    uint32_t inbound_read_index;
} vmbus_channel_t;

void vmbus_init(void);
void vmbus_poll(void);
int vmbus_ready(void);
uint32_t vmbus_protocol_version(void);
int vmbus_find_offer(const vmbus_guid_t *type_guid, vmbus_offer_t *out_offer);
int vmbus_open_channel(vmbus_channel_t *channel,
                       const vmbus_offer_t *offer,
                       uint32_t send_page_count,
                       uint32_t recv_page_count);
void vmbus_close_channel(vmbus_channel_t *channel);
int vmbus_send_inband(vmbus_channel_t *channel,
                      const void *payload,
                      uint32_t payload_size,
                      uint64_t request_id,
                      uint32_t flags);
int vmbus_recv_packet(vmbus_channel_t *channel,
                      void *payload_buffer,
                      uint32_t payload_capacity,
                      uint32_t *payload_size_out,
                      uint64_t *request_id_out,
                      uint16_t *packet_type_out);
const vmbus_guid_t *vmbus_keyboard_guid(void);

#endif
