# riverix

`riverix` is a small Unix-derived teaching kernel built from scratch on a modern Unix host.
It borrows Unix design ideas such as a small kernel core, simple device interfaces, a
process-oriented execution model, and a filesystem-first view of the system, but it does
not vendor historical AT&T Unix source into the kernel tree.

## Current state

The repository currently boots a 32-bit Multiboot kernel under GRUB and QEMU, initializes
serial and VGA console output, parses the Multiboot memory map, brings up a bitmap-backed
physical page allocator, and enables paging with both identity mappings and a higher-half
direct map at `0xC0000000`. It also installs a flat GDT, loads an IDT, remaps the PIC,
starts the PIT timer, and handles timer interrupts in a verified round-robin kernel-thread
scheduler with separate task stacks. A small `int 0x80` syscall layer now exposes `write`,
`getpid`, and `yield`, and a minimal VFS/file-descriptor layer attaches `fd 0`, `fd 1`,
and `fd 2` to a console device for each task. The kernel now also loads user code/data
segments plus a TSS, resolves `/bin/init`, validates and loads static ELF32 executables
into private user address spaces, and launches ring-3 tasks through the same
timer-preempted scheduler used by kernel workers. User tasks now support a minimal but
real Unix-like lifecycle: `fork` clones the current task into a fresh address space with
shared file-description references, `exec` replaces the current image in-place, `exit`
creates a zombie state, and `waitpid` blocks until a child exits and returns its status.
That `fork` path is now copy-on-write rather than eager-copy: writable user pages are
shared read-only across parent and child until the first write fault or kernel writeback
forces a private split. The shipped `/bin/init` program now proves three paths in sequence:
it forks and `exec`s `/bin/child`, it verifies kill-on-fault recovery through `/bin/fault`,
and it verifies COW by having a child modify inherited writable state while the parent
still observes the original value. User tasks still run under their own page directories
with shared kernel mappings plus private user mappings, so the kernel has crossed from a
single shared address space into a real per-process VM model. The allocator now tracks
physical-page reference counts, which is the right foundation for COW and later VM growth.
The kernel also now reserves a shared heap virtual range above the direct map, grows it on
demand with page-backed mappings from `palloc`, and exposes a small first-fit
`kmalloc`/`kfree` allocator with a boot selftest. The first real heap consumer is the VFS
file table, which has moved off a fixed static array and onto heap-backed storage.
The remaining Phase 3 memory work is now in place too: kernel tasks and user trap/syscall
entry now use dedicated kernel stacks allocated from a shared high kernel-stack region,
each with an unmapped guard page below the live stack pages. The paging layer now exposes
range-oriented helpers used by that stack allocator, kernel page faults can explicitly
identify guard-page hits, and the shipped `/bin/init` program now includes a repeated
`fork`/`waitpid` churn proof after the child, fault, and COW tests to show that process
creation and teardown can reuse stacks and address-space resources without collapsing.
The first Phase 3 fault-policy slice is in place too: user-originated CPU faults are
decoded in the trap layer, logged with fault details, converted into deterministic process
exit statuses, and reaped through the normal wait path instead of panicking the whole
system. Kernel faults still halt the machine. For storage,
the ISO boot path still mounts a read-only `simplefs` image from a Multiboot ramdisk
module, while the GPT/EFI disk-image boot path detects an ATA disk, parses GPT, wraps the
`riverix-rootfs` partition as a block device, and mounts the same filesystem format
directly from disk. On the disk path, `simplefs` is writable with on-disk inode/block
bitmaps, contiguous extent allocation, kernel-driven file creation and truncation, and a
boot-time persistence smoke test that updates `/var/bootcount` across reboots. The build
produces the raw disk image using only user-space tools, which is useful in WSL because it
avoids loop-mount and root-only image assembly steps. Phase 4 is now in place too: the
syscall ABI exposes `open`, `close`, `read`, `write`, `lseek`, `mkdir`, `unlink`, `stat`,
`chdir`, `dup`, `dup2`, `sleep`, and `ticks`, with per-process cwd handling and relative
path resolution. The shipped `/bin/phase4` user program proves that ABI on both boot
paths: on the ISO path it exercises read-only pathname and timing calls and emits a clear
write-skip marker, while on the disk path it creates files under `/var`, reads them back,
redirects output through `dup2`, unlinks files, removes an empty directory, and confirms
that those writes survive the persistence test. Phase 5 is now in place too: the old
assembly `/bin/init` proof harness has been preserved as `/bin/selftest`, while a new C
`/bin/init` runs that selftest, launches `/bin/sh` on a startup script, and then hands
off to an interactive serial shell. The repository now has a small freestanding C
userland runtime, mixed assembly/C user builds, line-buffered `/dev/console` input over
COM1, `execv` with `argc`/`argv` stack setup, and the first real user tools: `echo`,
`ls`, `cat`, `mkdir`, `rm`, and `ps`. The ISO path runs `/etc/rc-ro`, while the disk
path runs `/etc/rc-disk`, which exercises external command launch plus simple stdout
redirection before the system settles into the interactive shell prompt.

