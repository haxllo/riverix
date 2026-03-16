# Block-Backed Rootfs Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the built-in file table rootfs with a real read-only filesystem image loaded as a Multiboot module and mounted through a block device layer.

**Architecture:** Keep the design layered. GRUB loads a standalone `rootfs.img` as a Multiboot module, the kernel exposes it as a memory-backed block device, a tiny read-only filesystem driver parses the on-disk metadata, and the VFS mounts the resulting inode tree plus `/dev/console`. File contents must be read from filesystem blocks, not from compiled-in byte pointers.

**Tech Stack:** 32-bit x86 kernel C/assembly, Multiboot1 modules, GRUB ISO boot, QEMU/OVMF, host-side C mkfs tool.

---

### Task 1: Add Multiboot module handling and preserve module memory

**Files:**
- Modify: `include/kernel/multiboot.h`
- Modify: `src/kernel/palloc.c`
- Modify: `src/kernel/kernel.c`

**Step 1: Define Multiboot module metadata**

Add a `multiboot_module_t` structure plus helpers for begin/end access.

**Step 2: Reserve module memory**

Extend physical allocator boot reservations so module descriptors, module byte ranges, and module command lines are not handed out as free pages.

**Step 3: Thread module metadata into boot**

Update kernel boot flow so the VFS/rootfs layer can inspect Multiboot modules and mount the rootfs image before process startup.

### Task 2: Add block-device and ramdisk foundations

**Files:**
- Create: `include/kernel/block.h`
- Create: `src/kernel/block.c`
- Create: `include/kernel/ramdisk.h`
- Create: `src/kernel/ramdisk.c`

**Step 1: Add a generic block-device interface**

Define a fixed-capacity block registry with block size, block count, read-only flag, and read callback.

**Step 2: Add a ramdisk-backed block device**

Wrap a Multiboot module byte range as a read-only block device with 512-byte sectors.

**Step 3: Emit boot logs**

Log device registration and mount-time failures clearly so `make check` can prove the block layer is alive.

### Task 3: Mount a real filesystem image through VFS

**Files:**
- Create: `include/kernel/simplefs.h`
- Create: `src/kernel/simplefs.c`
- Modify: `include/kernel/vfs.h`
- Modify: `src/kernel/vfs.c`

**Step 1: Define a tiny on-disk filesystem format**

Add a superblock, inode table, and directory entry layout for a small read-only rootfs image.

**Step 2: Parse and mount the filesystem**

Load inode metadata from the block device, build the in-memory VFS tree, and keep `/dev/console` as a synthetic device subtree.

**Step 3: Serve regular-file reads from blocks**

File lookup and `exec` must consume bytes by reading filesystem blocks through the block layer, not through linked symbols.

### Task 4: Add host mkfs pipeline and boot the new rootfs

**Files:**
- Create: `tools/mkfs_rootfs.c`
- Modify: `Makefile`
- Modify: `grub/grub.cfg`
- Remove: `src/user/init.S` linkage as kernel blob input

**Step 1: Build a standalone `/bin/init` ELF**

Keep the current static init userspace program, but stop linking it into the kernel.

**Step 2: Generate `rootfs.img`**

Build a host-side mkfs tool that writes `/bin/init` plus the root directory structure into a block-aligned image.

**Step 3: Load the image as a Multiboot module**

Copy `rootfs.img` into the ISO and add a GRUB `module` entry so the kernel receives the image separately from `kernel.elf`.

### Task 5: Verify the boot path and update docs

**Files:**
- Modify: `Makefile`
- Modify: `README.md`
- Check: `build/qemu.log`

**Step 1: Strengthen automated checks**

Require boot logs for module discovery, block-device registration, filesystem mount, ELF load, and `/bin/init` startup.

**Step 2: Update project state**

Document that the rootfs is now block-backed but still memory-resident, and call out that persistence will require a real hardware-backed disk driver next.
