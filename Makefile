CC := gcc
HOSTCC := gcc
LD := ld
GRUB_MKRESCUE := grub-mkrescue
GRUB_MKSTANDALONE := grub-mkstandalone
QEMU := qemu-system-x86_64
OVMF_CODE := /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS_TEMPLATE := /usr/share/OVMF/OVMF_VARS_4M.fd
MKFS_VFAT := mkfs.vfat
SGDISK := sgdisk
MMD := mmd
MCOPY := mcopy

BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/isodir
KERNEL := $(BUILD_DIR)/kernel.elf
ISO := $(BUILD_DIR)/riverix.iso
LOG := $(BUILD_DIR)/qemu.log
DISK_LOG := $(BUILD_DIR)/qemu-disk.log
DISK_PERSIST_LOG1 := $(BUILD_DIR)/qemu-disk-pass1.log
DISK_PERSIST_LOG2 := $(BUILD_DIR)/qemu-disk-pass2.log
OVMF_VARS := $(BUILD_DIR)/OVMF_VARS_4M.fd
USER_LD_SCRIPT := src/user/init.ld
USER_PROGRAMS := init child fault
USER_ELFS := $(addprefix $(BUILD_DIR)/user_,$(addsuffix .elf,$(USER_PROGRAMS)))
ROOTFS_BIN_ITEMS := $(foreach program,$(USER_PROGRAMS),$(BUILD_DIR)/user_$(program).elf /bin/$(program))
ROOTFS_IMG := $(BUILD_DIR)/rootfs.img
MKFS_ROOTFS := $(BUILD_DIR)/mkfs_rootfs
GRUB_EFI := $(BUILD_DIR)/BOOTX64.EFI
DISK_IMAGE := $(BUILD_DIR)/riverix-disk.img
ESP_SIZE_KIB := 65536
ESP_START_SECTOR := 2048
ESP_OFFSET_SECTORS := 2048
ESP_OFFSET_BYTES := 1048576
ROOTFS_START_SECTOR := 133120
ROOTFS_SIZE_SECTORS := 32768
GRUB_EFI_MODULES := part_gpt fat normal multiboot search search_fs_file configfile serial terminal

CFLAGS := -m32 -std=gnu11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -Werror -Iinclude
ASFLAGS := -m32 -Iinclude
LDFLAGS := -m elf_i386 -T linker.ld -z noexecstack
USER_LDFLAGS := -m elf_i386 -T $(USER_LD_SCRIPT) -nostdlib -z noexecstack
HOSTCFLAGS := -std=gnu11 -Wall -Wextra -Werror -Iinclude

OBJS := \
	$(BUILD_DIR)/boot.o \
	$(BUILD_DIR)/block.o \
	$(BUILD_DIR)/ata.o \
	$(BUILD_DIR)/kernel.o \
	$(BUILD_DIR)/console.o \
	$(BUILD_DIR)/exec.o \
	$(BUILD_DIR)/gdt.o \
	$(BUILD_DIR)/idt.o \
		$(BUILD_DIR)/interrupts.o \
		$(BUILD_DIR)/kheap.o \
		$(BUILD_DIR)/kstack.o \
		$(BUILD_DIR)/memory.o \
	$(BUILD_DIR)/pic.o \
	$(BUILD_DIR)/palloc.o \
	$(BUILD_DIR)/paging.o \
	$(BUILD_DIR)/pit.o \
	$(BUILD_DIR)/partition.o \
	$(BUILD_DIR)/proc.o \
	$(BUILD_DIR)/ramdisk.o \
	$(BUILD_DIR)/serial.o \
	$(BUILD_DIR)/simplefs.o \
	$(BUILD_DIR)/syscall.o \
	$(BUILD_DIR)/usercopy.o \
	$(BUILD_DIR)/vfs.o \
	$(BUILD_DIR)/vga.o

.PHONY: all clean run check disk-image run-disk check-disk check-disk-persist