## Why this shape

- 32-bit x86 plus Multiboot keeps the early boot path understandable.
- GRUB handles loader details so the repository can focus on kernel code.
- Serial output makes automated checks possible in headless QEMU.
- Historical Unix documents are kept as references, not as imported implementation code.

## Prerequisites

- `gcc`
- `ld`
- `make`
- `grub-mkrescue`
- `grub-mkstandalone`
- `xorriso`
- `mkfs.vfat`
- `mtools`
- `sgdisk`
- `qemu-system-x86_64`
- OVMF firmware files at `/usr/share/OVMF/OVMF_CODE_4M.fd` and `/usr/share/OVMF/OVMF_VARS_4M.fd`

## Build

```bash
make
```

This produces:

- `build/kernel.elf`
- `build/riverix.iso`
- `build/riverix-disk.img` via `make disk-image`

## Run

```bash
make run
```

For the raw GPT/EFI disk image:

```bash
make run-disk
```

## Verify

```bash
make check
```

For the raw GPT/EFI disk image:

```bash
make check-disk
```

To verify persistence across two boots:

```bash
make check-disk-persist
```

The `check` target boots the ISO in headless QEMU, captures serial output, and verifies
the Multiboot rootfs module handoff, block-device registration, filesystem mount, ELF
loading of `/bin/init`, the `fork` -> `exec("/bin/child")` -> `waitpid` lifecycle, the
faulting `/bin/fault` user program with kill-on-fault recovery, the COW fork proof, the
Phase 4 `/bin/phase4` ABI proof on the read-only rootfs path, and timer-driven scheduler
activity.

The `check-disk` target boots the raw disk image in headless QEMU and verifies the ATA
driver, GPT rootfs partition discovery, disk-backed `simplefs` mount, the same user-mode
process lifecycle, the writable Phase 4 filesystem/fd proof, and timer-driven scheduler
activity.

The `check-disk-persist` target rebuilds a fresh disk image, boots it twice, and confirms
that the writable rootfs path persists the `/var/bootcount` update from the first boot to
the second.

## WSL note

This repository builds and tests correctly inside WSL. The disk-image path uses only
user-space tools (`sgdisk`, `mkfs.vfat`, `mtools`, `dd`) so the checkout can live on the
Windows side and still be built from WSL without loop devices or root-only mount steps.
There is no native Windows build script yet; the supported development path is still
Unix-like tooling inside WSL.

## Reference material

See `references/README.md` for the downloaded source material that guides the design.
See `docs/plans/2026-03-16-riverix-system-roadmap.md` for the current end-to-end roadmap.
See `docs/plans/2026-03-16-phase-3-completion.md` for the Phase 3 completion breakdown.
See `docs/plans/2026-03-18-phase-4-abi-growth.md` for the Phase 4 implementation plan.
See `docs/plans/2026-03-18-phase-5-userland-bootstrap.md` for the Phase 5 implementation plan.
See `docs/kernel-user-abi.md` for the current syscall contract.

## Near-term milestones

1. Start Phase 6 install and persistence tooling around the new shell/bootstrap path instead of only raw QEMU images.
2. Expand the shell and base userland beyond the first tool set while keeping the ABI conservative.
3. Strengthen storage internals with better writeback discipline and recovery instead of the current write-through path.
4. Replace the narrow ATA PIO path with a broader disk stack that can grow into PCI/AHCI or NVMe.
