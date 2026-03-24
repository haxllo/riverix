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
#define HV_X64_MSR_SCONTROL 0x40000080u
#define HV_X64_MSR_SIEFP 0x40000082u
#define HV_X64_MSR_SIMP 0x40000083u
#define HV_X64_MSR_EOM 0x40000084u
#define HV_X64_MSR_SINT2 0x40000092u
#define HV_X64_MSR_HYPERCALL_ENABLE 0x0000000000000001ull
#define HV_X64_MSR_HYPERCALL_PAGE_MASK 0xFFFFFFFFFFFFF000ull
#define HV_HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS 0x40000000u
#define HV_HYPERV_CPUID_INTERFACE 0x40000001u
#define HV_HYPERV_CPUID_FEATURES 0x40000003u
#define HV_HYPERV_INTERFACE_HV1 0x31237648u
#define HYPERV_RIVERIX_VENDOR_ID 0x8300u

#define HYPERV_CPUID_FEAT_EAX_SYNIC 0x00000004u
#define HYPERV_CPUID_FEAT_EBX_POST_MESSAGES 0x00000010u
#define HYPERV_CPUID_FEAT_EBX_SIGNAL_EVENTS 0x00000020u

#define HV_SYNIC_CONTROL_ENABLE 0x0000000000000001ull
#define HV_SYNIC_SIMP_ENABLE 0x0000000000000001ull
#define HV_SYNIC_SINT_MASKED 0x0000000000010000ull
#define HV_SYNIC_SINT_VECTOR_MASK 0x00000000000000FFull
#define HYPERV_MESSAGE_VECTOR 0xF2u

#define HVCALL_POST_MESSAGE 0x005Cull
#define HVCALL_SIGNAL_EVENT 0x005Dull
#define HV_HYPERCALL_FAST_BIT 0x0000000000010000ull
#define HV_HYPERCALL_RESULT_MASK 0x000000000000FFFFull

#define HV_STATUS_SUCCESS 0x0u
#define HV_STATUS_INSUFFICIENT_MEMORY 0xBu
#define HV_STATUS_INVALID_CONNECTION_ID 0x12u
#define HV_STATUS_INSUFFICIENT_BUFFERS 0x13u

#define HV_MESSAGE_PAYLOAD_QWORD_COUNT 30u
#define HV_MESSAGE_PAYLOAD_BYTE_COUNT (HV_MESSAGE_PAYLOAD_QWORD_COUNT * sizeof(uint64_t))
#define HV_SYNIC_SINT_COUNT 16u

typedef struct {
    uint32_t message_type;
    uint8_t payload_size;
    uint8_t message_flags;
    uint8_t reserved[2];
    uint64_t sender;
} __attribute__((packed)) hyperv_message_header_t;

typedef struct {
    hyperv_message_header_t header;
    uint64_t payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
} __attribute__((packed)) hyperv_message_t;

typedef struct {
    hyperv_message_t sint_message[HV_SYNIC_SINT_COUNT];
} __attribute__((packed)) hyperv_message_page_t;

typedef struct {
    volatile uint32_t pending;
    volatile uint32_t armed;
} __attribute__((packed)) hyperv_monitor_trigger_group_t;

typedef struct {
    uint32_t trigger_state;
    uint32_t reserved0;
    hyperv_monitor_trigger_group_t trigger_group[4];
} __attribute__((packed)) hyperv_monitor_page_head_t;

typedef struct {
    uint32_t connection_id;
    uint32_t reserved;
    uint32_t message_type;
    uint32_t payload_size;
    uint64_t payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
} __attribute__((packed)) hyperv_input_post_message_t;

extern uint64_t hyperv_hypercall_asm(void *target,
                                     uint32_t control_low,
                                     uint32_t control_high,
                                     uint32_t input_low,
                                     uint32_t input_high,
                                     uint32_t output_low,
                                     uint32_t output_high);
extern uint64_t hyperv_fast_hypercall8_asm(void *target,
                                           uint32_t control_low,
                                           uint32_t control_high,
                                           uint32_t input_low,
                                           uint32_t input_high);

