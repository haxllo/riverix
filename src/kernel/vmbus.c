#include "kernel/vmbus.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/hyperv.h"
#include "kernel/io.h"
#include "kernel/paging.h"
#include "kernel/palloc.h"

#define VMBUS_MAKE_VERSION(major, minor) ((((uint32_t)(major)) << 16) | (uint32_t)(minor))
#define VMBUS_VERSION_WIN10_V4_0 VMBUS_MAKE_VERSION(4u, 0u)
#define VMBUS_VERSION_WIN10_V4_1 VMBUS_MAKE_VERSION(4u, 1u)
#define VMBUS_VERSION_WIN10_V5_0 VMBUS_MAKE_VERSION(5u, 0u)
#define VMBUS_VERSION_WIN10_V5_1 VMBUS_MAKE_VERSION(5u, 1u)
#define VMBUS_VERSION_WIN10_V5_2 VMBUS_MAKE_VERSION(5u, 2u)
#define VMBUS_VERSION_WIN10_V5_3 VMBUS_MAKE_VERSION(5u, 3u)

#define VMBUS_MESSAGE_CONNECTION_ID 1u
#define VMBUS_MESSAGE_CONNECTION_ID_4 4u
#define VMBUS_MESSAGE_TYPE_CHANNEL 1u

#define VMBUS_MAX_MESSAGE_SIZE 240u
#define VMBUS_MAX_USER_DEFINED_BYTES 120u
#define VMBUS_MAX_RING_PAYLOAD 256u
#define VMBUS_RING_HEADER_SIZE PAGE_SIZE
#define VMBUS_RING_MIN_PAGES 2u
#define VMBUS_PKT_TRAILER 8u
#define VMBUS_GPADL_HANDLE_BASE 0x000E1E10u
#define VMBUS_INTERRUPT_PAGE_BYTES PAGE_SIZE
#define VMBUS_INTERRUPT_PAGE_HALF_BYTES (VMBUS_INTERRUPT_PAGE_BYTES / 2u)
#define VMBUS_INTERRUPT_PAGE_HALF_BITS (VMBUS_INTERRUPT_PAGE_HALF_BYTES * 8u)

#define CHANNELMSG_OFFERCHANNEL 1u
#define CHANNELMSG_REQUESTOFFERS 3u
#define CHANNELMSG_ALLOFFERS_DELIVERED 4u
#define CHANNELMSG_OPENCHANNEL 5u
#define CHANNELMSG_OPENCHANNEL_RESULT 6u
#define CHANNELMSG_GPADL_HEADER 8u
#define CHANNELMSG_GPADL_CREATED 10u
#define CHANNELMSG_INITIATE_CONTACT 14u
#define CHANNELMSG_VERSION_RESPONSE 15u

typedef struct {
    uint32_t msgtype;
    uint32_t padding;
} __attribute__((packed)) vmbus_channel_message_header_t;

typedef struct {
    vmbus_guid_t if_type;
    vmbus_guid_t if_instance;
    uint64_t reserved1;
    uint64_t reserved2;
    uint16_t chn_flags;
    uint16_t mmio_megabytes;
    uint8_t user_defined[VMBUS_MAX_USER_DEFINED_BYTES];
    uint16_t sub_channel_index;
    uint16_t reserved3;
} __attribute__((packed)) vmbus_channel_offer_payload_t;

typedef struct {
    vmbus_channel_message_header_t header;
    vmbus_channel_offer_payload_t offer;
    uint32_t child_relid;
    uint8_t monitorid;
    uint8_t monitor_allocated : 1;
    uint8_t reserved : 7;
    uint16_t is_dedicated_interrupt : 1;
    uint16_t reserved1 : 15;
    uint32_t connection_id;
} __attribute__((packed)) vmbus_channel_offer_channel_t;

typedef struct {
    vmbus_channel_message_header_t header;
    uint32_t vmbus_version_requested;
    uint32_t target_vcpu;
    union {
        uint64_t interrupt_page;
        struct {
            uint8_t msg_sint;
            uint8_t msg_vtl;
            uint8_t reserved[2];
            uint32_t feature_flags;
        } __attribute__((packed));
    } __attribute__((packed));
    uint64_t monitor_page1;
    uint64_t monitor_page2;
} __attribute__((packed)) vmbus_channel_initiate_contact_t;