all: $(ISO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/boot.o: src/boot/boot.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.o: src/kernel/kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/console.o: src/kernel/console.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/block.o: src/kernel/block.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ata.o: src/kernel/ata.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/exec.o: src/kernel/exec.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: src/kernel/gdt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: src/kernel/idt.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts.o: src/boot/interrupts.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/kheap.o: src/kernel/kheap.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kstack.o: src/kernel/kstack.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/memory.o: src/kernel/memory.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pic.o: src/kernel/pic.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/palloc.o: src/kernel/palloc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paging.o: src/kernel/paging.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pit.o: src/kernel/pit.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/partition.o: src/kernel/partition.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/proc.o: src/kernel/proc.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: src/kernel/serial.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/simplefs.o: src/kernel/simplefs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall.o: src/kernel/syscall.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/usercopy.o: src/kernel/usercopy.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs.o: src/kernel/vfs.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vga.o: src/kernel/vga.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ramdisk.o: src/kernel/ramdisk.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_%.o: src/user/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_%.elf: $(BUILD_DIR)/user_%.o $(USER_LD_SCRIPT) | $(BUILD_DIR)
	$(LD) $(USER_LDFLAGS) -o $@ $<

$(MKFS_ROOTFS): tools/mkfs_rootfs.c include/shared/simplefs_format.h | $(BUILD_DIR)
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@

$(ROOTFS_IMG): $(USER_ELFS) $(MKFS_ROOTFS) | $(BUILD_DIR)
	$(MKFS_ROOTFS) $@ $(ROOTFS_SIZE_SECTORS) $(ROOTFS_BIN_ITEMS)

$(GRUB_EFI): grub/grub-efi-early.cfg | $(BUILD_DIR)
	$(GRUB_MKSTANDALONE) -O x86_64-efi -o $@ --modules="$(GRUB_EFI_MODULES)" --install-modules="$(GRUB_EFI_MODULES)" "boot/grub/grub.cfg=grub/grub-efi-early.cfg" >/dev/null

$(DISK_IMAGE): $(GRUB_EFI) $(KERNEL) $(ROOTFS_IMG) grub/grub-disk.cfg | $(BUILD_DIR)
	rm -f $@
	truncate -s 128M $@
	$(SGDISK) -o -n 1:$(ESP_START_SECTOR):+64M -t 1:ef00 -c 1:"EFI System" -n 2:$(ROOTFS_START_SECTOR):+16M -t 2:8300 -c 2:"riverix-rootfs" $@ >/dev/null
	$(MKFS_VFAT) -F 32 -n RIVERIX --offset=$(ESP_START_SECTOR) -h $(ESP_START_SECTOR) $@ $(ESP_SIZE_KIB) >/dev/null
	test $$(stat -c %s $(ROOTFS_IMG)) -le $$(( $(ROOTFS_SIZE_SECTORS) * 512 ))
	dd if=$(ROOTFS_IMG) of=$@ bs=512 seek=$(ROOTFS_START_SECTOR) conv=notrunc status=none
	$(MMD) -i $@@@$(ESP_OFFSET_BYTES) ::/EFI
	$(MMD) -i $@@@$(ESP_OFFSET_BYTES) ::/EFI/BOOT
	$(MMD) -i $@@@$(ESP_OFFSET_BYTES) ::/boot
	$(MMD) -i $@@@$(ESP_OFFSET_BYTES) ::/boot/grub
	$(MCOPY) -i $@@@$(ESP_OFFSET_BYTES) $(GRUB_EFI) ::/EFI/BOOT/BOOTX64.EFI
	$(MCOPY) -i $@@@$(ESP_OFFSET_BYTES) $(KERNEL) ::/boot/kernel.elf
	$(MCOPY) -i $@@@$(ESP_OFFSET_BYTES) grub/grub-disk.cfg ::/boot/grub/grub.cfg

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(ISO_DIR)/boot/grub:
	mkdir -p $(ISO_DIR)/boot/grub

$(ISO_DIR)/boot/kernel.elf: $(KERNEL) | $(ISO_DIR)/boot/grub
	cp $(KERNEL) $@

$(ISO_DIR)/boot/rootfs.img: $(ROOTFS_IMG) | $(ISO_DIR)/boot/grub
	cp $(ROOTFS_IMG) $@

$(ISO_DIR)/boot/grub/grub.cfg: grub/grub.cfg | $(ISO_DIR)/boot/grub
	cp $< $@

$(ISO): $(ISO_DIR)/boot/kernel.elf $(ISO_DIR)/boot/rootfs.img $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR) >/dev/null 2>&1

$(OVMF_VARS): | $(BUILD_DIR)
	cp $(OVMF_VARS_TEMPLATE) $(OVMF_VARS)

