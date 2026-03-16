# Riverix System Roadmap Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Turn the current teaching kernel into a small installable Unix-derived operating system with a real process model, persistent storage, usable userland, and a disciplined architecture that can keep growing without frequent rewrites.

**Architecture:** Keep building vertically through stable interfaces instead of adding isolated demos. The kernel should grow in layers: boot and VM, block and filesystem, process and syscall model, userland, installability, then broader hardware and networking. Each phase should leave behind real interfaces and tests, not throwaway scaffolding, and each phase may be refined as we learn more from runtime behavior.

**Tech Stack:** freestanding C, x86, Multiboot, GRUB, QEMU, OVMF, GPT, simplefs, ATA PIO, WSL-hosted Unix toolchain

---

## Current Baseline

The repository already has:

- Multiboot boot under GRUB and QEMU
- serial and VGA output
- physical page allocator
- paging with shared kernel mappings and per-process user mappings
- GDT, IDT, PIC, PIT, timer-preempted scheduler
- kernel threads plus ring-3 user tasks
- a tiny syscall layer
- ELF loading for `/bin/init`
- a read-only `simplefs`
- ISO boot with ramdisk fallback
- GPT/EFI raw disk image boot with ATA-backed rootfs partition

That is enough to stop doing pure bring-up and start building a coherent operating system.

## Non-Goals For Now

Do not let these derail the main line yet:

- SMP
- USB
- GUI/windowing
- dynamic linking
- POSIX completeness
- 64-bit port
- network stack before local process/storage basics are solid

## Roadmap Principles

- Prefer one real path over multiple half-finished paths.
- Keep the ISO ramdisk path only as a regression/fallback path, not as the main architecture.
- Do not build a large userspace on top of a fake process or fake filesystem model.
- Make storage durable before chasing breadth in syscalls.
- Preserve WSL-friendly build flows wherever possible.
- Every phase needs a verification target and an exit criterion.

## Phase Order Summary

1. Writable local storage
2. Real process lifecycle
3. Better memory model
4. Kernel/user ABI growth
5. Real userland bootstrap
6. Install path and persistence tooling
7. Better storage hardware path
8. TTY, permissions, and Unix semantics
9. Networking
10. Robustness, observability, and long-term architecture cleanup

---

### Phase 1: Writable Local Storage

**Why now:** The project now boots from a real disk partition. The next highest-value step is making that storage writable and internally consistent.

**Primary deliverables:**

- add `simplefs` block allocation and inode updates
- add file create/remove/truncate/write paths
- add directory mutation
- add a minimal buffer cache or write-through discipline
- add a clean unmount/flush path for tests
- add host tools to build and inspect writable disk images

**Kernel files most likely involved:**

- `src/kernel/simplefs.c`
- `include/kernel/simplefs.h`
- `include/shared/simplefs_format.h`
- `src/kernel/vfs.c`
- `include/kernel/vfs.h`
- `tools/mkfs_rootfs.c`
- `Makefile`

**Verification gates:**

- create a file from the kernel or init and read it back
- reboot and confirm persistence on the disk image
- detect and reject obvious allocator corruption
- keep `make check` and `make check-disk` green

**Exit criterion:**

The kernel can create and modify files on the real rootfs partition and those changes survive reboot in QEMU.

**Likely follow-on decision:**

Decide whether `simplefs` remains the long-term format or only the learning filesystem. Keep the on-disk format conservative until process/userland work is further along.

### Phase 2: Real Process Lifecycle

**Why now:** A persistent filesystem is only truly useful if userspace can create, replace, and reap processes.

**Primary deliverables:**

- `exit`
- `wait` / `waitpid`
- user-visible `exec`
- `fork` with either full copy first or a minimal structured clone path
- per-process file descriptor tables with reference semantics
- initial process reaping and orphan handling

**Kernel files most likely involved:**

- `src/kernel/proc.c`
- `include/kernel/proc.h`
- `src/kernel/syscall.c`
- `include/kernel/syscall.h`
- `src/kernel/exec.c`
- `src/kernel/vfs.c`

**Verification gates:**

- init can spawn a child, wait for it, and observe an exit status
- `exec` replaces the image rather than acting like a second task-creation API
- file descriptor inheritance behaves predictably
- scheduler and timer path remain stable under repeated process churn

**Exit criterion:**

There is a minimal but real Unix-like process lifecycle: `fork`, `exec`, `exit`, and `wait`.

**Likely follow-on decision:**

After a correct first `fork`, decide whether to invest in copy-on-write immediately or defer until memory pressure justifies it.

### Phase 3: Better Memory Model

**Why now:** Once `fork` exists, the current memory model will become the main scaling limit.

**Primary deliverables:**

- kernel heap allocator
- stronger page-table management APIs
- user/kernel copy helpers generalized beyond `write`
- copy-on-write `fork` or at least a clear path toward it
- guard pages for kernel stacks
- page fault decoding and kill-on-fault policy for bad userspace accesses

**Kernel files most likely involved:**

- `src/kernel/paging.c`
- `include/kernel/paging.h`
- `src/kernel/palloc.c`
- `src/kernel/usercopy.c`
- `src/kernel/idt.c`
- `src/kernel/proc.c`

**Verification gates:**

- page faults are logged with actionable diagnostics
- invalid user pointers no longer panic the kernel
- repeated process creation does not leak page tables or stacks

**Exit criterion:**

The kernel has a sustainable memory-management base for multiple user processes and non-trivial syscall surfaces.

### Phase 4: Kernel/User ABI Growth

**Why now:** A process model without enough syscalls forces hacks in init and blocks userland growth.

**Primary deliverables:**

- `open`, `close`, `read`, `write`, `lseek`
- `mkdir`, `unlink`, `stat`
- `chdir` and per-process cwd
- `dup`/`dup2`
- time and sleep primitives
- stable syscall numbering and ABI documentation

