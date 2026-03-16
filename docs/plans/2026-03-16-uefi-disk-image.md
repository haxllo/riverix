# UEFI Disk Image Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a `make disk-image` pipeline that builds a bootable GPT/EFI raw disk image for the current kernel and rootfs, and verify it boots in QEMU under OVMF.

**Architecture:** Keep Phase 1 as packaging, not storage-driver work. Build the existing `kernel.elf` and `rootfs.img`, generate a standalone `BOOTX64.EFI` that chains into the on-disk GRUB config, create a FAT32 EFI System Partition image in user space, and place that ESP into a GPT raw disk image. This proves installation media layout without yet replacing the Multiboot ramdisk or requiring root-only loop-mount steps.

**Tech Stack:** GRUB x86_64 UEFI, GPT raw disk image, FAT32 ESP, QEMU/OVMF, WSL-friendly host tools (`grub-mkstandalone`, `sgdisk`, `mkfs.vfat`, `mtools`, `dd`).

---

### Task 1: Add the disk-image boot assets

**Files:**
- Create: `grub/grub-efi-early.cfg`
- Modify: `grub/grub.cfg`

**Step 1: Add embedded early EFI GRUB config**

Create a tiny early config that enables serial output, searches the FAT partition for `/boot/grub/grub.cfg`, sets `root`, and chains into the normal GRUB config.

**Step 2: Keep the normal GRUB config disk-friendly**

Continue loading `/boot/kernel.elf` and `/boot/rootfs.img` through the regular GRUB config so the same boot payload works for ISO and disk image packaging.

### Task 2: Add a WSL-friendly ESP image pipeline

**Files:**
- Modify: `Makefile`

**Step 1: Build standalone `BOOTX64.EFI`**

Use `grub-mkstandalone` to generate an EFI executable with the early config embedded.

**Step 2: Build a FAT32 ESP image**

Create a 32 MiB FAT image as a normal file, format it with `mkfs.vfat`, and populate it with `mmd`/`mcopy`.

**Step 3: Assemble a GPT raw disk image**

Create a raw disk file, partition it with `sgdisk`, and copy the ESP image into the EFI partition offset with `dd`.

### Task 3: Add disk boot targets and verification

**Files:**
- Modify: `Makefile`

**Step 1: Add `disk-image`, `run-disk`, and `check-disk`**

Create top-level targets for building the raw disk image, booting it in QEMU, and verifying the same runtime markers currently used for the ISO path.

**Step 2: Keep the check serial-driven**

Capture serial logs so `check-disk` can prove the machine booted from the disk image and reached the existing kernel, VFS, exec, and scheduler milestones.

### Task 4: Update docs

**Files:**
- Modify: `README.md`

**Step 1: Document the new artifact**

Describe the raw GPT/EFI image output, note that it is a bootable packaging step rather than a persistent disk-mounted rootfs, and explain that replacing the Multiboot ramdisk with a real disk-backed root mount remains a later phase.
