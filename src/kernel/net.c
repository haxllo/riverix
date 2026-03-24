#include "kernel/net.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/e1000.h"
#include "kernel/pit.h"
#include "kernel/trace.h"

#define NET_ETH_TYPE_ARP 0x0806u
#define NET_ETH_TYPE_IPV4 0x0800u
#define NET_ARP_REQUEST 1u
#define NET_ARP_REPLY 2u
#define NET_ARP_HW_ETHERNET 1u
#define NET_IPV4_PROTOCOL_ICMP 1u
#define NET_ICMP_ECHO_REPLY 0u
#define NET_ICMP_ECHO_REQUEST 8u
#define NET_IPV4_VERSION_IHL 0x45u
#define NET_FRAME_BUFFER_BYTES 1600u
#define NET_QEMU_IPV4 0x0A00020Fu
#define NET_QEMU_NETMASK 0xFFFFFF00u
#define NET_QEMU_GATEWAY 0x0A000202u
#define NET_PING_RETRY_TICKS 20u
#define NET_PING_DEFAULT_TIMEOUT 100u

typedef struct net_eth_header {
    uint8_t destination[6];
    uint8_t source[6];
    uint16_t ethertype_be;
} __attribute__((packed)) net_eth_header_t;

typedef struct net_arp_packet {
    uint16_t hardware_type_be;
    uint16_t protocol_type_be;
    uint8_t hardware_size;
    uint8_t protocol_size;
    uint16_t opcode_be;
    uint8_t sender_mac[6];
    uint32_t sender_ipv4_be;
    uint8_t target_mac[6];
    uint32_t target_ipv4_be;
} __attribute__((packed)) net_arp_packet_t;

typedef struct net_ipv4_header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length_be;
    uint16_t identification_be;
    uint16_t flags_fragment_be;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum_be;
    uint32_t source_ipv4_be;
    uint32_t destination_ipv4_be;
} __attribute__((packed)) net_ipv4_header_t;

typedef struct net_icmp_echo {
    uint8_t type;
    uint8_t code;
    uint16_t checksum_be;
    uint16_t identifier_be;
    uint16_t sequence_be;
} __attribute__((packed)) net_icmp_echo_t;

typedef enum net_ping_stage {
    NET_PING_STAGE_IDLE = 0,
    NET_PING_STAGE_ARP,
    NET_PING_STAGE_ECHO,
} net_ping_stage_t;

typedef struct net_ping_state {
    uint32_t active;
    uint32_t completed;
    uint32_t owner_pid;
    uint32_t destination_ipv4;
    uint32_t next_hop_ipv4;
    uint32_t deadline_tick;
    uint32_t next_action_tick;
    uint16_t identifier;
    uint16_t sequence;
    uint32_t attempts;
    net_ping_stage_t stage;
    int32_t result;
} net_ping_state_t;

typedef struct net_state {
    uint32_t ready;
    uint8_t mac[6];
    uint32_t ipv4_addr;
    uint32_t ipv4_netmask;
    uint32_t ipv4_gateway;
    uint32_t arp_valid;
    uint8_t arp_mac[6];
    uint32_t arp_ipv4;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint16_t next_ping_identifier;
    net_ping_state_t ping;
} net_state_t;

static net_state_t net_state;

static uint16_t net_bswap16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint32_t net_bswap32(uint32_t value) {
    return ((value & 0x000000FFu) << 24) |
           ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

static uint16_t net_htons(uint16_t value) {
    return net_bswap16(value);
}

static uint16_t net_ntohs(uint16_t value) {
    return net_bswap16(value);
}

static uint32_t net_htonl(uint32_t value) {
    return net_bswap32(value);
}

static uint32_t net_ntohl(uint32_t value) {
    return net_bswap32(value);
}

static void zero_bytes(void *buffer, uint32_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        bytes[index] = 0u;
    }
}

static void copy_bytes(void *destination, const void *source, uint32_t length) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        dst[index] = src[index];
    }
}

static int mac_equal(const uint8_t left[6], const uint8_t right[6]) {
    uint32_t index;

    for (index = 0u; index < 6u; index++) {
        if (left[index] != right[index]) {
            return 0;
        }
    }

    return 1;
}

static int mac_is_broadcast(const uint8_t mac[6]) {
    uint32_t index;

    for (index = 0u; index < 6u; index++) {
        if (mac[index] != 0xFFu) {
            return 0;
        }
    }

    return 1;
}