**Kernel files most likely involved:**

- `src/kernel/syscall.c`
- `include/kernel/syscall.h`
- `src/kernel/vfs.c`
- `include/kernel/vfs.h`
- `src/kernel/simplefs.c`

**Verification gates:**

- userspace can navigate directories and manipulate files
- stdio redirection works
- syscall errors are returned consistently

**Exit criterion:**

Userspace can do basic shell-style filesystem work without kernel-only shortcuts.

### Phase 5: Real Userland Bootstrap

**Why now:** Once the syscall floor is useful, the project needs a real userland to expose gaps and drive architecture honestly.

**Primary deliverables:**

- tiny libc or syscall wrapper layer
- `/bin/sh` or an even smaller command interpreter
- core tools: `ls`, `cat`, `echo`, `mkdir`, `rm`, `ps`, `mount` subset as needed
- init script or static init sequence
- cross-build path for user programs

**Repo areas most likely involved:**

- `src/user/`
- `Makefile`
- `tools/`
- rootfs image generation

**Verification gates:**

- boot to init, then launch multiple user programs
- shell can inspect and mutate the filesystem
- userland rebuilds are reproducible from the host toolchain

**Exit criterion:**

The system can be used from a minimal shell instead of only from hardcoded test binaries.

### Phase 6: Install Path And Persistence Tooling

**Why now:** Once the system has writable disk-backed storage and a usable init/userland path, installation becomes meaningful rather than cosmetic.

**Primary deliverables:**

- stable disk layout document
- bootable raw image pipeline that represents the intended installed system
- rootfs population tooling
- optional installer flow for writing to another virtual disk
- recovery/debug boot path

**Repo areas most likely involved:**

- `Makefile`
- `grub/`
- `tools/`
- new docs under `docs/`

**Verification gates:**

- create a fresh disk image, boot it, modify files, reboot, observe persistence
- prove the installed image does not rely on the ramdisk fallback
- document how to test it from WSL and from a Linux host

**Exit criterion:**

There is a repeatable “build image -> boot -> persist changes -> reboot” install story.

### Phase 7: Better Storage Hardware Path

**Why now:** ATA PIO is fine for bring-up but weak as a long-term storage foundation.

**Primary deliverables:**

- refactor the block layer for cleaner controller/device separation
- add PCI discovery if needed
- add AHCI or another more modern controller path
- keep ATA PIO as fallback until the newer path is stable

**Kernel files most likely involved:**

- `src/kernel/block.c`
- `include/kernel/block.h`
- `src/kernel/ata.c`
- new PCI/AHCI modules

**Verification gates:**

- both old and new block paths can mount the same filesystem
- block I/O errors are surfaced cleanly
- disk boot remains automated in QEMU

**Exit criterion:**

The storage stack is no longer tied to a single narrow legacy disk path.

### Phase 8: TTY, Permissions, And Unix Semantics

**Why now:** A shell and writable filesystem will quickly expose the absence of real Unix process and file semantics.

**Primary deliverables:**

- session and controlling tty groundwork
- permission bits and basic uid/gid model
- `pipe`
- `dup2` if not already done
- pathname cleanup for `.` and `..`
- stronger `stat` model

**Verification gates:**

- shell pipelines work
- permission checks are enforced
- interactive console behavior is cleaner and less hardcoded

**Exit criterion:**

The system starts behaving like a recognizable small Unix rather than a kernel demo with files.

### Phase 9: Networking

**Why now:** Networking is valuable, but only after local process, storage, and shell flows are credible.

**Primary deliverables:**

- NIC bring-up in QEMU
- packet rx/tx path
- minimal socket model or a staged network API
- DHCP/static config and a diagnostic tool

**Exit criterion:**

The system can at least communicate over a virtual network in QEMU for testing and future package/tool transfer.

### Phase 10: Robustness, Observability, And Cleanup

**Why now:** Once the system is minimally usable, the main risk becomes entropy.

**Primary deliverables:**

- stronger panic and fault reporting
- memory, process, and block tracing hooks
- long-run soak tests
- codebase cleanup around duplicated helpers and hidden coupling
- architecture docs and subsystem maps

**Exit criterion:**

The system is small, understandable, and not one regression away from collapse.

---

## Cross-Cutting Workstreams

These should happen continuously instead of being postponed to the end:

- keep `make check` and `make check-disk` fast and authoritative
- grow host tooling only where it reduces kernel complexity
- document disk formats, syscall ABI, and boot contracts as they stabilize
- keep WSL-compatible build flows when practical
- avoid adding subsystems that bypass VFS, block, or process abstractions

## Suggested Immediate Execution Order

If we continue right now, the best order is:

1. Writable `simplefs`
2. `exit` and `wait`
3. User-visible `exec`
4. `fork`
5. File syscalls and cwd
6. Tiny shell and core userland tools
7. Better install/persistence tooling

That order keeps the critical path on storage and process reality instead of breadth.

## What Can Change Later

This roadmap is not a contract. The most likely changes on the move are:

- replacing `simplefs` after we learn its limitations
- moving copy-on-write earlier if `fork` pressure becomes painful
- moving networking later if storage or tty semantics stay rough
- moving AHCI earlier if ATA PIO becomes too constraining for tests
- trimming POSIX-like breadth in favor of a smaller cleaner teaching Unix

## Definition Of “First Real OS” For This Project

Treat the first real milestone as:

- boots from a disk image
- mounts a persistent writable rootfs from disk
- runs init and a shell in ring 3
- supports `fork`, `exec`, `exit`, and `wait`
- supports basic file and directory operations
- survives reboot with data intact

That is the point where `riverix` stops being mainly a kernel exercise and becomes a small operating system.