run: $(ISO) $(OVMF_VARS)
	$(QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(OVMF_VARS) -cdrom $(ISO) -serial stdio -monitor none -display none -no-reboot -no-shutdown

disk-image: $(DISK_IMAGE)

run-disk: $(DISK_IMAGE) | $(BUILD_DIR)
	cp $(OVMF_VARS_TEMPLATE) $(OVMF_VARS)
	$(QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(OVMF_VARS) -drive if=ide,format=raw,file=$(DISK_IMAGE) -serial stdio -monitor none -display none -no-reboot -no-shutdown

check: $(ISO) $(OVMF_VARS)
	rm -f $(LOG)
	cp $(OVMF_VARS_TEMPLATE) $(OVMF_VARS)
	timeout 20s $(QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(OVMF_VARS) -cdrom $(ISO) -serial file:$(LOG) -monitor none -display none -no-reboot -no-shutdown >/dev/null 2>&1 || true
	grep -q "riverix: kernel_main reached" $(LOG)
	grep -q "memory: map complete" $(LOG)
	grep -q "palloc: free pages" $(LOG)
	grep -q "paging: enabled" $(LOG)
	grep -q "paging: high alias flags" $(LOG)
	grep -q "kheap: ready base 0x" $(LOG)
	grep -q "kheap: selftest ok" $(LOG)
	grep -q "kstack: ready slots 0x" $(LOG)
	grep -q "kstack: selftest ok" $(LOG)
	grep -q "idt: loaded" $(LOG)
	grep -q "ramdisk: rootfs module bytes 0x" $(LOG)
	grep -q "block: registered rootfs0 blocks 0x" $(LOG)
	grep -q "simplefs: mounted rootfs0 inodes 0x" $(LOG)
	grep -q "vfs: rootfs mounted from ramdisk" $(LOG)
	grep -q "storage: writable smoke skipped" $(LOG)
	grep -q "vfs: ready" $(LOG)
	grep -q "exec: loaded /bin/init entry 0x" $(LOG)
	grep -q "sched: ready" $(LOG)
	grep -q "task: init as 0x" $(LOG)
	grep -q "task: worker-a online" $(LOG)
	grep -q "task: worker-b online" $(LOG)
	grep -q "init: online" $(LOG)
	grep -q "proc: fork child pid 0x" $(LOG)
	grep -q "exec: loaded /bin/child entry 0x" $(LOG)
	grep -q "init: child pid 0x" $(LOG)
	grep -q "child: online" $(LOG)
	grep -q "init: child exit 0x0000002A" $(LOG)
	grep -q "exec: loaded /bin/fault entry 0x" $(LOG)
	grep -q "fault: trigger" $(LOG)
	grep -q "trap: user pid 0x" $(LOG)
	grep -q "init: fault exit 0x0000008E" $(LOG)
	grep -q "paging: cow split va 0x" $(LOG)
	grep -q "init: cow child 0x22222222" $(LOG)
	grep -q "init: cow exit 0x00000033" $(LOG)
	grep -q "init: cow parent 0x11111111" $(LOG)
	grep -q "init: churn ok" $(LOG)
	grep -q "pid 0x" $(LOG)
	grep -q " work 0x" $(LOG)
	grep -q "sched: switch" $(LOG)
	grep -q "pit: tick" $(LOG)
	@printf 'check passed: %s\n' "$(LOG)"

check-disk: | $(BUILD_DIR)
	rm -f $(DISK_IMAGE)
	$(MAKE) $(DISK_IMAGE)
	rm -f $(DISK_LOG)
	cp $(OVMF_VARS_TEMPLATE) $(OVMF_VARS)
	timeout 20s $(QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(OVMF_VARS) -drive if=ide,format=raw,file=$(DISK_IMAGE) -serial file:$(DISK_LOG) -monitor none -display none -no-reboot -no-shutdown >/dev/null 2>&1 || true
	grep -q "riverix: kernel_main reached" $(DISK_LOG)
	grep -q "memory: map complete" $(DISK_LOG)
	grep -q "kheap: ready base 0x" $(DISK_LOG)
	grep -q "kheap: selftest ok" $(DISK_LOG)
	grep -q "kstack: ready slots 0x" $(DISK_LOG)
	grep -q "kstack: selftest ok" $(DISK_LOG)
	grep -q "block: registered ata0 blocks 0x" $(DISK_LOG)
	grep -q "ata: detected ata0 sectors 0x" $(DISK_LOG)
	grep -q "block: registered ata0p2 blocks 0x" $(DISK_LOG)
	grep -q "partition: registered ata0p2 start 0x" $(DISK_LOG)
	grep -q "simplefs: mounted ata0p2 inodes 0x" $(DISK_LOG)
	grep -q "vfs: rootfs mounted from disk" $(DISK_LOG)
	grep -q "storage: bootcount 0x00000001" $(DISK_LOG)
	grep -q "exec: loaded /bin/init entry 0x" $(DISK_LOG)
	grep -q "task: init as 0x" $(DISK_LOG)
	grep -q "init: online" $(DISK_LOG)
	grep -q "proc: fork child pid 0x" $(DISK_LOG)
	grep -q "exec: loaded /bin/child entry 0x" $(DISK_LOG)
	grep -q "init: child pid 0x" $(DISK_LOG)
	grep -q "child: online" $(DISK_LOG)
	grep -q "init: child exit 0x0000002A" $(DISK_LOG)
	grep -q "exec: loaded /bin/fault entry 0x" $(DISK_LOG)
	grep -q "fault: trigger" $(DISK_LOG)
	grep -q "trap: user pid 0x" $(DISK_LOG)
	grep -q "init: fault exit 0x0000008E" $(DISK_LOG)
	grep -q "paging: cow split va 0x" $(DISK_LOG)
	grep -q "init: cow child 0x22222222" $(DISK_LOG)
	grep -q "init: cow exit 0x00000033" $(DISK_LOG)
	grep -q "init: cow parent 0x11111111" $(DISK_LOG)
	grep -q "init: churn ok" $(DISK_LOG)
	grep -q "pit: tick" $(DISK_LOG)
	! grep -q "ramdisk: rootfs module bytes 0x" $(DISK_LOG)
	@printf 'check passed: %s\n' "$(DISK_LOG)"

check-disk-persist: | $(BUILD_DIR)
	rm -f $(DISK_IMAGE)
	$(MAKE) $(DISK_IMAGE)
	rm -f $(DISK_PERSIST_LOG1) $(DISK_PERSIST_LOG2)
	cp $(OVMF_VARS_TEMPLATE) $(OVMF_VARS)
	timeout 20s $(QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(OVMF_VARS) -drive if=ide,format=raw,file=$(DISK_IMAGE) -serial file:$(DISK_PERSIST_LOG1) -monitor none -display none -no-reboot -no-shutdown >/dev/null 2>&1 || true
	cp $(OVMF_VARS_TEMPLATE) $(OVMF_VARS)
	timeout 20s $(QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,file=$(OVMF_VARS) -drive if=ide,format=raw,file=$(DISK_IMAGE) -serial file:$(DISK_PERSIST_LOG2) -monitor none -display none -no-reboot -no-shutdown >/dev/null 2>&1 || true
	grep -q "vfs: rootfs mounted from disk" $(DISK_PERSIST_LOG1)
	grep -q "storage: bootcount 0x00000001" $(DISK_PERSIST_LOG1)
	grep -q "init: child exit 0x0000002A" $(DISK_PERSIST_LOG1)
	grep -q "init: fault exit 0x0000008E" $(DISK_PERSIST_LOG1)
	grep -q "init: cow exit 0x00000033" $(DISK_PERSIST_LOG1)
	grep -q "init: cow parent 0x11111111" $(DISK_PERSIST_LOG1)
	grep -q "init: churn ok" $(DISK_PERSIST_LOG1)
	grep -q "vfs: rootfs mounted from disk" $(DISK_PERSIST_LOG2)
	grep -q "storage: bootcount 0x00000002" $(DISK_PERSIST_LOG2)
	grep -q "init: child exit 0x0000002A" $(DISK_PERSIST_LOG2)
	grep -q "init: fault exit 0x0000008E" $(DISK_PERSIST_LOG2)
	grep -q "init: cow exit 0x00000033" $(DISK_PERSIST_LOG2)
	grep -q "init: cow parent 0x11111111" $(DISK_PERSIST_LOG2)
	grep -q "init: churn ok" $(DISK_PERSIST_LOG2)
	@printf 'check passed: %s %s\n' "$(DISK_PERSIST_LOG1)" "$(DISK_PERSIST_LOG2)"

clean:
	rm -rf $(BUILD_DIR)
