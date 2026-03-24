#include "kernel/hyperv_keyboard.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/input.h"
#include "kernel/platform.h"
#include "kernel/vmbus.h"

#define SYNTH_KBD_VERSION_MAJOR 1u
#define SYNTH_KBD_VERSION_MINOR 0u
#define SYNTH_KBD_VERSION ((SYNTH_KBD_VERSION_MAJOR << 16) | SYNTH_KBD_VERSION_MINOR)

#define SYNTH_KBD_PROTOCOL_REQUEST 1u
#define SYNTH_KBD_PROTOCOL_RESPONSE 2u
#define SYNTH_KBD_EVENT 3u

#define PROTOCOL_ACCEPTED 0x00000001u

#define SYNTH_KBD_INFO_UNICODE 0x00000001u
#define SYNTH_KBD_INFO_BREAK 0x00000002u
#define SYNTH_KBD_INFO_E0 0x00000004u
#define SYNTH_KBD_INFO_E1 0x00000008u

#define HYPERV_KBD_SEND_PAGES 2u
#define HYPERV_KBD_RECV_PAGES 2u
#define HYPERV_KBD_MAX_PACKET_SIZE 256u
#define HYPERV_KBD_PROTOCOL_WAIT 200000u

typedef struct {
    uint32_t type;
} __attribute__((packed)) synth_kbd_msg_hdr_t;

typedef struct {
    synth_kbd_msg_hdr_t header;
    uint32_t version_requested;
} __attribute__((packed)) synth_kbd_protocol_request_t;

typedef struct {
    synth_kbd_msg_hdr_t header;
    uint32_t proto_status;
} __attribute__((packed)) synth_kbd_protocol_response_t;

typedef struct {
    synth_kbd_msg_hdr_t header;
    uint16_t make_code;
    uint16_t reserved0;
    uint32_t info;
} __attribute__((packed)) synth_kbd_keystroke_t;

static vmbus_channel_t hyperv_keyboard_channel;
static uint32_t hyperv_keyboard_ready;
static uint32_t hyperv_keyboard_protocol_ok;

static void hyperv_keyboard_handle_message(const void *message, uint32_t message_length) {
    const synth_kbd_msg_hdr_t *header = (const synth_kbd_msg_hdr_t *)message;

    if (message == 0 || message_length < sizeof(*header)) {
        return;
    }

    switch (header->type) {
    case SYNTH_KBD_PROTOCOL_RESPONSE: {
        const synth_kbd_protocol_response_t *response = (const synth_kbd_protocol_response_t *)message;

        if (message_length < sizeof(*response)) {
            return;
        }

        if ((response->proto_status & PROTOCOL_ACCEPTED) != 0u) {
            hyperv_keyboard_protocol_ok = 1u;
        }
        break;
    }
    case SYNTH_KBD_EVENT: {
        const synth_kbd_keystroke_t *keystroke = (const synth_kbd_keystroke_t *)message;
        uint8_t scan_code;

        if (message_length < sizeof(*keystroke)) {
            return;
        }

        if ((keystroke->info & SYNTH_KBD_INFO_UNICODE) != 0u) {
            return;
        }

        if ((keystroke->info & SYNTH_KBD_INFO_E0) != 0u) {
            input_process_xt_scancode(0xE0u);
        }
        if ((keystroke->info & SYNTH_KBD_INFO_E1) != 0u) {
            input_process_xt_scancode(0xE1u);
        }

        scan_code = (uint8_t)(keystroke->make_code & 0x7Fu);
        if ((keystroke->info & SYNTH_KBD_INFO_BREAK) != 0u) {
            scan_code |= 0x80u;
        }

        input_process_xt_scancode(scan_code);
        break;
    }
    default:
        break;
    }
}

int hyperv_keyboard_init(void) {
    synth_kbd_protocol_request_t request;
    vmbus_offer_t offer;
    uint8_t packet_buffer[HYPERV_KBD_MAX_PACKET_SIZE];
    uint32_t payload_length;
    uint16_t packet_type;
    uint64_t request_id;
    uint32_t attempts;

    if (!platform_is_hyperv() || !vmbus_ready()) {
        return 0;
    }

    if (hyperv_keyboard_ready != 0u) {
        return 1;
    }

    if (vmbus_find_offer(vmbus_keyboard_guid(), &offer) != 0) {
        console_write("hyperv-kbd: offer missing\n");
        return 0;
    }

    if (vmbus_open_channel(&hyperv_keyboard_channel, &offer, HYPERV_KBD_SEND_PAGES, HYPERV_KBD_RECV_PAGES) != 0) {
        console_write("hyperv-kbd: open failed\n");
        return 0;
    }

    request.header.type = SYNTH_KBD_PROTOCOL_REQUEST;
    request.version_requested = SYNTH_KBD_VERSION;
    hyperv_keyboard_protocol_ok = 0u;

    if (vmbus_send_inband(&hyperv_keyboard_channel,
                          &request,
                          sizeof(request),
                          1u,
                          VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED) != 0) {
        console_write("hyperv-kbd: protocol send failed\n");
        vmbus_close_channel(&hyperv_keyboard_channel);
        return 0;
    }

    for (attempts = 0u; attempts < HYPERV_KBD_PROTOCOL_WAIT; attempts++) {
        vmbus_poll();
        while (vmbus_recv_packet(&hyperv_keyboard_channel,
                                 packet_buffer,
                                 sizeof(packet_buffer),
                                 &payload_length,
                                 &request_id,
                                 &packet_type) != 0) {
            (void)request_id;
            if (packet_type == VMBUS_PACKET_DATA_INBAND || packet_type == VMBUS_PACKET_COMPLETION) {
                hyperv_keyboard_handle_message(packet_buffer, payload_length);
            }
        }

        if (hyperv_keyboard_protocol_ok != 0u) {
            hyperv_keyboard_ready = 1u;
            console_write("hyperv-kbd: ready\n");
            return 1;
        }
    }

    console_write("hyperv-kbd: protocol timeout\n");
    vmbus_close_channel(&hyperv_keyboard_channel);
    return 0;
}

void hyperv_keyboard_poll(void) {
    uint8_t packet_buffer[HYPERV_KBD_MAX_PACKET_SIZE];
    uint32_t payload_length;
    uint16_t packet_type;
    uint64_t request_id;

    if (hyperv_keyboard_ready == 0u) {
        return;
    }

    vmbus_poll();
    while (vmbus_recv_packet(&hyperv_keyboard_channel,
                             packet_buffer,
                             sizeof(packet_buffer),
                             &payload_length,
                             &request_id,
                             &packet_type) != 0) {
        (void)request_id;
        if (packet_type == VMBUS_PACKET_DATA_INBAND || packet_type == VMBUS_PACKET_COMPLETION) {
            hyperv_keyboard_handle_message(packet_buffer, payload_length);
        }
    }
}
