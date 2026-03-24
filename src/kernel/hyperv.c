#include "kernel/hyperv.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/cpu.h"
#include "kernel/paging.h"
#include "kernel/palloc.h"
#include "kernel/platform.h"

#define HV_X64_MSR_GUEST_OS_ID 0x40000000u
#define HV_X64_MSR_HYPERCALL 0x40000001u
#define HV_X64_MSR_VP_INDEX 0x40000002u
#define HV_X64_MSR_HYPERCALL_ENABLE 0x0000000000000001ull
#define HV_X64_MSR_HYPERCALL_PAGE_MASK 0xFFFFFFFFFFFFF000ull
#define HV_HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS 0x40000000u
#define HV_HYPERV_CPUID_INTERFACE 0x40000001u
#define HV_HYPERV_CPUID_FEATURES 0x40000003u
#define HV_HYPERV_INTERFACE_HV1 0x31237648u
#define HYPERV_RIVERIX_VENDOR_ID 0x8300u

static uint32_t hyperv_max_leaf;
static uint32_t hyperv_interface_signature;
static uint32_t hyperv_features_eax;
static uint32_t hyperv_features_ebx;
static uint32_t hyperv_features_ecx;
static uint32_t hyperv_features_edx;
static uint32_t hyperv_hypercall_page_phys;
static void *hyperv_hypercall_page_virt;
static uint64_t hyperv_guest_id_value;
static int hyperv_guest_ready;

extern uint64_t hyperv_hypercall_asm(void *target,
                                     uint32_t control_low,
                                     uint32_t control_high,
                                     uint32_t input_low,
                                     uint32_t input_high,
                                     uint32_t output_low,
                                     uint32_t output_high);

static void zero_page(uint32_t physical_address) {
    uint8_t *page = (uint8_t *)paging_phys_to_virt(physical_address);
    uint32_t index;

    for (index = 0u; index < PAGE_SIZE; index++) {
        page[index] = 0u;
    }
}

static uint64_t hyperv_build_guest_id(void) {
    return ((uint64_t)HYPERV_RIVERIX_VENDOR_ID << 48) |
           ((uint64_t)0x0001u << 16) |
           0x0001u;
}

static void hyperv_collect_cpuid_state(void) {
    uint32_t eax;

    cpu_cpuid(HV_HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS, 0u, &eax, 0, 0, 0);
    hyperv_max_leaf = eax;

    if (hyperv_max_leaf >= HV_HYPERV_CPUID_INTERFACE) {
        cpu_cpuid(HV_HYPERV_CPUID_INTERFACE, 0u, &hyperv_interface_signature, 0, 0, 0);
    } else {
        hyperv_interface_signature = 0u;
    }

    if (hyperv_max_leaf >= HV_HYPERV_CPUID_FEATURES) {
        cpu_cpuid(HV_HYPERV_CPUID_FEATURES, 0u,
                  &hyperv_features_eax,
                  &hyperv_features_ebx,
                  &hyperv_features_ecx,
                  &hyperv_features_edx);
    } else {
        hyperv_features_eax = 0u;
        hyperv_features_ebx = 0u;
        hyperv_features_ecx = 0u;
        hyperv_features_edx = 0u;
    }
}

static int hyperv_setup_hypercall_page(void) {
    uint64_t hypercall_msr_value;

    hyperv_hypercall_page_phys = palloc_alloc_page();
    if (hyperv_hypercall_page_phys == 0u) {
        console_write("hyperv: hypercall page alloc failed\n");
        return -1;
    }

    hyperv_hypercall_page_virt = (void *)paging_phys_to_virt(hyperv_hypercall_page_phys);
    zero_page(hyperv_hypercall_page_phys);

    hyperv_guest_id_value = hyperv_build_guest_id();
    cpu_wrmsr(HV_X64_MSR_GUEST_OS_ID, hyperv_guest_id_value);

    hypercall_msr_value = ((uint64_t)hyperv_hypercall_page_phys & HV_X64_MSR_HYPERCALL_PAGE_MASK) |
                          HV_X64_MSR_HYPERCALL_ENABLE;
    cpu_wrmsr(HV_X64_MSR_HYPERCALL, hypercall_msr_value);

    hypercall_msr_value = cpu_rdmsr(HV_X64_MSR_HYPERCALL);
    if ((hypercall_msr_value & HV_X64_MSR_HYPERCALL_ENABLE) == 0u) {
        console_write("hyperv: hypercall enable failed\n");
        return -1;
    }

    return 0;
}

void hyperv_init(void) {
    if (!platform_is_hyperv()) {
        return;
    }

    hyperv_collect_cpuid_state();

    console_write("hyperv: max leaf 0x");
    console_write_hex32(hyperv_max_leaf);
    console_write(" interface 0x");
    console_write_hex32(hyperv_interface_signature);
    console_write("\n");

    if (hyperv_interface_signature != HV_HYPERV_INTERFACE_HV1) {
        console_write("hyperv: unsupported interface\n");
        return;
    }

    if (hyperv_setup_hypercall_page() != 0) {
        return;
    }

    hyperv_guest_ready = 1;

    console_write("hyperv: guest id 0x");
    console_write_hex64(hyperv_guest_id_value);
    console_write(" hypercall page 0x");
    console_write_hex32(hyperv_hypercall_page_phys);
    console_write(" vp 0x");
    console_write_hex32(hyperv_vp_index());
    console_write("\n");
}

int hyperv_present(void) {
    return platform_is_hyperv();
}

int hyperv_hypercall_ready(void) {
    return hyperv_guest_ready;
}

uint64_t hyperv_guest_os_id(void) {
    return hyperv_guest_id_value;
}

uint32_t hyperv_vp_index(void) {
    if (!platform_is_hyperv()) {
        return 0xFFFFFFFFu;
    }

    return (uint32_t)cpu_rdmsr(HV_X64_MSR_VP_INDEX);
}

uint64_t hyperv_do_hypercall(uint64_t control, uint64_t input_physical, uint64_t output_physical) {
    if (!hyperv_guest_ready || hyperv_hypercall_page_virt == 0) {
        return ~0ull;
    }

    return hyperv_hypercall_asm(hyperv_hypercall_page_virt,
                                (uint32_t)control,
                                (uint32_t)(control >> 32),
                                (uint32_t)input_physical,
                                (uint32_t)(input_physical >> 32),
                                (uint32_t)output_physical,
                                (uint32_t)(output_physical >> 32));
}
