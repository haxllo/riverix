# Riverix Major Roadmap

This document defines the next major roadmap for `riverix` after the completion of
the original ten baseline phases. The end goal is no longer just a small
installable Unix-like OS. The end goal is a production-grade desktop operating
system built on top of the current `riverix` foundation.

This roadmap is intentionally split into major topics instead of small numbered
phases. The current kernel and userland are already real enough that the next
work needs to be organized by long-lived product tracks, not only by bring-up
milestones.

## Product Direction

These choices are now assumed unless changed later:

- End goal: full desktop OS
- Near-term execution target: VM-first
- Real hardware comes after stable VM boot, shell, and testing
- The system should stay scalable and clean, not artificially minimal

## What `simplefs` Is

`simplefs` is Riverix's current teaching filesystem.

It is:

- a small custom on-disk filesystem format
- the current root filesystem format used by the ISO, disk, recovery, and reinstall flows
- integrated with the Riverix VFS layer
- intentionally simple enough to understand and debug end to end

It is not:

- a borrowed Linux or BSD filesystem
- a production-grade journaled filesystem
- necessarily the final long-term Riverix filesystem

Today it is good enough for:

- persistent writable rootfs
- file and directory creation/removal
- reboot persistence
- recovery reinstall flows

It is not yet good enough for:

- strong crash consistency guarantees
- large-scale caching and writeback tuning
- advanced filesystem features expected from a mature desktop OS

Direction:

- `simplefs` is now explicitly transitional
- Riverix will replace it instead of treating it as the long-term production desktop filesystem

## Current Foundation Mapping

These major topics already have meaningful completed work.

### Boot And Install Base

Completed:

- Multiboot + GRUB boot
- UEFI/GPT disk image layout
- ISO, disk, recovery, reinstall, and soak boot paths
- recovery and reinstall tooling
- persistence verification

### Kernel Core

Completed:

- paging, physical allocator, kernel heap, guarded kernel stacks
- interrupts, timer, scheduler
- panic path
- structured trace ring
- automated soak validation

### Process And Memory Model

Completed:

- ring-3 userspace
- per-process address spaces
- `fork`, `exec`, `exit`, `waitpid`
- copy-on-write `fork`
- user fault kill/reap policy
- validated usercopy path

### VFS And Storage

Completed:

- VFS layer
- `simplefs`
- fd tables, cwd, pathname operations
- writable disk-backed rootfs
- GPT partition handling
- ATA PIO and AHCI storage paths

### Unix Semantics

Completed:

- pipes
- tty basics
- uid/gid basics
- session/process-group groundwork
- permission checks and sticky `/tmp`

### Networking

Completed:

- e1000 driver
- static IPv4
- ARP
- ICMP echo
- `netinfo` and `ping`

### Userland

Completed:

- `/bin/init`
- `/bin/sh`
- small libc/syscall wrapper layer
- core tools: `echo`, `ls`, `cat`, `mkdir`, `rm`, `ps`, `pwd`, `id`, `tty`
- recovery/install tools
- observability tools: `trace`, `phase10`

## Major Topics To Reach A Production-Grade Desktop OS

### 1. Platform Portability And Display Foundation

Goal:

- make Riverix boot visibly and predictably on more than QEMU

Already done:

- UEFI disk layout
- VM-first boot discipline
- serial and legacy VGA text output
- framebuffer-backed text console through GRUB graphics handoff
- shared backend-driven console input core
- serial and i8042 keyboard backends feeding `/dev/console`

Still needed:

- broader VM validation and cleanup for the new framebuffer console path
- cleaner console abstraction instead of ad hoc serial + VGA + framebuffer fanout
- Hyper-V Gen2 synthetic keyboard backend on top of the new input core
- broader VM compatibility, especially Hyper-V class environments
- later, real hardware display bring-up

Why this is first:

- the current kernel foundation is real, but the platform/display foundation is still too narrow

### 2. Kernel Hardening And Scale

Goal:

- keep the current kernel understandable while making it less fragile under growth

Already done:

- panic and trace infrastructure
- soak-mode verification
- real scheduler/process/VM stack