static uint32_t hyperv_max_leaf;
static uint32_t hyperv_interface_signature;
static uint32_t hyperv_features_eax;
static uint32_t hyperv_features_ebx;
static uint32_t hyperv_features_ecx;
static uint32_t hyperv_features_edx;
static uint32_t hyperv_hypercall_page_phys;
static void *hyperv_hypercall_page_virt;
static uint32_t hyperv_post_message_page_phys;
static hyperv_input_post_message_t *hyperv_post_message_page_virt;
static uint32_t hyperv_message_page_phys;
static hyperv_message_page_t *hyperv_message_page_virt;
static uint32_t hyperv_event_page_phys;
static void *hyperv_event_page_virt;
static uint32_t hyperv_monitor_page_phys[2];
static uint64_t hyperv_guest_id_value;
static int hyperv_guest_ready;
static int hyperv_synic_active;

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

static void zero_page_phys(uint32_t physical_address) {
    zero_bytes((void *)paging_phys_to_virt(physical_address), PAGE_SIZE);
}

static void compiler_barrier(void) {
    __asm__ volatile ("" : : : "memory");
}

static uint32_t hyperv_result(uint64_t status) {
    return (uint32_t)(status & HV_HYPERCALL_RESULT_MASK);
}

static int hyperv_allocate_page(uint32_t *physical_out, void **virtual_out) {
    uint32_t physical = palloc_alloc_page();

    if (physical == 0u) {
        return -1;
    }

    zero_page_phys(physical);
    *physical_out = physical;
    if (virtual_out != 0) {
        *virtual_out = (void *)paging_phys_to_virt(physical);
    }
    return 0;
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

    if (hyperv_allocate_page(&hyperv_hypercall_page_phys, &hyperv_hypercall_page_virt) != 0) {
        console_write("hyperv: hypercall page alloc failed\n");
        return -1;
    }

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

static int hyperv_setup_synic(void) {
    uint64_t simp;
    uint64_t siefp;
    uint64_t scontrol;
    uint64_t sint;

    if ((hyperv_features_eax & HYPERV_CPUID_FEAT_EAX_SYNIC) == 0u) {
        console_write("hyperv: synic unavailable\n");
        return -1;
    }

    if ((hyperv_features_ebx & HYPERV_CPUID_FEAT_EBX_POST_MESSAGES) == 0u ||
        (hyperv_features_ebx & HYPERV_CPUID_FEAT_EBX_SIGNAL_EVENTS) == 0u) {
        console_write("hyperv: vmbus messaging unavailable\n");
        return -1;
    }

    if (hyperv_allocate_page(&hyperv_message_page_phys, (void **)&hyperv_message_page_virt) != 0 ||
        hyperv_allocate_page(&hyperv_event_page_phys, &hyperv_event_page_virt) != 0 ||
        hyperv_allocate_page(&hyperv_post_message_page_phys, (void **)&hyperv_post_message_page_virt) != 0 ||
        hyperv_allocate_page(&hyperv_monitor_page_phys[0], 0) != 0 ||
        hyperv_allocate_page(&hyperv_monitor_page_phys[1], 0) != 0) {
        console_write("hyperv: synic page alloc failed\n");
        return -1;
    }

    simp = ((uint64_t)hyperv_message_page_phys & HV_X64_MSR_HYPERCALL_PAGE_MASK) | HV_SYNIC_SIMP_ENABLE;
    cpu_wrmsr(HV_X64_MSR_SIMP, simp);

    siefp = ((uint64_t)hyperv_event_page_phys & HV_X64_MSR_HYPERCALL_PAGE_MASK) | HV_SYNIC_SIMP_ENABLE;
    cpu_wrmsr(HV_X64_MSR_SIEFP, siefp);

    sint = (uint64_t)HYPERV_MESSAGE_VECTOR & HV_SYNIC_SINT_VECTOR_MASK;
    sint |= HV_SYNIC_SINT_MASKED;
    cpu_wrmsr(HV_X64_MSR_SINT2, sint);

    scontrol = cpu_rdmsr(HV_X64_MSR_SCONTROL);
    scontrol |= HV_SYNIC_CONTROL_ENABLE;
    cpu_wrmsr(HV_X64_MSR_SCONTROL, scontrol);

    hyperv_synic_active = 1;

    console_write("hyperv: synic msg page 0x");
    console_write_hex32(hyperv_message_page_phys);
    console_write(" event page 0x");
    console_write_hex32(hyperv_event_page_phys);
    console_write(" post page 0x");
    console_write_hex32(hyperv_post_message_page_phys);
    console_write(" sint 0x");
    console_write_hex32(HYPERV_MESSAGE_VECTOR);
    console_write(" masked\n");

    return 0;
}

static uint64_t hyperv_do_fast_hypercall8(uint16_t code, uint64_t input) {
    uint64_t control = (uint64_t)code | HV_HYPERCALL_FAST_BIT;

    if (!hyperv_guest_ready || hyperv_hypercall_page_virt == 0) {
        return ~0ull;
    }

    return hyperv_fast_hypercall8_asm(hyperv_hypercall_page_virt,
                                      (uint32_t)control,
                                      (uint32_t)(control >> 32),
                                      (uint32_t)input,
                                      (uint32_t)(input >> 32));
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
    (void)hyperv_setup_synic();

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

int hyperv_synic_ready(void) {
    return hyperv_synic_active;
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

int hyperv_post_message(uint32_t connection_id, uint32_t message_type, const void *payload, uint32_t payload_size) {
    uint64_t status;
    uint32_t result;

    if (!hyperv_synic_active || hyperv_post_message_page_virt == 0) {
        return -1;
    }

    if (payload_size > HV_MESSAGE_PAYLOAD_BYTE_COUNT) {
        return -1;
    }

    zero_bytes(hyperv_post_message_page_virt, sizeof(*hyperv_post_message_page_virt));
    hyperv_post_message_page_virt->connection_id = connection_id;
    hyperv_post_message_page_virt->message_type = message_type;
    hyperv_post_message_page_virt->payload_size = payload_size;

    if (payload != 0 && payload_size != 0u) {
        copy_bytes(hyperv_post_message_page_virt->payload, payload, payload_size);
    }

    status = hyperv_do_hypercall(HVCALL_POST_MESSAGE, hyperv_post_message_page_phys, 0u);
    result = hyperv_result(status);

    if (result == HV_STATUS_SUCCESS) {
        return 0;
    }

    if (result == HV_STATUS_INSUFFICIENT_MEMORY || result == HV_STATUS_INSUFFICIENT_BUFFERS) {
        return -2;
    }

    if (result == HV_STATUS_INVALID_CONNECTION_ID) {
        return -3;
    }

    return -1;
}

int hyperv_poll_message(uint32_t sint_index,
                        void *payload_buffer,
                        uint32_t payload_capacity,
                        uint32_t *message_type_out,
                        uint32_t *payload_size_out) {
    hyperv_message_t *message;
    uint32_t message_type;
    uint32_t payload_size;
    uint32_t pending;

    if (!hyperv_synic_active || hyperv_message_page_virt == 0 || sint_index >= HV_SYNIC_SINT_COUNT) {
        return 0;
    }

    message = &hyperv_message_page_virt->sint_message[sint_index];
    message_type = message->header.message_type;
    if (message_type == 0u) {
        return 0;
    }

    payload_size = message->header.payload_size;
    if (message_type_out != 0) {
        *message_type_out = message_type;
    }
    if (payload_size_out != 0) {
        *payload_size_out = payload_size;
    }

    if (payload_buffer != 0 && payload_capacity != 0u) {
        uint32_t copy_length = payload_size;
        if (copy_length > payload_capacity) {
            copy_length = payload_capacity;
        }
        copy_bytes(payload_buffer, message->payload, copy_length);
    }

    pending = (uint32_t)(message->header.message_flags & 0x1u);
    compiler_barrier();
    message->header.message_type = 0u;
    compiler_barrier();

    if (pending != 0u) {
        cpu_wrmsr(HV_X64_MSR_EOM, 0u);
    }

    return 1;
}

int hyperv_signal_event(uint32_t connection_id) {
    uint64_t status = hyperv_do_fast_hypercall8((uint16_t)HVCALL_SIGNAL_EVENT, (uint64_t)connection_id);

    return hyperv_result(status) == HV_STATUS_SUCCESS ? 0 : -1;
}

uint32_t hyperv_monitor_page(uint32_t page_index) {
    if (page_index >= 2u) {
        return 0u;
    }

    return hyperv_monitor_page_phys[page_index];
}

void hyperv_set_monitor_pending(uint32_t page_index, uint32_t monitor_id) {
    hyperv_monitor_page_head_t *page;
    uint32_t group_index;
    uint32_t bit_mask;

    if (page_index >= 2u || monitor_id >= 128u) {
        return;
    }

    page = (hyperv_monitor_page_head_t *)paging_phys_to_virt(hyperv_monitor_page_phys[page_index]);
    if (page == 0) {
        return;
    }

    group_index = monitor_id / 32u;
    bit_mask = 1u << (monitor_id % 32u);
    page->trigger_group[group_index].pending |= bit_mask;
    compiler_barrier();
}