typedef struct {
    vmbus_channel_message_header_t header;
    uint8_t version_supported;
    uint8_t connection_state;
    uint16_t padding;
    uint32_t msg_conn_id;
} __attribute__((packed)) vmbus_channel_version_response_t;

typedef struct {
    vmbus_channel_message_header_t header;
    uint32_t child_relid;
    uint32_t gpadl;
    uint16_t range_buflen;
    uint16_t rangecount;
    struct {
        uint32_t byte_count;
        uint32_t byte_offset;
        uint64_t pfn_array[VMBUS_RING_MIN_PAGES * 2u];
    } __attribute__((packed)) range;
} __attribute__((packed)) vmbus_channel_gpadl_header_t;

typedef struct {
    vmbus_channel_message_header_t header;
    uint32_t child_relid;
    uint32_t gpadl;
    uint32_t creation_status;
} __attribute__((packed)) vmbus_channel_gpadl_created_t;

typedef struct {
    vmbus_channel_message_header_t header;
    uint32_t child_relid;
    uint32_t openid;
    uint32_t ringbuffer_gpadlhandle;
    uint32_t target_vp;
    uint32_t downstream_ringbuffer_pageoffset;
    uint8_t userdata[VMBUS_MAX_USER_DEFINED_BYTES];
} __attribute__((packed)) vmbus_channel_open_channel_t;

typedef struct {
    vmbus_channel_message_header_t header;
    uint32_t child_relid;
    uint32_t openid;
    uint32_t status;
} __attribute__((packed)) vmbus_channel_open_result_t;

typedef struct {
    uint32_t write_index;
    uint32_t read_index;
    uint32_t interrupt_mask;
    uint32_t pending_send_sz;
    uint32_t reserved1[12];
    uint32_t feature_bits;
    uint8_t reserved2[PAGE_SIZE - 68u];
    uint8_t buffer[];
} __attribute__((packed)) vmbus_ring_buffer_t;

typedef struct {
    uint16_t type;
    uint16_t offset8;
    uint16_t len8;
    uint16_t flags;
    uint64_t trans_id;
} __attribute__((packed)) vmbus_packet_descriptor_t;

static const uint32_t vmbus_supported_versions[] = {
    VMBUS_VERSION_WIN10_V5_3,
    VMBUS_VERSION_WIN10_V5_2,
    VMBUS_VERSION_WIN10_V5_1,
    VMBUS_VERSION_WIN10_V5_0,
    VMBUS_VERSION_WIN10_V4_1,
    VMBUS_VERSION_WIN10_V4_0,
};

static const vmbus_guid_t vmbus_keyboard_guid_value = {
    0xF912AD6Du,
    0x2B17u,
    0x48EAu,
    { 0xBDu, 0x65u, 0xF9u, 0x27u, 0xA6u, 0x1Cu, 0x76u, 0x84u },
};

static uint32_t vmbus_connected;
static uint32_t vmbus_protocol;
static uint32_t vmbus_message_connection_id;
static uint32_t vmbus_next_gpadl_handle;
static uint32_t vmbus_interrupt_page_phys;
static uint8_t *vmbus_interrupt_page_virt;
static uint32_t vmbus_have_version_response;
static uint32_t vmbus_have_gpadl_response;
static uint32_t vmbus_have_open_response;
static uint32_t vmbus_offers_delivered;
static vmbus_channel_version_response_t vmbus_version_response;
static vmbus_channel_gpadl_created_t vmbus_gpadl_response;
static vmbus_channel_open_result_t vmbus_open_response;
static vmbus_offer_t vmbus_keyboard_offer;
static uint32_t vmbus_keyboard_offer_valid;

static void copy_bytes(void *dst, const void *src, uint32_t length) {
    uint8_t *dst_bytes = (uint8_t *)dst;
    const uint8_t *src_bytes = (const uint8_t *)src;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        dst_bytes[index] = src_bytes[index];
    }
}

static void zero_bytes(void *buffer, uint32_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        bytes[index] = 0u;
    }
}

