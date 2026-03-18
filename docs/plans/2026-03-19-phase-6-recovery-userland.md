# Phase 6 Recovery Userland Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Turn recovery boot into a real operating mode by exposing boot-mode information to userspace and booting a dedicated recovery script and shell.

**Architecture:** Parse the Multiboot command line once in a shared kernel boot-info layer, use that layer both for rootfs selection and for a small boot-info syscall, then let `/bin/init` choose the recovery script based on those explicit boot flags instead of inferring mode from filesystem writability. Keep this slice small and honest: it builds a real recovery mode and first recovery-oriented tools without pretending the installer/repair path is finished.

**Tech Stack:** freestanding C kernel, x86 Multiboot, GRUB, tiny syscall ABI, simplefs rootfs image generation, C userland tools

---

### Task 1: Add shared kernel boot-info parsing

**Files:**
- Create: `include/kernel/bootinfo.h`
- Create: `src/kernel/bootinfo.c`
- Modify: `src/kernel/kernel.c`
- Modify: `src/kernel/vfs.c`
- Modify: `Makefile`

**Implementation:**
- Parse `root=disk`, `root=ramdisk`, and `recovery=1` exactly once from the Multiboot command line.
- Store boot root policy and flags in a dedicated kernel module.
- Initialize that module after paging but before VFS.
- Switch VFS to consume the shared boot-info root policy instead of reparsing the command line.
- Add a serial boot log marker so recovery mode is visible in logs.

**Verification:**
- `make check` still boots the ISO path and logs the expected ramdisk root policy.
- `make check-disk` still boots the disk path and logs the expected disk root policy.
- `make check-disk-recovery` logs recovery mode.

### Task 2: Expose boot mode and cwd to userspace

**Files:**
- Modify: `include/shared/syscall_abi.h`
- Modify: `include/kernel/syscall.h`
- Modify: `src/kernel/syscall.c`
- Modify: `include/kernel/proc.h`
- Modify: `src/kernel/proc.c`
- Modify: `include/user/unistd.h`
- Create: `include/user/boot.h`
- Modify: `src/user/libc/syscall.c`

**Implementation:**
- Add a small `SYS_BOOTINFO` syscall that returns root policy plus flags.
- Add a small `SYS_GETCWD` syscall so userland can print the current directory.
- Keep both syscalls copy-safe for ring-3 callers and directly callable from kernel tasks if needed.
- Keep the ABI minimal: one shared `sys_bootinfo_t` struct plus existing scalar return conventions.

**Verification:**
- The new userland helper binaries can read boot mode and cwd.
- Existing syscalls remain unchanged.

### Task 3: Make init recovery-aware

**Files:**
- Modify: `src/user/init.c`

**Implementation:**
- Query boot info early in `/bin/init`.
- If the recovery flag is set, run `/bin/sh /etc/rc-recovery`.
- Otherwise preserve the existing behavior: writable disk root -> `/etc/rc-disk`, read-only ramdisk root -> `/etc/rc-ro`.
- Emit explicit `init:` log markers for recovery mode.

**Verification:**
- Normal ISO boot still runs the read-only script.
- Normal disk boot still runs the writable disk script.
- Recovery disk boot runs the recovery script and still hands off to an interactive shell.

### Task 4: Add first recovery-oriented userland content

**Files:**
- Create: `src/user/bootmode.c`
- Create: `src/user/pwd.c`
- Modify: `Makefile`
- Create: `src/rootfs/etc/rc-recovery`
- Modify: `README.md`

**Implementation:**
- Add `/bin/bootmode` to print root policy and recovery flag.
- Add `/bin/pwd` using the new cwd syscall.
- Add `/etc/rc-recovery` with explicit recovery markers plus `bootmode`, `pwd`, `ls`, and `ps`.
- Package the new binaries and script into both ISO and disk rootfs images.
- Document recovery mode in the README.

**Verification:**
- `check-disk-recovery` sees the recovery script marker, bootmode output, and recovery completion marker.
- `check` and `check-disk` remain green.

### Task 5: Tighten automated verification

**Files:**
- Modify: `Makefile`

**Implementation:**
- Update `check-disk-recovery` to assert the recovery-specific `init` and shell-script markers.
- Leave `check` and `check-disk` on their existing non-recovery paths.

**Verification:**
- `make check`
- `make check-disk`
- `make check-disk-recovery`
