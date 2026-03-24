# Riverix Subsystem Map

This document is the quick architectural map for the current Phase 10 `main`
branch. It points at the files that define each subsystem boundary so the kernel
stays readable as it grows.

## Boot And CPU Bring-Up

- `src/boot/boot.S`
- `src/kernel/kernel.c`
- `src/kernel/bootinfo.c`
- `src/kernel/framebuffer.c`
- `src/kernel/gdt.c`
- `src/kernel/idt.c`
- `src/kernel/pic.c`
- `src/kernel/pit.c`
- `src/kernel/panic.c`
- `src/kernel/trace.c`

Responsibilities:

- enter the kernel from Multiboot
- initialize console, framebuffer handoff, paging-adjacent early services, descriptor tables, and timer IRQs
- parse boot policy flags such as `root=disk`, `recovery=1`, `reinstall=1`, and `soak=1`
- centralize fatal kernel halts through `panic()`
- record structured trace events early enough that later subsystems can explain what happened

## Memory And VM

- `src/kernel/memory.c`
- `src/kernel/palloc.c`
- `src/kernel/paging.c`
- `src/kernel/kheap.c`
- `src/kernel/kstack.c`
- `src/kernel/usercopy.c`

Responsibilities:

- report Multiboot memory layout
- manage physical frames and frame refcounts
- own page-directory and page-table operations
- provide shared kernel mappings, per-process user mappings, and copy-on-write support
- provide heap allocation and guarded kernel stacks
- validate and copy buffers across kernel/user boundaries

## Process, Scheduling, And Syscalls

- `src/kernel/proc.c`
- `src/kernel/syscall.c`
- `src/kernel/exec.c`
- `src/boot/interrupts.S`

Responsibilities:

- task table, scheduler, and blocked-task wakeup paths
- user process lifecycle: `fork`, `exec`, `exit`, `waitpid`
- fd tables, cwd, uid/gid, session and tty metadata
- staged syscall dispatch and user return-frame handling
- ELF loading and initial user stack setup

## Storage And Block Stack

- `src/kernel/block.c`
- `src/kernel/storage.c`
- `src/kernel/pci.c`
- `src/kernel/mmio.c`
- `src/kernel/ata.c`
- `src/kernel/ahci.c`
- `src/kernel/partition.c`
- `src/kernel/ramdisk.c`

Responsibilities:

- controller/device registration and transport tagging
- choose the boot disk path
- PCI config-space and MMIO discovery for controller bring-up
- ATA PIO fallback path
- AHCI path under QEMU
- GPT partition discovery
- ramdisk registration for ISO, recovery, and reinstall flows

## Filesystem And Namespace

- `src/kernel/simplefs.c`
- `src/kernel/vfs.c`
- `include/shared/simplefs_format.h`

Responsibilities:

- on-disk `simplefs` format and mutation rules
- path resolution, directory operations, permissions, and sticky `/tmp`
- console/tty and pipe inodes
- rootfs mount policy and recovery reinstall path
- boot-time writable smoke test

## Networking

- `src/kernel/e1000.c`
- `src/kernel/net.c`

Responsibilities:

- QEMU-first e1000 driver
- static IPv4 configuration
- ARP cache, ICMP echo, and staged `ping4`
- polling-based RX/TX progress through the scheduler path
- network trace hooks for bring-up, ARP, ping start, reply, and timeout

## Userland And Boot Scripts

- `src/user/`
- `src/rootfs/etc/`
- `Makefile`
- `tools/mkfs_rootfs.c`
- `tools/install_disk_image.sh`

Responsibilities:

- build ELF user programs against the freestanding user libc
- ship `/bin/init`, `/bin/sh`, core tools, networking tools, `phase8`, `phase10`, and `trace`
- select the correct rc script for read-only, disk, recovery, reinstall, and soak boots
- produce ISO, disk, recovery, reinstall, and soak images

## Observability Contract

- serial log lines remain the authoritative boot/test interface
- framebuffer text output is the local on-screen console path when graphics handoff is available
- trace records are the structured in-kernel interface for recent events
- `/bin/trace` is the current user-facing trace inspector
- `make check*` targets are the authoritative subsystem proofs

## Current Invariants

- one CPU
- 32-bit x86 under GRUB + QEMU/OVMF
- one fixed-size trace ring
- one staged network path, not sockets
- one teaching filesystem format
- one logical `/dev/console` path, backed by serial input plus serial/VGA/framebuffer output
