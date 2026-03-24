# Post-Roadmap Sequencing Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Sequence Riverix from the completed baseline roadmap into a VM-first, desktop-capable operating system without forcing major rewrites of the current kernel, storage, and userland foundations.

**Architecture:** Split the next work into major tracks, but execute them in a strict dependency order. First widen platform visibility and console portability so the system is no longer QEMU-display-bound. In parallel at the planning level, lock the storage direction by replacing `simplefs` with an ext2-like Riverix filesystem, then harden storage semantics before widening service, security, and desktop layers. Keep one real path at a time and preserve compatibility until each replacement is verified.

**Tech Stack:** freestanding C, x86, GRUB/Multiboot, UEFI framebuffer handoff, VFS, block layer, AHCI/ATA, e1000, ring-3 userland, host image tooling, VM-first verification

---

## Immediate Priority Order

1. Platform portability and display foundation
2. Replacement filesystem design and migration plan
3. Replacement filesystem implementation
4. Storage hardening on top of the new filesystem
5. Network and IPC growth
6. Security, identity, and trusted system bootstrap
7. Userland and service-layer maturity
8. Desktop foundation
9. Production discipline and release engineering

## Why This Order

- Platform portability comes first because the core OS is real, but the current display and console path is still too narrow for broader VM work.
- The `simplefs` decision is now closed, so storage architecture must stop drifting and move toward a long-term replacement before more features accumulate on the wrong on-disk format.
- Storage hardening should happen before larger service or desktop work, because a desktop platform built on weak persistence semantics will force expensive rewrites later.
- Networking beyond diagnostics, authentication, service management, and a desktop stack all depend on the earlier storage and platform layers being stable.

### Task 1: Platform Portability And Display Foundation

**Files:**
- Modify: `README.md`
- Modify: `docs/riverix-major-roadmap.md`
- Modify: `include/kernel/multiboot.h`
- Modify: `src/kernel/kernel.c`
- Modify: `src/kernel/console.c`
- Modify: `src/kernel/vga.c`
- Create: `include/kernel/framebuffer.h`
- Create: `src/kernel/framebuffer.c`
- Create: `include/kernel/console_backend.h`
- Create: `src/kernel/console_backend.c`
- Modify: `Makefile`
- Modify: `grub/`

**Plan:**

1. Add a framebuffer-aware boot-info path so the kernel can consume graphics mode handoff from the bootloader.
2. Split console output into backends instead of hardwiring serial plus legacy VGA assumptions.
3. Add a first framebuffer text renderer with a clean built-in bitmap font and scrolling behavior.
4. Keep serial and VGA text as fallbacks.
5. Add VM checks that prove visible output on a UEFI graphics-backed VM path.

**Verification:**

- Existing `make check*` targets remain green.
- A new framebuffer-oriented boot check proves the kernel banner is visible without relying on legacy VGA text.
- Hyper-V-class or non-QEMU UEFI VM boot no longer fails due to missing console assumptions.

**Exit Criterion:**

Riverix can boot visibly through a framebuffer-backed console path and no longer depends on serial plus legacy VGA text as its only usable local console.

### Task 2: Replacement Filesystem Design And Migration Plan

**Files:**
- Modify: `docs/riverix-major-roadmap.md`
- Modify: `docs/disk-layout.md`
- Modify: `docs/subsystems.md`
- Create: `docs/plans/2026-03-24-riverfs-design.md`
- Create: `include/shared/riverfs_format.h`
- Create: `include/kernel/riverfs.h`
- Modify: `include/kernel/vfs.h`
- Modify: `src/kernel/vfs.c`
- Modify: `tools/mkfs_rootfs.c`

**Plan:**

1. Freeze `simplefs` as the compatibility filesystem and stop extending its on-disk feature set except for migration support.
2. Define a new Riverix filesystem with an ext2-like Unix model:
   - superblock
   - block groups or a simplified allocation domain
   - inode table
   - direct and indirect block addressing
   - scalable directories
   - explicit ownership and mode metadata
3. Define how boot, recovery, reinstall, and rootfs image tooling will recognize the new format.
4. Define the migration path:
   - dual-format mount support during transition
   - host-side mkfs for the new format
   - optional image conversion tooling
5. Keep the VFS contract stable so most userland and syscall code does not have to change.

**Verification:**

- Design doc covers on-disk format, mount semantics, migration, recovery expectations, and test strategy.
- Tooling plan is concrete enough that implementation can proceed without reopening basic format questions.

**Exit Criterion:**

The new filesystem format and migration strategy are fixed, documented, and ready for implementation.

### Task 3: Replacement Filesystem Implementation

**Files:**
- Create: `src/kernel/riverfs.c`
- Create: `include/kernel/riverfs.h`
- Create: `include/shared/riverfs_format.h`
- Modify: `src/kernel/vfs.c`
- Modify: `src/kernel/ramdisk.c`
- Modify: `src/kernel/partition.c`
- Modify: `src/kernel/syscall.c`
- Modify: `tools/mkfs_rootfs.c`
- Create: `tools/fsck_riverfs.c`
- Modify: `Makefile`
- Modify: `README.md`