static uint32_t min_u32(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

static uint32_t align_up_u32(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void vmbus_set_interrupt_bit(uint32_t relid) {
    volatile uint8_t *send_page;

    if (vmbus_interrupt_page_virt == 0 || relid >= VMBUS_INTERRUPT_PAGE_HALF_BITS) {
        return;
    }

    send_page = (volatile uint8_t *)(vmbus_interrupt_page_virt + VMBUS_INTERRUPT_PAGE_HALF_BYTES);
    send_page[relid >> 3] |= (uint8_t)(1u << (relid & 7u));
}

static int vmbus_guid_equal(const vmbus_guid_t *left, const vmbus_guid_t *right) {
    uint32_t index;

    if (left == 0 || right == 0) {
        return 0;
    }

    if (left->data1 != right->data1 || left->data2 != right->data2 || left->data3 != right->data3) {
        return 0;
    }

    for (index = 0u; index < sizeof(left->data4); index++) {
        if (left->data4[index] != right->data4[index]) {
            return 0;
        }
    }

    return 1;
}

static void vmbus_copy_from_ring(const uint8_t *ring,
                                 uint32_t ring_size,
                                 uint32_t offset,
                                 void *buffer,
                                 uint32_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        bytes[index] = ring[(offset + index) % ring_size];
    }
}

static void vmbus_copy_to_ring(uint8_t *ring,
                               uint32_t ring_size,
                               uint32_t offset,
                               const void *buffer,
                               uint32_t length) {
    const uint8_t *bytes = (const uint8_t *)buffer;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        ring[(offset + index) % ring_size] = bytes[index];
    }
}

static uint32_t vmbus_ring_data_size(const vmbus_ring_buffer_t *ring, uint32_t page_count) {
    (void)ring;
    return (page_count * PAGE_SIZE) - VMBUS_RING_HEADER_SIZE;
}

static uint32_t vmbus_ring_bytes_to_write(const vmbus_ring_buffer_t *ring, uint32_t page_count) {
    uint32_t ring_size = vmbus_ring_data_size(ring, page_count);
    uint32_t read_index = ring->read_index;
    uint32_t write_index = ring->write_index;

    return write_index >= read_index ? ring_size - (write_index - read_index) :
                                       read_index - write_index;
}

static uint32_t vmbus_ring_bytes_to_read(const vmbus_ring_buffer_t *ring, uint32_t page_count) {
    uint32_t ring_size = vmbus_ring_data_size(ring, page_count);
    uint32_t read_index = ring->read_index;
    uint32_t write_index = ring->write_index;

    return write_index >= read_index ? write_index - read_index :
                                       (ring_size - read_index) + write_index;
}

static int vmbus_post_control_message(const void *message, uint32_t message_size) {
    uint32_t attempts;

    for (attempts = 0u; attempts < 256u; attempts++) {
        int result = hyperv_post_message(vmbus_message_connection_id,
                                         VMBUS_MESSAGE_TYPE_CHANNEL,
                                         message,
                                         message_size);
        if (result == 0) {
            return 0;
        }

        if (result != -2) {
            return -1;
        }

        io_wait();
    }

    return -1;
}

static void vmbus_handle_offer(const vmbus_channel_offer_channel_t *offer) {
    vmbus_guid_t interface_type;

    if (offer == 0) {
        return;
    }

    interface_type = offer->offer.if_type;
    if (!vmbus_guid_equal(&interface_type, &vmbus_keyboard_guid_value)) {
        return;
    }

    vmbus_keyboard_offer.interface_type = offer->offer.if_type;
    vmbus_keyboard_offer.interface_instance = offer->offer.if_instance;
    vmbus_keyboard_offer.child_relid = offer->child_relid;
    vmbus_keyboard_offer.signal_connection_id = offer->connection_id;
    vmbus_keyboard_offer.monitor_id = offer->monitorid;
    vmbus_keyboard_offer.monitor_allocated = offer->monitor_allocated != 0u ? 1u : 0u;
    vmbus_keyboard_offer.is_dedicated_interrupt = offer->is_dedicated_interrupt;
    vmbus_keyboard_offer_valid = 1u;

    console_write("vmbus: keyboard offer relid 0x");
    console_write_hex32(offer->child_relid);
    console_write(" conn 0x");
    console_write_hex32(offer->connection_id);
    console_write(" monitor 0x");
    console_write_hex32(offer->monitorid);
    console_write("\n");
}

