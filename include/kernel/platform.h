#ifndef RIVERIX_PLATFORM_H
#define RIVERIX_PLATFORM_H

typedef enum platform_kind {
    PLATFORM_KIND_GENERIC = 0,
    PLATFORM_KIND_HYPERV,
    PLATFORM_KIND_HYPERVISOR_OTHER,
} platform_kind_t;

void platform_init(void);
platform_kind_t platform_kind(void);
int platform_is_hyperv(void);
const char *platform_name(void);

#endif