**Plan:**

1. Implement read-only mount first for the new filesystem.
2. Add writable allocation, directory mutation, unlink, rename, and metadata updates.
3. Move rootfs image generation to the new filesystem.
4. Keep `simplefs` readable during transition until all disk and recovery paths are migrated.
5. Add an explicit fsck/inspection tool early instead of postponing repair tooling.

**Verification:**

- ISO, disk, recovery, reinstall, and persistence checks all run on the new filesystem.
- The kernel can still read old `simplefs` media during the transition if needed.
- The rootfs build path no longer depends on `simplefs` for normal operation.

**Exit Criterion:**

Riverix boots and persists on the new filesystem, and `simplefs` is no longer the active rootfs format.

### Task 4: Storage Hardening On Top Of The New Filesystem

**Files:**
- Modify: `src/kernel/riverfs.c`
- Modify: `src/kernel/block.c`
- Modify: `src/kernel/trace.c`
- Modify: `src/kernel/panic.c`
- Modify: `tools/fsck_riverfs.c`
- Modify: `README.md`

**Plan:**

1. Add a clear writeback model instead of ad hoc metadata flushes.
2. Add crash-consistency rules and replay/repair strategy appropriate to the chosen format.
3. Add corruption detection and offline repair coverage.
4. Add storage stress and fault-injection tests.

**Verification:**

- Reboot and persistence tests stay green.
- Simulated partial writes or corrupted metadata are detected and handled by tooling instead of silently mounting bad state.

**Exit Criterion:**

The new filesystem is durable enough to support larger userland and service growth without obvious integrity holes.

### Task 5: Network And IPC Growth

**Files:**
- Modify: `src/kernel/net.c`
- Modify: `src/kernel/syscall.c`
- Modify: `include/shared/syscall_abi.h`
- Create: `src/kernel/socket.c`
- Create: `include/kernel/socket.h`
- Create: `src/kernel/ipc.c`
- Create: `include/kernel/ipc.h`
- Modify: `src/user/`

**Plan:**

1. Add UDP first, then TCP.
2. Add a real sockets API instead of staged one-off network syscalls.
3. Define a local IPC model for service processes and desktop daemons.
4. Add DHCP and DNS after the lower transport path is stable.

**Exit Criterion:**

Riverix can run real networked and local service processes instead of only diagnostics.

### Task 6: Security, Identity, And Trusted Bootstrap

**Files:**
- Modify: `src/kernel/proc.c`
- Modify: `src/kernel/vfs.c`
- Modify: `src/kernel/syscall.c`
- Create: `src/user/login.c`
- Create: `src/user/passwd.c`
- Modify: `src/user/init.c`
- Modify: `src/rootfs/etc/`

**Plan:**

1. Grow the users/groups model beyond the current basic uid/gid split.
2. Add login/authentication and privileged service structure.
3. Define trusted update/install semantics.
4. Add a longer-term sandboxing direction for later desktop applications.

**Exit Criterion:**

Riverix can support real user sessions and trusted system startup rather than a shell-only single-user model.

### Task 7: Userland And Service-Layer Maturity

**Files:**
- Modify: `src/user/`
- Modify: `src/rootfs/etc/`
- Modify: `Makefile`
- Modify: `README.md`

**Plan:**

1. Strengthen the shell and core tools.
2. Add missing daily-use utilities like copy, move, editor, inspect, and system-control tools.
3. Add a structured service/startup model above init scripts.
4. Add update/install userland tools once the storage and trust model are ready.

**Exit Criterion:**

Riverix is usable as a deeper Unix environment, not only as a kernel exercise with a thin shell.

### Task 8: Desktop Foundation

**Files:**
- Create: `src/kernel/input/`
- Create: `src/kernel/display/`
- Create: `src/user/session/`
- Modify: `docs/riverix-major-roadmap.md`

**Plan:**

1. Build framebuffer-backed display primitives and input delivery into a session model.
2. Add a compositor/window-server direction.
3. Add the first desktop shell layer only after storage, security, and services are real.

**Exit Criterion:**

Riverix has the beginning of a true desktop platform instead of only a shell environment.

### Task 9: Production Discipline And Release Engineering

**Files:**
- Modify: `README.md`
- Create: `docs/releases/`
- Create: `docs/ops/`
- Modify: `Makefile`
- Modify: CI or verification scripts if introduced later

**Plan:**

1. Define release criteria and compatibility policy.
2. Expand automated stress and regression coverage.
3. Add reproducible image and release tooling.
4. Add documentation for install, upgrade, recovery, and diagnostics.

**Exit Criterion:**

Riverix can be released, tested, repaired, and evolved like a serious operating system project instead of an internal build artifact.