static void vmbus_signal_channel(const vmbus_channel_t *channel) {
    if (channel == 0) {
        return;
    }

    vmbus_set_interrupt_bit(channel->child_relid);

    if (channel->monitor_allocated != 0u) {
        hyperv_set_monitor_pending(1u, channel->monitor_id);
        return;
    }

    if (channel->signal_connection_id != 0u) {
        (void)hyperv_signal_event(channel->signal_connection_id);
    }
}

void vmbus_poll(void) {
    uint8_t message_buffer[VMBUS_MAX_MESSAGE_SIZE];
    uint32_t message_type;
    uint32_t payload_size;

    while (hyperv_poll_message(HYPERV_VMBUS_SINT_INDEX,
                               message_buffer,
                               sizeof(message_buffer),
                               &message_type,
                               &payload_size) != 0) {
        vmbus_channel_message_header_t *header;

        if (message_type != VMBUS_MESSAGE_TYPE_CHANNEL ||
            payload_size < sizeof(vmbus_channel_message_header_t)) {
            continue;
        }

        header = (vmbus_channel_message_header_t *)message_buffer;
        switch (header->msgtype) {
        case CHANNELMSG_VERSION_RESPONSE:
            if (payload_size >= sizeof(vmbus_channel_version_response_t)) {
                copy_bytes(&vmbus_version_response, message_buffer, sizeof(vmbus_version_response));
                vmbus_have_version_response = 1u;
            }
            break;
        case CHANNELMSG_OFFERCHANNEL:
            if (payload_size >= sizeof(vmbus_channel_offer_channel_t)) {
                vmbus_handle_offer((const vmbus_channel_offer_channel_t *)message_buffer);
            }
            break;
        case CHANNELMSG_ALLOFFERS_DELIVERED:
            vmbus_offers_delivered = 1u;
            break;
        case CHANNELMSG_GPADL_CREATED:
            if (payload_size >= sizeof(vmbus_channel_gpadl_created_t)) {
                copy_bytes(&vmbus_gpadl_response, message_buffer, sizeof(vmbus_gpadl_response));
                vmbus_have_gpadl_response = 1u;
            }
            break;
        case CHANNELMSG_OPENCHANNEL_RESULT:
            if (payload_size >= sizeof(vmbus_channel_open_result_t)) {
                copy_bytes(&vmbus_open_response, message_buffer, sizeof(vmbus_open_response));
                vmbus_have_open_response = 1u;
            }
            break;
        default:
            break;
        }
    }
}

static int vmbus_wait_for_flag(const uint32_t *flag) {
    uint32_t attempts;

    for (attempts = 0u; attempts < 1000000u; attempts++) {
        vmbus_poll();
        if (*flag != 0u) {
            return 0;
        }
        io_wait();
    }

    return -1;
}

