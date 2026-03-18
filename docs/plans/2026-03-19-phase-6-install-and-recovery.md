# Phase 6 Install And Recovery Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Turn the current disk-image flow into a real install/recovery story with documented layout, reusable host-side image installation, and an explicit recovery rootfs boot path.

**Architecture:** Keep the current installed-system shape: GPT disk, EFI System Partition, rootfs partition, GRUB, and a disk-backed rootfs. Add a boot policy knob so the kernel can explicitly choose disk or ramdisk rootfs, then build host-side tooling around that contract so normal and recovery images are assembled from the same artifacts instead of ad hoc Makefile command blocks.

**Tech Stack:** freestanding C kernel, GRUB Multiboot, QEMU/OVMF, GPT, FAT32 ESP, simplefs, bash host tooling, WSL-friendly user-space disk-image assembly

---

### Task 1: Add explicit rootfs boot policy

**Files:**
- Modify: `src/kernel/vfs.c`
- Modify: `grub/grub.cfg`
- Modify: `grub/grub-disk.cfg`
- Create: `grub/grub-disk-recovery.cfg`

**Implementation:**
- Parse the Multiboot kernel command line for `root=disk`, `root=ramdisk`, or no explicit root policy.
- Make VFS honor that policy instead of always preferring disk.
- Pass `root=ramdisk` on the ISO path.
- Pass `root=disk` on the normal installed disk path.
- Add a recovery GRUB config that boots with `root=ramdisk` and loads `/boot/rootfs.img`.

**Verification:**
- ISO boot still mounts the ramdisk rootfs.
- Normal disk boot mounts the disk rootfs and does not rely on the ramdisk module.
- Recovery boot mounts the ramdisk rootfs even when a disk is present.

### Task 2: Replace the fixed Makefile disk recipe with reusable image-install tooling

**Files:**
- Create: `tools/install_disk_image.sh`
- Modify: `Makefile`

**Implementation:**
- Move GPT/ESP/rootfs image assembly out of the inline Makefile recipe into a dedicated host-side script.
- Copy `/boot/rootfs.img` into the ESP so recovery boot has a known-good ramdisk payload.
- Use the script for the existing normal disk image target.
- Add a recovery disk image target and a configurable install target for arbitrary output image paths.

**Verification:**
- `make disk-image` still produces the current bootable installed image.
- `make install-image OUTPUT=...` produces the same layout at a caller-selected path.
- `make recovery-disk-image` produces a disk that boots the recovery path by default.

### Task 3: Document the installed layout and usage

**Files:**
- Create: `docs/disk-layout.md`
- Modify: `README.md`

**Implementation:**
- Document the GPT layout, partition purposes, installed ESP files, rootfs partition label, and recovery path.
- Document WSL/Linux host usage for normal, recovery, and arbitrary-output image installation.
- Update README milestones to reflect the new Phase 6 baseline.

**Verification:**
- The documented commands match the Makefile targets exactly.

### Task 4: Add automated recovery verification

**Files:**
- Modify: `Makefile`

**Implementation:**
- Add `run-disk-recovery` and `check-disk-recovery`.
- Verify recovery boots with `root=ramdisk`, mounts the ramdisk rootfs, runs the existing selftest plus Phase 5 shell bootstrap, and reaches the interactive shell prompt.

**Verification:**
- `make check`
- `make check-disk`
- `make check-disk-recovery`
- `make check-disk-persist`