Still needed:

- removal of painful fixed-size limits
- better internal subsystem contracts
- cleaner concurrency model
- later SMP planning and implementation

### 3. Storage Maturity

Goal:

- move from a correct teaching storage stack to a durable desktop-grade storage stack

Already done:

- persistent writable rootfs
- reinstall and recovery
- controller/device abstraction
- disk boot through ATA and AHCI

Still needed:

- stronger writeback model
- crash consistency strategy
- repair tooling beyond full reinstall
- replacement filesystem design and migration path away from `simplefs`

### 4. Network And IPC Platform

Goal:

- move from staged diagnostics to a real service-capable communication layer

Already done:

- NIC bring-up
- ARP/IPv4/ICMP
- basic network tooling

Still needed:

- IPC model for local services
- UDP
- TCP
- sockets API
- DHCP and DNS
- service-to-service communication model for later desktop daemons

### 5. Security, Identity, And Trust

Goal:

- build a system that can support real users, sessions, and trusted software updates

Already done:

- uid/gid basics
- permission checks
- root/non-root distinction

Still needed:

- login/authentication model
- fuller users/groups model
- executable and update trust model
- privileged service design
- sandboxing direction for desktop apps later

### 6. Userland Maturity

Goal:

- turn the current shell-first environment into a deeper Unix userland

Already done:

- init, shell, libc shim, basic tools
- rc scripts for normal/recovery/reinstall/soak boots

Still needed:

- stronger shell behavior
- more tools: copy/move, edit, inspect, archive, system control
- userland conventions and layout discipline
- later package/install/update user tools

### 7. Service Layer And System Management

Goal:

- grow from rc-script bootstrapping into a true managed OS service layer

Already done:

- boot mode selection in init
- recovery/reinstall operational scripts

Still needed:

- service manager model
- background daemons
- logging pipeline
- settings/config model
- session management

### 8. Graphics, Input, And Desktop Base

Goal:

- create the actual foundation for a desktop OS instead of stopping at a shell

Already done:

- only the very early console groundwork

Still needed:

- framebuffer/GPU display path
- keyboard/mouse input path beyond serial assumptions
- text rendering
- compositor/window server direction
- desktop shell direction

This is where Riverix stops being only a small Unix OS and starts becoming a desktop platform.

### 9. Desktop Frameworks And Applications

Goal:

- make the OS usable as a desktop, not just bootable as a kernel and shell

Already done:

- none in a desktop sense

Still needed:

- application framework direction
- settings UI
- terminal UI or terminal app
- file manager
- system monitor
- installer/updater UX

### 10. Production Discipline

Goal:

- make Riverix releasable, supportable, and testable as a product

Already done:

- strong QEMU-centered verification matrix
- persistence, recovery, reinstall, and soak automation

Still needed:

- broader compatibility matrix
- release criteria
- upgrade policy
- rollback and backup policy
- operational documentation
- performance benchmarks
- security response model

## Recommended Execution Order

This is the recommended order from the current state.

1. Platform portability and display foundation
2. Storage maturity
3. Network and IPC platform
4. Security, identity, and trust
5. Userland maturity
6. Service layer and system management
7. Graphics, input, and desktop base
8. Desktop frameworks and applications
9. Production discipline

## Immediate Next Track

The best next major track is:

- Platform portability and display foundation

Why:

- the kernel and Unix foundation are already strong enough to build on
- Hyper-V exposed a real platform gap
- the current biggest weakness is not the process model or VFS anymore
- the current biggest weakness is portability of boot visibility and console behavior

## Decision Points That Will Matter Later

These do not need to be resolved immediately, but they will shape the long-term OS:

- what the `simplefs` replacement should be: evolve a new Riverix-native filesystem or adopt a more established design pattern
- what service manager model Riverix should adopt
- what desktop graphics/compositor architecture Riverix should use
- what application framework and package/update model Riverix should expose

## Short Version

Riverix already has a real Unix-like OS foundation.

The next job is not to rebuild that foundation. The next job is to:

- make it portable across VMs
- harden storage and communications
- deepen the service and userland layers
- then build the real desktop stack on top of that base
