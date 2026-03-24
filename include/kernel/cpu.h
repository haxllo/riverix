#ifndef RIVERIX_CPU_H
#define RIVERIX_CPU_H

#include <stdint.h>

static inline void cpu_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ volatile (
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(subleaf));

    if (eax != 0) {
        *eax = a;
    }
    if (ebx != 0) {
        *ebx = b;
    }
    if (ecx != 0) {
        *ecx = c;
    }
    if (edx != 0) {
        *edx = d;
    }
}

static inline uint64_t cpu_rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void cpu_wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile ("wrmsr"
                      :
                      : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

#endif
