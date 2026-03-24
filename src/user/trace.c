#include <stdint.h>

#include "user/stdio.h"
#include "user/trace.h"

#define TRACE_DEFAULT_COUNT 8u

static uint32_t parse_count(const char *text, uint32_t fallback) {
    uint32_t value = 0u;
    uint32_t index = 0u;

    if (text == 0 || text[0] == '\0') {
        return fallback;
    }

    while (text[index] != '\0') {
        if (text[index] < '0' || text[index] > '9') {
            return fallback;
        }

        value = (value * 10u) + (uint32_t)(text[index] - '0');
        index++;
    }

    return value != 0u ? value : fallback;
}

static const char *trace_category_name(uint32_t category) {
    switch (category) {
    case TRACE_CATEGORY_BOOT:
        return "boot";
    case TRACE_CATEGORY_MEMORY:
        return "memory";
    case TRACE_CATEGORY_PROC:
        return "proc";
    case TRACE_CATEGORY_BLOCK:
        return "block";
    case TRACE_CATEGORY_NET:
        return "net";
    case TRACE_CATEGORY_TRAP:
        return "trap";
    case TRACE_CATEGORY_PANIC:
        return "panic";
    default:
        return "unknown";
    }
}

static const char *trace_event_name(uint32_t event) {
    switch (event) {
    case TRACE_EVENT_BOOT_ROOT:
        return "boot-root";
    case TRACE_EVENT_MEMORY_STATE:
        return "memory-state";
    case TRACE_EVENT_PROC_FORK:
        return "proc-fork";
    case TRACE_EVENT_PROC_EXEC:
        return "proc-exec";
    case TRACE_EVENT_PROC_EXIT:
        return "proc-exit";
    case TRACE_EVENT_PROC_PING_BLOCK:
        return "proc-ping-block";
    case TRACE_EVENT_PROC_PING_WAKE:
        return "proc-ping-wake";
    case TRACE_EVENT_BLOCK_STORAGE:
        return "block-storage";
    case TRACE_EVENT_BLOCK_ROOTFS:
        return "block-rootfs";
    case TRACE_EVENT_NET_READY:
        return "net-ready";
    case TRACE_EVENT_NET_ARP:
        return "net-arp";
    case TRACE_EVENT_NET_PING_START:
        return "net-ping-start";
    case TRACE_EVENT_NET_PING_REPLY:
        return "net-ping-reply";
    case TRACE_EVENT_NET_PING_TIMEOUT:
        return "net-ping-timeout";
    case TRACE_EVENT_TRAP_USER:
        return "trap-user";
    case TRACE_EVENT_TRAP_KERNEL:
        return "trap-kernel";
    case TRACE_EVENT_PANIC_FATAL:
        return "panic-fatal";
    default:
        return "unknown";
    }
}

static void print_record(const tracerecord_t *record) {
    putstr_fd(1, "trace: seq 0x");
    puthex32(record->sequence);
    putstr_fd(1, " tick 0x");
    puthex32(record->ticks);
    putstr_fd(1, " cat ");
    putstr_fd(1, trace_category_name(record->category));
    putstr_fd(1, " evt ");
    putstr_fd(1, trace_event_name(record->event));
    putstr_fd(1, " a0 0x");
    puthex32(record->arg0);
    putstr_fd(1, " a1 0x");
    puthex32(record->arg1);
    putstr_fd(1, " a2 0x");
    puthex32(record->arg2);
    putstr_fd(1, "\n");
}

int main(int argc, char **argv) {
    traceinfo_t info;
    tracerecord_t record;
    uint32_t count = TRACE_DEFAULT_COUNT;
    uint32_t start;
    uint32_t sequence;

    if (argc >= 2) {
        count = parse_count(argv[1], TRACE_DEFAULT_COUNT);
    }

    if (gettraceinfo(&info) != 0) {
        putstr_fd(2, "trace: unavailable\n");
        return 1;
    }

    putstr_fd(1, "trace: count 0x");
    puthex32(info.count);
    putstr_fd(1, " next 0x");
    puthex32(info.next_sequence);
    putstr_fd(1, "\n");

    if (info.count == 0u) {
        return 0;
    }

    if (count > info.count) {
        count = info.count;
    }

    start = info.next_sequence - count;
    for (sequence = start; sequence < info.next_sequence; sequence++) {
        if (readtrace(sequence, &record) != 0) {
            continue;
        }

        print_record(&record);
    }

    return 0;
}
