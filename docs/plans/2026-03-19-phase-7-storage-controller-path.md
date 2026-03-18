# Phase 7 Storage Controller Path Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the storage stack no longer depend on one narrow legacy ATA PIO path by adding controller/device separation, PCI discovery, and a second automated disk controller path in QEMU.

**Architecture:** Keep the existing block-device API at the top, but insert explicit controller ownership under it so transport drivers and partition devices are no longer conflated. Add a minimal PCI layer plus a small MMIO window so a PCI storage controller can be discovered and mapped safely during early boot, then bring up one modern controller path under QEMU while preserving ATA PIO as fallback.

**Tech Stack:** freestanding C kernel, x86 PCI config-space I/O, x86 paging/MMIO mapping, block layer, QEMU/OVMF verification

**Status:** Implemented on `main`. The repo now verifies both ATA PIO and AHCI disk boot paths against the same installed image and userspace stack.

---

### Task 1: Refactor block registration around controllers

**Files:**
- Modify: `include/kernel/block.h`
- Modify: `src/kernel/block.c`
- Modify: `src/kernel/ata.c`
- Modify: `src/kernel/partition.c`

**Steps:**
1. Add a `block_controller_t` type and a small controller registry.
2. Attach each `block_device_t` to an owning controller.
3. Register the legacy ATA controller separately from the ATA disk device.
4. Preserve the current block-device API so higher layers do not need to change yet.

**Verification:**
- Existing ATA boot still logs controller + device registration.

### Task 2: Add PCI discovery and MMIO mapping support

**Files:**
- Modify: `include/kernel/io.h`
- Modify: `include/kernel/paging.h`
- Modify: `src/kernel/paging.c`
- Create: `include/kernel/mmio.h`
- Create: `src/kernel/mmio.c`
- Create: `include/kernel/pci.h`
- Create: `src/kernel/pci.c`
- Modify: `Makefile`

**Steps:**
1. Add `inl`/`outl` helpers for PCI config-space access.
2. Reserve a small shared kernel MMIO window in paging.
3. Add a tiny one-way MMIO mapper for early boot controller BARs.
4. Add PCI config-space scanning for class-code lookup and BAR reads.

**Verification:**
- Kernel can log AHCI-class PCI discovery without breaking existing ATA boot.

### Task 3: Add the modern controller path

**Files:**
- Create: `include/kernel/ahci.h`
- Create: `src/kernel/ahci.c`
- Create: `include/kernel/storage.h`
- Create: `src/kernel/storage.c`
- Modify: `src/kernel/vfs.c`
- Modify: `src/kernel/kernel.c`

**Steps:**
1. Add a minimal AHCI driver for one SATA disk under QEMU:
   - controller discovery through PCI
   - ABAR MMIO mapping
   - one port rebased with one command slot
   - polled read/write path
2. Register an AHCI controller and disk block device.
3. Add a storage bring-up layer that prefers AHCI and falls back to ATA PIO.
4. Make VFS use the generic storage path instead of directly calling ATA.

**Verification:**
- Existing ATA boot still mounts disk rootfs.
- New AHCI boot mounts the same disk rootfs through a different controller path.

### Task 4: Add automated AHCI verification and docs

**Files:**
- Modify: `Makefile`
- Modify: `README.md`
- Modify: `docs/kernel-user-abi.md` only if needed
- Modify: `docs/disk-layout.md` only if needed

**Steps:**
1. Add `run-disk-ahci` and `check-disk-ahci` with a QEMU AHCI controller configuration.
2. Assert that AHCI logs appear and the disk-backed rootfs still boots the same userland path.
3. Keep the existing ATA targets intact as fallback coverage.
4. Update the repo docs to mark Phase 7 bring-up and the dual-controller verification matrix.

**Verification:**
- `make check`
- `make check-disk`
- `make check-disk-ahci`
- `make check-disk-recovery`
- `make check-disk-reinstall`
- `make check-disk-persist`
