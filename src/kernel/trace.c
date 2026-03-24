#include "kernel/trace.h"

#include <stdint.h>

#include "kernel/pit.h"

static sys_trace_record_t trace_ring[TRACE_CAPACITY];
static uint32_t trace_next_sequence;
static uint32_t trace_count;
static uint32_t trace_ready;

static uint32_t irq_save(void) {
    uint32_t eflags;

    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(eflags) : : "memory");
    return eflags;
}

static void irq_restore(uint32_t eflags) {
    __asm__ volatile ("pushl %0; popfl" : : "r"(eflags) : "memory", "cc");
}

static void zero_bytes(void *buffer, uint32_t length) {
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        bytes[index] = 0u;
    }
}

void trace_init(void) {
    zero_bytes(trace_ring, sizeof(trace_ring));
    trace_next_sequence = 0u;
    trace_count = 0u;
    trace_ready = 1u;
}

void trace_log(uint32_t category, uint32_t event, uint32_t arg0, uint32_t arg1, uint32_t arg2) {
    sys_trace_record_t *record;
    uint32_t eflags;
    uint32_t slot;

    if (trace_ready == 0u) {
        return;
    }

    eflags = irq_save();
    slot = trace_next_sequence % TRACE_CAPACITY;
    record = &trace_ring[slot];
    record->sequence = trace_next_sequence;
    record->ticks = pit_ticks();
    record->category = category;
    record->event = event;
    record->arg0 = arg0;
    record->arg1 = arg1;
    record->arg2 = arg2;

    trace_next_sequence++;
    if (trace_count < TRACE_CAPACITY) {
        trace_count++;
    }
    irq_restore(eflags);
}

int32_t trace_get_info(sys_trace_info_t *info) {
    uint32_t eflags;

    if (info == 0 || trace_ready == 0u) {
        return -1;
    }

    eflags = irq_save();
    info->capacity = TRACE_CAPACITY;
    info->count = trace_count;
    info->next_sequence = trace_next_sequence;
    irq_restore(eflags);
    return 0;
}

int32_t trace_read(uint32_t sequence, sys_trace_record_t *record) {
    uint32_t eflags;
    uint32_t oldest_sequence;

    if (record == 0 || trace_ready == 0u) {
        return -1;
    }

    eflags = irq_save();
    if (trace_count == 0u) {
        irq_restore(eflags);
        return 1;
    }

    oldest_sequence = trace_next_sequence > trace_count ? (trace_next_sequence - trace_count) : 0u;
    if (sequence < oldest_sequence || sequence >= trace_next_sequence) {
        irq_restore(eflags);
        return 1;
    }

    *record = trace_ring[sequence % TRACE_CAPACITY];
    irq_restore(eflags);
    return 0;
}
