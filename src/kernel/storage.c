#include "kernel/storage.h"

#include <stdint.h>

#include "kernel/ahci.h"
#include "kernel/ata.h"
#include "kernel/console.h"
#include "kernel/pci.h"
#include "kernel/trace.h"

static block_device_t *boot_disk;
static const char *boot_partition_name;
static uint32_t storage_initialized;

int32_t storage_init(void) {
    if (storage_initialized != 0u) {
        return boot_disk != 0 ? 0 : -1;
    }

    storage_initialized = 1u;
    boot_disk = 0;
    boot_partition_name = 0;

    pci_init();

    if (ahci_init() == 0) {
        boot_disk = block_find("ahci0");
        if (boot_disk != 0) {
            boot_partition_name = "ahci0p2";
        }
    }

    if (boot_disk == 0 && ata_init() == 0) {
        boot_disk = block_find("ata0");
        if (boot_disk != 0) {
            boot_partition_name = "ata0p2";
        }
    }

    if (boot_disk == 0 || boot_partition_name == 0) {
        console_write("storage: no disk controller ready\n");
        return -1;
    }

    console_write("storage: boot disk ");
    console_write(boot_disk->name);
    console_write("\n");
    trace_log(SYS_TRACE_CATEGORY_BLOCK,
              SYS_TRACE_EVENT_BLOCK_STORAGE,
              boot_disk->controller != 0 ? boot_disk->controller->transport : 0u,
              boot_disk->block_count,
              0u);
    return 0;
}

block_device_t *storage_boot_disk(void) {
    return boot_disk;
}

const char *storage_boot_partition_name(void) {
    return boot_partition_name;
}
