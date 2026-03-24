#include <stdint.h>

#include "user/boot.h"
#include "user/net.h"
#include "user/stdio.h"
#include "user/trace.h"
#include "user/unistd.h"

static int trace_scan(traceinfo_t *info,
                      uint32_t *saw_boot,
                      uint32_t *saw_memory,
                      uint32_t *saw_proc,
                      uint32_t *saw_block,
                      uint32_t *saw_net,
                      uint32_t *saw_trap,
                      uint32_t *saw_panic) {
    tracerecord_t record;
    uint32_t start;
    uint32_t sequence;

    if (gettraceinfo(info) != 0) {
        return -1;
    }

    *saw_boot = 0u;
    *saw_memory = 0u;
    *saw_proc = 0u;
    *saw_block = 0u;
    *saw_net = 0u;
    *saw_trap = 0u;
    *saw_panic = 0u;

    start = info->next_sequence - info->count;
    for (sequence = start; sequence < info->next_sequence; sequence++) {
        if (readtrace(sequence, &record) != 0) {
            continue;
        }

        if (record.category == TRACE_CATEGORY_BOOT && record.event == TRACE_EVENT_BOOT_ROOT) {
            *saw_boot = 1u;
        } else if (record.category == TRACE_CATEGORY_MEMORY && record.event == TRACE_EVENT_MEMORY_STATE) {
            *saw_memory = 1u;
        } else if (record.category == TRACE_CATEGORY_BLOCK &&
                   (record.event == TRACE_EVENT_BLOCK_STORAGE || record.event == TRACE_EVENT_BLOCK_ROOTFS)) {
            *saw_block = 1u;
        } else if (record.category == TRACE_CATEGORY_PROC &&
                   (record.event == TRACE_EVENT_PROC_FORK ||
                    record.event == TRACE_EVENT_PROC_EXEC ||
                    record.event == TRACE_EVENT_PROC_EXIT ||
                    record.event == TRACE_EVENT_PROC_PING_WAKE)) {
            *saw_proc = 1u;
        } else if (record.category == TRACE_CATEGORY_NET &&
                   (record.event == TRACE_EVENT_NET_READY ||
                    record.event == TRACE_EVENT_NET_PING_START ||
                    record.event == TRACE_EVENT_NET_PING_REPLY)) {
            *saw_net = 1u;
        } else if (record.category == TRACE_CATEGORY_TRAP && record.event == TRACE_EVENT_TRAP_USER) {
            *saw_trap = 1u;
        } else if (record.category == TRACE_CATEGORY_PANIC) {
            *saw_panic = 1u;
        }
    }

    return 0;
}

static int run_soak_rounds(void) {
    bootinfo_t bootinfo;
    netinfo_t netinfo;
    traceinfo_t before;
    traceinfo_t after;
    uint32_t round;

    if (getbootinfo(&bootinfo) != 0 || (bootinfo.flags & BOOT_FLAG_SOAK) == 0u) {
        return 0;
    }

    if (getnetinfo(&netinfo) != 0 || netinfo.ready == 0u) {
        putstr_fd(2, "phase10: soak net unavailable\n");
        return 1;
    }

    if (gettraceinfo(&before) != 0) {
        putstr_fd(2, "phase10: soak trace unavailable\n");
        return 1;
    }

    putstr_fd(1, "phase10: soak begin\n");
    for (round = 1u; round <= 3u; round++) {
        putstr_fd(1, "phase10: soak round 0x");
        puthex32(round);
        putstr_fd(1, "\n");

        if (ping4(netinfo.ipv4_gateway, 120u) != PING_OK) {
            putstr_fd(2, "phase10: soak ping failed\n");
            return 1;
        }

        if (sleep(20u) != 0) {
            putstr_fd(2, "phase10: soak sleep failed\n");
            return 1;
        }
    }

    if (gettraceinfo(&after) != 0 || after.next_sequence <= before.next_sequence) {
        putstr_fd(2, "phase10: soak trace stalled\n");
        return 1;
    }

    putstr_fd(1, "phase10: soak done\n");
    return 0;
}

int main(int argc, char **argv) {
    traceinfo_t info;
    uint32_t saw_boot;
    uint32_t saw_memory;
    uint32_t saw_proc;
    uint32_t saw_block;
    uint32_t saw_net;
    uint32_t saw_trap;
    uint32_t saw_panic;

    (void)argc;
    (void)argv;

    putstr_fd(1, "phase10: trace begin\n");
    if (trace_scan(&info, &saw_boot, &saw_memory, &saw_proc, &saw_block, &saw_net, &saw_trap, &saw_panic) != 0) {
        putstr_fd(2, "phase10: trace unavailable\n");
        return 1;
    }

    putstr_fd(1, "phase10: trace count 0x");
    puthex32(info.count);
    putstr_fd(1, "\n");

    if (saw_boot == 0u || saw_memory == 0u || saw_block == 0u) {
        putstr_fd(2, "phase10: trace boot missing\n");
        return 1;
    }

    if (saw_proc == 0u) {
        putstr_fd(2, "phase10: trace proc missing\n");
        return 1;
    }

    if (saw_net == 0u) {
        putstr_fd(2, "phase10: trace net missing\n");
        return 1;
    }

    if (saw_trap == 0u) {
        putstr_fd(2, "phase10: trace trap missing\n");
        return 1;
    }

    if (saw_panic != 0u) {
        putstr_fd(2, "phase10: trace panic present\n");
        return 1;
    }

    putstr_fd(1, "phase10: trace boot ok\n");
    putstr_fd(1, "phase10: trace proc ok\n");
    putstr_fd(1, "phase10: trace net ok\n");
    putstr_fd(1, "phase10: trace trap ok\n");
    putstr_fd(1, "phase10: trace panic clear\n");

    if (run_soak_rounds() != 0) {
        return 1;
    }

    putstr_fd(1, "phase10: trace end\n");
    return 0;
}
