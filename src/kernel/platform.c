#include "kernel/platform.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/cpu.h"

static platform_kind_t current_platform = PLATFORM_KIND_GENERIC;

static int strings_equal(const char *left, const char *right) {
    uint32_t index = 0u;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }

        index++;
    }

    return left[index] == right[index];
}

void platform_init(void) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    char vendor[13];

    cpu_cpuid(1u, 0u, &eax, &ebx, &ecx, &edx);
    if ((ecx & (1u << 31)) == 0u) {
        current_platform = PLATFORM_KIND_GENERIC;
        console_write("platform: generic\n");
        return;
    }

    cpu_cpuid(0x40000000u, 0u, &eax, &ebx, &ecx, &edx);
    ((uint32_t *)vendor)[0] = ebx;
    ((uint32_t *)vendor)[1] = ecx;
    ((uint32_t *)vendor)[2] = edx;
    vendor[12] = '\0';

    if (strings_equal(vendor, "Microsoft Hv")) {
        current_platform = PLATFORM_KIND_HYPERV;
    } else {
        current_platform = PLATFORM_KIND_HYPERVISOR_OTHER;
    }

    console_write("platform: ");
    console_write(platform_name());
    console_write("\n");
}

platform_kind_t platform_kind(void) {
    return current_platform;
}

int platform_is_hyperv(void) {
    return current_platform == PLATFORM_KIND_HYPERV;
}

const char *platform_name(void) {
    switch (current_platform) {
    case PLATFORM_KIND_HYPERV:
        return "hyper-v";
    case PLATFORM_KIND_HYPERVISOR_OTHER:
        return "hypervisor";
    case PLATFORM_KIND_GENERIC:
    default:
        return "generic";
    }
}