void vmbus_init(void) {
    uint32_t index;

    if (!hyperv_present() || !hyperv_synic_ready()) {
        return;
    }

    if (vmbus_connected != 0u) {
        return;
    }

    vmbus_message_connection_id = VMBUS_MESSAGE_CONNECTION_ID_4;
    vmbus_next_gpadl_handle = VMBUS_GPADL_HANDLE_BASE;
    vmbus_keyboard_offer_valid = 0u;

    if (vmbus_interrupt_page_virt == 0) {
        vmbus_interrupt_page_phys = palloc_alloc_page();
        if (vmbus_interrupt_page_phys == 0u) {
            console_write("vmbus: interrupt page alloc failed\n");
            return;
        }

        vmbus_interrupt_page_virt = (uint8_t *)paging_phys_to_virt(vmbus_interrupt_page_phys);
        zero_bytes(vmbus_interrupt_page_virt, VMBUS_INTERRUPT_PAGE_BYTES);
    }

    for (index = 0u; index < (sizeof(vmbus_supported_versions) / sizeof(vmbus_supported_versions[0])); index++) {
        vmbus_channel_initiate_contact_t contact;

        zero_bytes(&contact, sizeof(contact));
        contact.header.msgtype = CHANNELMSG_INITIATE_CONTACT;
        contact.vmbus_version_requested = vmbus_supported_versions[index];
        contact.target_vcpu = hyperv_vp_index();
        if (vmbus_supported_versions[index] >= VMBUS_VERSION_WIN10_V5_0) {
            contact.msg_sint = (uint8_t)HYPERV_VMBUS_SINT_INDEX;
            contact.msg_vtl = 0u;
            contact.feature_flags = 0u;
        } else {
            contact.interrupt_page = vmbus_interrupt_page_phys;
            vmbus_message_connection_id = VMBUS_MESSAGE_CONNECTION_ID;
        }
        contact.monitor_page1 = hyperv_monitor_page(0u);
        contact.monitor_page2 = hyperv_monitor_page(1u);

        vmbus_have_version_response = 0u;
        if (vmbus_post_control_message(&contact, sizeof(contact)) != 0) {
            continue;
        }

        if (vmbus_wait_for_flag(&vmbus_have_version_response) != 0) {
            continue;
        }

        if (vmbus_version_response.version_supported == 0u) {
            continue;
        }

        vmbus_protocol = vmbus_supported_versions[index];
        if (vmbus_version_response.msg_conn_id != 0u) {
            vmbus_message_connection_id = vmbus_version_response.msg_conn_id;
        }
        vmbus_connected = 1u;
        break;
    }

    if (vmbus_connected == 0u) {
        console_write("vmbus: connect failed\n");
        return;
    }

    {
        vmbus_channel_message_header_t request;

        zero_bytes(&request, sizeof(request));
        request.msgtype = CHANNELMSG_REQUESTOFFERS;
        vmbus_offers_delivered = 0u;

        if (vmbus_post_control_message(&request, sizeof(request)) != 0 ||
            vmbus_wait_for_flag(&vmbus_offers_delivered) != 0) {
            console_write("vmbus: offers failed\n");
            return;
        }
    }

    console_write("vmbus: connected version 0x");
    console_write_hex32(vmbus_protocol);
    console_write(" msg conn 0x");
    console_write_hex32(vmbus_message_connection_id);
    console_write(" int page 0x");
    console_write_hex32(vmbus_interrupt_page_phys);
    console_write("\n");

    if (vmbus_keyboard_offer_valid == 0u) {
        console_write("vmbus: keyboard offer missing\n");
    }
}

int vmbus_ready(void) {
    return vmbus_connected != 0u;
}

uint32_t vmbus_protocol_version(void) {
    return vmbus_protocol;
}

int vmbus_find_offer(const vmbus_guid_t *type_guid, vmbus_offer_t *out_offer) {
    if (out_offer == 0 || type_guid == 0) {
        return -1;
    }

    if (vmbus_keyboard_offer_valid == 0u ||
        !vmbus_guid_equal(type_guid, &vmbus_keyboard_offer.interface_type)) {
        return -1;
    }

    *out_offer = vmbus_keyboard_offer;
    return 0;
}