static uint16_t checksum16(const void *buffer, uint32_t length) {
    const uint8_t *bytes = (const uint8_t *)buffer;
    uint32_t sum = 0u;
    uint32_t index;

    for (index = 0u; index + 1u < length; index += 2u) {
        sum += ((uint32_t)bytes[index] << 8) | (uint32_t)bytes[index + 1u];
    }

    if ((length & 1u) != 0u) {
        sum += (uint32_t)bytes[length - 1u] << 8;
    }

    while ((sum >> 16) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

static uint32_t route_next_hop(uint32_t destination_ipv4) {
    if ((destination_ipv4 & net_state.ipv4_netmask) == (net_state.ipv4_addr & net_state.ipv4_netmask)) {
        return destination_ipv4;
    }

    return net_state.ipv4_gateway;
}

static int arp_cache_matches(uint32_t ipv4_addr) {
    return net_state.arp_valid != 0u && net_state.arp_ipv4 == ipv4_addr;
}

static void arp_cache_store(uint32_t ipv4_addr, const uint8_t mac[6]) {
    net_state.arp_valid = 1u;
    net_state.arp_ipv4 = ipv4_addr;
    copy_bytes(net_state.arp_mac, mac, 6u);
}

static int32_t send_frame(const void *frame, uint32_t length) {
    int32_t result = e1000_transmit(frame, length);

    if (result > 0) {
        net_state.tx_packets++;
    }

    return result;
}

static void ping_complete(int32_t result) {
    if (net_state.ping.active == 0u || net_state.ping.completed != 0u) {
        return;
    }

    net_state.ping.completed = 1u;
    net_state.ping.result = result;
}

static int32_t send_arp(uint16_t opcode, const uint8_t destination_mac[6], uint32_t target_ipv4, const uint8_t target_mac[6]) {
    struct {
        net_eth_header_t eth;
        net_arp_packet_t arp;
    } __attribute__((packed)) frame;

    zero_bytes(&frame, sizeof(frame));
    copy_bytes(frame.eth.destination, destination_mac, 6u);
    copy_bytes(frame.eth.source, net_state.mac, 6u);
    frame.eth.ethertype_be = net_htons(NET_ETH_TYPE_ARP);
    frame.arp.hardware_type_be = net_htons(NET_ARP_HW_ETHERNET);
    frame.arp.protocol_type_be = net_htons(NET_ETH_TYPE_IPV4);
    frame.arp.hardware_size = 6u;
    frame.arp.protocol_size = 4u;
    frame.arp.opcode_be = net_htons(opcode);
    copy_bytes(frame.arp.sender_mac, net_state.mac, 6u);
    frame.arp.sender_ipv4_be = net_htonl(net_state.ipv4_addr);
    copy_bytes(frame.arp.target_mac, target_mac, 6u);
    frame.arp.target_ipv4_be = net_htonl(target_ipv4);
    return send_frame(&frame, sizeof(frame));
}

static int32_t send_arp_request(uint32_t target_ipv4) {
    static const uint8_t broadcast_mac[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    static const uint8_t zero_mac[6] = {0u, 0u, 0u, 0u, 0u, 0u};

    return send_arp(NET_ARP_REQUEST, broadcast_mac, target_ipv4, zero_mac);
}

static int32_t send_arp_reply(const uint8_t destination_mac[6], uint32_t target_ipv4) {
    return send_arp(NET_ARP_REPLY, destination_mac, target_ipv4, destination_mac);
}

static int32_t send_icmp(uint8_t type,
                         uint32_t destination_ipv4,
                         const uint8_t destination_mac[6],
                         uint16_t identifier,
                         uint16_t sequence,
                         const void *payload,
                         uint32_t payload_length) {
    struct {
        net_eth_header_t eth;
        net_ipv4_header_t ipv4;
        net_icmp_echo_t icmp;
        uint8_t payload[16];
    } __attribute__((packed)) frame;
    uint32_t total_length;

    if (payload_length > sizeof(frame.payload)) {
        return -1;
    }

    zero_bytes(&frame, sizeof(frame));
    copy_bytes(frame.eth.destination, destination_mac, 6u);
    copy_bytes(frame.eth.source, net_state.mac, 6u);
    frame.eth.ethertype_be = net_htons(NET_ETH_TYPE_IPV4);

    frame.ipv4.version_ihl = NET_IPV4_VERSION_IHL;
    frame.ipv4.tos = 0u;
    total_length = (uint32_t)sizeof(frame.ipv4) + (uint32_t)sizeof(frame.icmp) + payload_length;
    frame.ipv4.total_length_be = net_htons((uint16_t)total_length);
    frame.ipv4.identification_be = net_htons(sequence);
    frame.ipv4.flags_fragment_be = 0u;
    frame.ipv4.ttl = 64u;
    frame.ipv4.protocol = NET_IPV4_PROTOCOL_ICMP;
    frame.ipv4.source_ipv4_be = net_htonl(net_state.ipv4_addr);
    frame.ipv4.destination_ipv4_be = net_htonl(destination_ipv4);
    frame.ipv4.checksum_be = net_htons(checksum16(&frame.ipv4, sizeof(frame.ipv4)));

    frame.icmp.type = type;
    frame.icmp.code = 0u;
    frame.icmp.identifier_be = net_htons(identifier);
    frame.icmp.sequence_be = net_htons(sequence);
    if (payload_length != 0u && payload != 0) {
        copy_bytes(frame.payload, payload, payload_length);
    }
    frame.icmp.checksum_be = net_htons(checksum16(&frame.icmp, (uint32_t)sizeof(frame.icmp) + payload_length));

    return send_frame(&frame, (uint32_t)sizeof(frame.eth) + total_length);
}

static int32_t send_icmp_echo_request(uint32_t destination_ipv4, const uint8_t destination_mac[6], uint16_t identifier, uint16_t sequence) {
    static const uint8_t payload[] = {'r', 'i', 'v', 'e', 'r', 'i', 'x', '\0'};

    return send_icmp(NET_ICMP_ECHO_REQUEST, destination_ipv4, destination_mac, identifier, sequence, payload, sizeof(payload));
}

static int32_t send_icmp_echo_reply(uint32_t destination_ipv4,
                                    const uint8_t destination_mac[6],
                                    uint16_t identifier,
                                    uint16_t sequence,
                                    const void *payload,
                                    uint32_t payload_length) {
    return send_icmp(NET_ICMP_ECHO_REPLY, destination_ipv4, destination_mac, identifier, sequence, payload, payload_length);
}

static void process_arp(const net_arp_packet_t *arp, uint32_t length) {
    uint16_t opcode;
    uint32_t sender_ipv4;
    uint32_t target_ipv4;

    if (length < sizeof(*arp) ||
        net_ntohs(arp->hardware_type_be) != NET_ARP_HW_ETHERNET ||
        net_ntohs(arp->protocol_type_be) != NET_ETH_TYPE_IPV4 ||
        arp->hardware_size != 6u ||
        arp->protocol_size != 4u) {
        return;
    }

    opcode = net_ntohs(arp->opcode_be);
    sender_ipv4 = net_ntohl(arp->sender_ipv4_be);
    target_ipv4 = net_ntohl(arp->target_ipv4_be);
    arp_cache_store(sender_ipv4, arp->sender_mac);
    trace_log(SYS_TRACE_CATEGORY_NET,
              SYS_TRACE_EVENT_NET_ARP,
              sender_ipv4,
              target_ipv4,
              opcode);

    if (net_state.ping.active != 0u &&
        net_state.ping.completed == 0u &&
        net_state.ping.stage == NET_PING_STAGE_ARP &&
        sender_ipv4 == net_state.ping.next_hop_ipv4 &&
        opcode == NET_ARP_REPLY) {
        net_state.ping.stage = NET_PING_STAGE_ECHO;
        net_state.ping.attempts = 0u;
        net_state.ping.next_action_tick = 0u;
    }

    if (opcode == NET_ARP_REQUEST && target_ipv4 == net_state.ipv4_addr) {
        (void)send_arp_reply(arp->sender_mac, sender_ipv4);
    }
}

static void process_icmp(const net_eth_header_t *eth,
                         const net_ipv4_header_t *ipv4,
                         const uint8_t *payload,
                         uint32_t payload_length) {
    const net_icmp_echo_t *icmp;
    uint16_t identifier;
    uint16_t sequence;

    if (payload_length < sizeof(net_icmp_echo_t)) {
        return;
    }

    icmp = (const net_icmp_echo_t *)payload;
    identifier = net_ntohs(icmp->identifier_be);
    sequence = net_ntohs(icmp->sequence_be);

    if (icmp->type == NET_ICMP_ECHO_REPLY &&
        net_state.ping.active != 0u &&
        net_state.ping.completed == 0u &&
        net_state.ping.stage == NET_PING_STAGE_ECHO &&
        net_ntohl(ipv4->source_ipv4_be) == net_state.ping.destination_ipv4 &&
        identifier == net_state.ping.identifier &&
        sequence == net_state.ping.sequence) {
        trace_log(SYS_TRACE_CATEGORY_NET,
                  SYS_TRACE_EVENT_NET_PING_REPLY,
                  net_state.ping.destination_ipv4,
                  identifier,
                  sequence);
        ping_complete(SYS_PING_OK);
        return;
    }

    if (icmp->type == NET_ICMP_ECHO_REQUEST) {
        const uint8_t *icmp_payload = payload + sizeof(*icmp);
        uint32_t icmp_payload_length = payload_length - (uint32_t)sizeof(*icmp);

        (void)send_icmp_echo_reply(net_ntohl(ipv4->source_ipv4_be),
                                   eth->source,
                                   identifier,
                                   sequence,
                                   icmp_payload,
                                   icmp_payload_length);
    }
}

static void process_ipv4(const net_eth_header_t *eth, const uint8_t *payload, uint32_t length) {
    const net_ipv4_header_t *ipv4;
    uint32_t header_length;
    uint32_t total_length;
    uint32_t destination_ipv4;

    if (length < sizeof(net_ipv4_header_t)) {
        return;
    }

    ipv4 = (const net_ipv4_header_t *)payload;
    if ((ipv4->version_ihl >> 4) != 4u) {
        return;
    }

    header_length = (uint32_t)(ipv4->version_ihl & 0x0Fu) * 4u;
    if (header_length < sizeof(*ipv4) || header_length > length) {
        return;
    }

    total_length = net_ntohs(ipv4->total_length_be);
    if (total_length < header_length || total_length > length) {
        return;
    }

    destination_ipv4 = net_ntohl(ipv4->destination_ipv4_be);
    if (destination_ipv4 != net_state.ipv4_addr) {
        return;
    }

    if (ipv4->protocol == NET_IPV4_PROTOCOL_ICMP) {
        process_icmp(eth, ipv4, payload + header_length, total_length - header_length);
    }
}

static void process_frame(const uint8_t *frame, uint32_t length) {
    const net_eth_header_t *eth;
    uint16_t ethertype;

    if (length < sizeof(net_eth_header_t)) {
        return;
    }

    eth = (const net_eth_header_t *)frame;
    if (!mac_equal(eth->destination, net_state.mac) && !mac_is_broadcast(eth->destination)) {
        return;
    }

    ethertype = net_ntohs(eth->ethertype_be);
    if (ethertype == NET_ETH_TYPE_ARP) {
        process_arp((const net_arp_packet_t *)(frame + sizeof(*eth)), length - (uint32_t)sizeof(*eth));
    } else if (ethertype == NET_ETH_TYPE_IPV4) {
        process_ipv4(eth, frame + sizeof(*eth), length - (uint32_t)sizeof(*eth));
    }
}

static void drive_ping(void) {
    uint32_t now;

    if (net_state.ping.active == 0u || net_state.ping.completed != 0u) {
        return;
    }

    now = pit_ticks();
    if ((int32_t)(now - net_state.ping.deadline_tick) >= 0) {
        trace_log(SYS_TRACE_CATEGORY_NET,
                  SYS_TRACE_EVENT_NET_PING_TIMEOUT,
                  net_state.ping.destination_ipv4,
                  net_state.ping.attempts,
                  0u);
        ping_complete(SYS_PING_ERR_TIMEOUT);
        return;
    }

    if ((int32_t)(now - net_state.ping.next_action_tick) < 0) {
        return;
    }

    if (net_state.ping.stage == NET_PING_STAGE_ARP) {
        if (send_arp_request(net_state.ping.next_hop_ipv4) < 0) {
            return;
        }
    } else if (net_state.ping.stage == NET_PING_STAGE_ECHO) {
        const uint8_t *destination_mac = arp_cache_matches(net_state.ping.next_hop_ipv4) ? net_state.arp_mac : 0;

        if (destination_mac == 0 ||
            send_icmp_echo_request(net_state.ping.destination_ipv4,
                                   destination_mac,
                                   net_state.ping.identifier,
                                   net_state.ping.sequence) < 0) {
            return;
        }
    } else {
        return;
    }

    net_state.ping.attempts++;
    net_state.ping.next_action_tick = now + NET_PING_RETRY_TICKS;
}

int32_t net_init(void) {
    zero_bytes(&net_state, sizeof(net_state));
    net_state.ipv4_addr = NET_QEMU_IPV4;
    net_state.ipv4_netmask = NET_QEMU_NETMASK;
    net_state.ipv4_gateway = NET_QEMU_GATEWAY;
    net_state.next_ping_identifier = 1u;

    if (e1000_init() != 0 || e1000_get_mac(net_state.mac) != 0) {
        console_write("net: unavailable\n");
        return -1;
    }

    net_state.ready = 1u;
    console_write("net: ready\n");
    trace_log(SYS_TRACE_CATEGORY_NET,
              SYS_TRACE_EVENT_NET_READY,
              net_state.ipv4_addr,
              net_state.ipv4_gateway,
              0u);
    return 0;
}

void net_poll(void) {
    uint8_t frame[NET_FRAME_BUFFER_BYTES];
    uint32_t budget = 8u;

    if (net_state.ready == 0u) {
        return;
    }

    while (budget-- != 0u) {
        int32_t result = e1000_receive(frame, sizeof(frame));

        if (result == 0) {
            break;
        }

        if (result < 0) {
            continue;
        }

        net_state.rx_packets++;
        process_frame(frame, (uint32_t)result);
    }

    drive_ping();
}

int net_ready(void) {
    return net_state.ready != 0u;
}

int32_t net_get_info(sys_netinfo_t *info) {
    if (info == 0) {
        return -1;
    }

    zero_bytes(info, sizeof(*info));
    info->ready = net_state.ready;
    copy_bytes(info->mac, net_state.mac, sizeof(info->mac));
    info->ipv4_addr = net_state.ipv4_addr;
    info->ipv4_netmask = net_state.ipv4_netmask;
    info->ipv4_gateway = net_state.ipv4_gateway;
    info->arp_valid = net_state.arp_valid;
    copy_bytes(info->arp_mac, net_state.arp_mac, sizeof(info->arp_mac));
    info->arp_ipv4 = net_state.arp_ipv4;
    info->rx_packets = net_state.rx_packets;
    info->tx_packets = net_state.tx_packets;
    return 0;
}

int32_t net_ping4_start(uint32_t requester_pid, uint32_t destination_ipv4, uint32_t timeout_ticks) {
    if (net_state.ready == 0u) {
        return SYS_PING_ERR_NOT_READY;
    }

    if (requester_pid == 0u || destination_ipv4 == 0u) {
        return SYS_PING_ERR_INVALID;
    }

    if (net_state.ping.active != 0u && net_state.ping.completed == 0u) {
        return SYS_PING_ERR_BUSY;
    }

    zero_bytes(&net_state.ping, sizeof(net_state.ping));
    net_state.ping.active = 1u;
    net_state.ping.owner_pid = requester_pid;
    net_state.ping.destination_ipv4 = destination_ipv4;
    net_state.ping.next_hop_ipv4 = route_next_hop(destination_ipv4);
    net_state.ping.deadline_tick = pit_ticks() + (timeout_ticks != 0u ? timeout_ticks : NET_PING_DEFAULT_TIMEOUT);
    net_state.ping.identifier = net_state.next_ping_identifier++;
    net_state.ping.sequence = 1u;
    net_state.ping.stage = arp_cache_matches(net_state.ping.next_hop_ipv4) ? NET_PING_STAGE_ECHO : NET_PING_STAGE_ARP;
    net_state.ping.next_action_tick = 0u;
    trace_log(SYS_TRACE_CATEGORY_NET,
              SYS_TRACE_EVENT_NET_PING_START,
              destination_ipv4,
              net_state.ping.next_hop_ipv4,
              timeout_ticks != 0u ? timeout_ticks : NET_PING_DEFAULT_TIMEOUT);
    drive_ping();
    return 0;
}

int32_t net_ping4_poll_result(uint32_t requester_pid, int32_t *result) {
    if (result == 0 || net_state.ping.active == 0u || net_state.ping.owner_pid != requester_pid) {
        return -1;
    }

    if (net_state.ping.completed == 0u) {
        return 0;
    }

    *result = net_state.ping.result;
    zero_bytes(&net_state.ping, sizeof(net_state.ping));
    return 1;
}