int vmbus_open_channel(vmbus_channel_t *channel,
                       const vmbus_offer_t *offer,
                       uint32_t send_page_count,
                       uint32_t recv_page_count) {
    vmbus_channel_gpadl_header_t gpadl;
    vmbus_channel_open_channel_t open_message;
    uint32_t total_pages;
    uint32_t base_phys;
    uint32_t page_index;

    if (channel == 0 || offer == 0 || send_page_count < VMBUS_RING_MIN_PAGES || recv_page_count < VMBUS_RING_MIN_PAGES) {
        return -1;
    }

    total_pages = send_page_count + recv_page_count;
    if (total_pages > (VMBUS_RING_MIN_PAGES * 2u)) {
        return -1;
    }

    base_phys = palloc_alloc_pages(total_pages);
    if (base_phys == 0u) {
        return -1;
    }

    zero_bytes((void *)paging_phys_to_virt(base_phys), total_pages * PAGE_SIZE);

    zero_bytes(channel, sizeof(*channel));
    channel->child_relid = offer->child_relid;
    channel->signal_connection_id = offer->signal_connection_id;
    channel->monitor_id = offer->monitor_id;
    channel->monitor_allocated = offer->monitor_allocated;
    channel->is_dedicated_interrupt = offer->is_dedicated_interrupt;
    channel->ring_pages_phys = base_phys;
    channel->ring_pages_virt = (void *)paging_phys_to_virt(base_phys);
    channel->ring_page_count = total_pages;
    channel->send_page_count = send_page_count;
    channel->outbound_ring = channel->ring_pages_virt;
    channel->inbound_ring = (void *)((uintptr_t)channel->ring_pages_virt + (send_page_count * PAGE_SIZE));
    channel->gpadl_handle = vmbus_next_gpadl_handle++;

    ((vmbus_ring_buffer_t *)channel->outbound_ring)->feature_bits = 1u;
    ((vmbus_ring_buffer_t *)channel->inbound_ring)->feature_bits = 1u;

    zero_bytes(&gpadl, sizeof(gpadl));
    gpadl.header.msgtype = CHANNELMSG_GPADL_HEADER;
    gpadl.child_relid = offer->child_relid;
    gpadl.gpadl = channel->gpadl_handle;
    gpadl.rangecount = 1u;
    gpadl.range.byte_count = total_pages * PAGE_SIZE;
    gpadl.range.byte_offset = 0u;
    gpadl.range_buflen = (uint16_t)(sizeof(gpadl.range.byte_count) +
                                    sizeof(gpadl.range.byte_offset) +
                                    (total_pages * sizeof(uint64_t)));

    for (page_index = 0u; page_index < total_pages; page_index++) {
        gpadl.range.pfn_array[page_index] = ((uint64_t)base_phys / PAGE_SIZE) + page_index;
    }

    vmbus_have_gpadl_response = 0u;
    if (vmbus_post_control_message(&gpadl, sizeof(gpadl.header) +
                                           sizeof(gpadl.child_relid) +
                                           sizeof(gpadl.gpadl) +
                                           sizeof(gpadl.range_buflen) +
                                           sizeof(gpadl.rangecount) +
                                           sizeof(gpadl.range.byte_count) +
                                           sizeof(gpadl.range.byte_offset) +
                                           (total_pages * sizeof(uint64_t))) != 0 ||
        vmbus_wait_for_flag(&vmbus_have_gpadl_response) != 0 ||
        vmbus_gpadl_response.creation_status != 0u ||
        vmbus_gpadl_response.child_relid != offer->child_relid) {
        palloc_free_pages_range(base_phys, total_pages);
        zero_bytes(channel, sizeof(*channel));
        return -1;
    }

    zero_bytes(&open_message, sizeof(open_message));
    open_message.header.msgtype = CHANNELMSG_OPENCHANNEL;
    open_message.child_relid = offer->child_relid;
    open_message.openid = offer->child_relid;
    open_message.ringbuffer_gpadlhandle = channel->gpadl_handle;
    open_message.target_vp = hyperv_vp_index();
    open_message.downstream_ringbuffer_pageoffset = send_page_count;

    vmbus_have_open_response = 0u;
    if (vmbus_post_control_message(&open_message, sizeof(open_message)) != 0 ||
        vmbus_wait_for_flag(&vmbus_have_open_response) != 0 ||
        vmbus_open_response.status != 0u ||
        vmbus_open_response.child_relid != offer->child_relid) {
        palloc_free_pages_range(base_phys, total_pages);
        zero_bytes(channel, sizeof(*channel));
        return -1;
    }

    channel->open = 1;
    return 0;
}

void vmbus_close_channel(vmbus_channel_t *channel) {
    if (channel == 0 || channel->open == 0) {
        return;
    }

    palloc_free_pages_range(channel->ring_pages_phys, channel->ring_page_count);
    zero_bytes(channel, sizeof(*channel));
}

int vmbus_send_inband(vmbus_channel_t *channel,
                      const void *payload,
                      uint32_t payload_size,
                      uint64_t request_id,
                      uint32_t flags) {
    vmbus_ring_buffer_t *ring;
    uint8_t *ring_data;
    vmbus_packet_descriptor_t descriptor;
    uint32_t ring_size;
    uint32_t old_write;
    uint32_t write_index;
    uint32_t packet_length;
    uint32_t aligned_packet_length;
    uint32_t total_write_length;
    uint64_t trailer = 0ull;

    if (channel == 0 || channel->open == 0 || payload == 0) {
        return -1;
    }

    ring = (vmbus_ring_buffer_t *)channel->outbound_ring;
    ring_size = vmbus_ring_data_size(ring, channel->send_page_count);
    ring_data = ring->buffer;

    packet_length = sizeof(descriptor) + payload_size;
    aligned_packet_length = align_up_u32(packet_length, sizeof(uint64_t));
    total_write_length = aligned_packet_length + VMBUS_PKT_TRAILER;

    if (vmbus_ring_bytes_to_write(ring, channel->send_page_count) <= total_write_length) {
        return -1;
    }

    zero_bytes(&descriptor, sizeof(descriptor));
    descriptor.type = VMBUS_PACKET_DATA_INBAND;
    descriptor.offset8 = (uint16_t)(sizeof(descriptor) >> 3);
    descriptor.len8 = (uint16_t)(aligned_packet_length >> 3);
    descriptor.flags = (uint16_t)flags;
    descriptor.trans_id = request_id;

    write_index = ring->write_index;
    old_write = write_index;
    vmbus_copy_to_ring(ring_data, ring_size, write_index, &descriptor, sizeof(descriptor));
    write_index = (write_index + sizeof(descriptor)) % ring_size;
    vmbus_copy_to_ring(ring_data, ring_size, write_index, payload, payload_size);
    write_index = (write_index + payload_size) % ring_size;

    if (aligned_packet_length > packet_length) {
        uint8_t padding[8];
        uint32_t padding_length = aligned_packet_length - packet_length;

        zero_bytes(padding, sizeof(padding));
        vmbus_copy_to_ring(ring_data, ring_size, write_index, padding, padding_length);
        write_index = (write_index + padding_length) % ring_size;
    }

    vmbus_copy_to_ring(ring_data, ring_size, write_index, &trailer, sizeof(trailer));
    write_index = (write_index + sizeof(trailer)) % ring_size;
    ring->write_index = write_index;

    if (old_write == ring->read_index) {
        vmbus_signal_channel(channel);
    }

    return 0;
}

int vmbus_recv_packet(vmbus_channel_t *channel,
                      void *payload_buffer,
                      uint32_t payload_capacity,
                      uint32_t *payload_size_out,
                      uint64_t *request_id_out,
                      uint16_t *packet_type_out) {
    vmbus_ring_buffer_t *ring;
    uint8_t *ring_data;
    vmbus_packet_descriptor_t descriptor;
    uint32_t ring_size;
    uint32_t payload_offset;
    uint32_t payload_length;
    uint32_t read_index;

    if (payload_size_out != 0) {
        *payload_size_out = 0u;
    }
    if (request_id_out != 0) {
        *request_id_out = 0ull;
    }
    if (packet_type_out != 0) {
        *packet_type_out = 0u;
    }

    if (channel == 0 || channel->open == 0) {
        return 0;
    }

    ring = (vmbus_ring_buffer_t *)channel->inbound_ring;
    ring_size = vmbus_ring_data_size(ring, channel->ring_page_count - channel->send_page_count);
    if (vmbus_ring_bytes_to_read(ring, channel->ring_page_count - channel->send_page_count) < sizeof(descriptor)) {
        return 0;
    }

    ring_data = ring->buffer;
    read_index = channel->inbound_read_index;
    vmbus_copy_from_ring(ring_data, ring_size, read_index, &descriptor, sizeof(descriptor));

    payload_offset = (uint32_t)descriptor.offset8 << 3;
    payload_length = ((uint32_t)descriptor.len8 << 3) - payload_offset;

    if (payload_buffer != 0 && payload_capacity != 0u) {
        uint32_t copy_length = min_u32(payload_length, payload_capacity);
        vmbus_copy_from_ring(ring_data,
                             ring_size,
                             read_index + payload_offset,
                             payload_buffer,
                             copy_length);
    }

    channel->inbound_read_index = read_index + ((uint32_t)descriptor.len8 << 3) + VMBUS_PKT_TRAILER;
    if (channel->inbound_read_index >= ring_size) {
        channel->inbound_read_index %= ring_size;
    }
    ring->read_index = channel->inbound_read_index;

    if (payload_size_out != 0) {
        *payload_size_out = payload_length;
    }
    if (request_id_out != 0) {
        *request_id_out = descriptor.trans_id;
    }
    if (packet_type_out != 0) {
        *packet_type_out = descriptor.type;
    }

    return 1;
}

const vmbus_guid_t *vmbus_keyboard_guid(void) {
    return &vmbus_keyboard_guid_value;
}
