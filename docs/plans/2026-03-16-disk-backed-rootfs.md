# Disk-Backed Rootfs Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Boot the kernel from the GPT/EFI disk image and mount `/bin/init` from a real disk partition instead of a GRUB-loaded Multiboot module, while keeping the ISO + ramdisk path working as a fallback.

**Architecture:** Add a narrow ATA PIO read-only disk driver under the existing block API, then wrap the rootfs GPT partition as a block device that `simplefs` can mount unchanged. VFS should prefer the hardware-backed rootfs path when available and fall back to the Multiboot ramdisk path so both disk and ISO boot flows remain supported without forking the kernel boot logic.

**Tech Stack:** freestanding C, x86 port I/O, Multiboot, QEMU, OVMF, GPT, mtools, sgdisk, make

---

### Task 1: Add the low-level disk and partition layers

**Files:**
- Create: `include/kernel/ata.h`
- Create: `include/kernel/partition.h`
- Modify: `include/kernel/block.h`
- Modify: `include/kernel/io.h`
- Create: `src/kernel/ata.c`
- Create: `src/kernel/partition.c`
- Test: `Makefile`

**Step 1: Write the failing integration expectations**

Expected new serial markers after disk boot:

```text
ata: detected ata0 sectors 0x...
partition: registered rootfs0 start 0x...
```

**Step 2: Run disk boot to verify the markers are absent**

Run: `make check-disk`
Expected: PASS today only because the ramdisk module path is still in use; the new ATA/GPT markers do not exist yet.

**Step 3: Write the minimal implementation**

- Add `inw()` to `include/kernel/io.h` for ATA PIO sector reads.
- Extend `block_device_t` with an optional `parent` pointer so partition devices can record their backing disk cleanly.
- Implement `ata_init()` in `src/kernel/ata.c`:
  - probe the primary master disk with ATA IDENTIFY
  - reject missing or non-ATA devices
  - parse the 28-bit LBA sector count
  - register a read-only `ata0` block device
- Implement `partition_register_rootfs()` in `src/kernel/partition.c`:
  - read GPT header from LBA 1
  - validate GPT signature and entry sizing
  - scan partition entries for the name `riverix-rootfs`
  - wrap that partition as a `rootfs0` block device

**Step 4: Run the disk boot path again**

Run: `make check-disk`
Expected: still FAIL until the VFS and image layout are switched to use the new disk path.

**Step 5: Commit**

```bash
git add include/kernel/ata.h include/kernel/partition.h include/kernel/block.h include/kernel/io.h src/kernel/ata.c src/kernel/partition.c
git commit -m "feat: add ata disk and gpt rootfs layers"
```

### Task 2: Make VFS prefer disk-backed rootfs and preserve ISO fallback

**Files:**
- Modify: `include/kernel/vfs.h`
- Modify: `src/kernel/vfs.c`
- Modify: `src/kernel/kernel.c`
- Test: `Makefile`

**Step 1: Write the failing integration expectations**

Expected new disk-boot markers:

```text
vfs: rootfs mounted from disk
exec: loaded /bin/init entry 0x...
init: online
```

**Step 2: Run disk boot to verify the exact flow is not present**

Run: `make check-disk`
Expected: FAIL after the old ramdisk module is removed from disk boot, because VFS still depends on `ramdisk_register_rootfs()`.

**Step 3: Write the minimal implementation**

- Split `vfs_init()` into mount attempts:
  - `ata_init()`
  - `partition_register_rootfs(block_find("ata0"), "rootfs0")`
  - `simplefs_mount(block_find("rootfs0"), &root_inode)`
  - fall back to `ramdisk_register_rootfs(multiboot_info, "rootfs0")` if the disk path fails
- Keep `/dev/console` synthetic and attach it after whichever rootfs succeeds.
- Add explicit serial logs for disk path vs ramdisk fallback.

**Step 4: Run the disk and ISO checks**

Run: `make check-disk`
Expected: PASS with disk-backed mount.

Run: `make check`
Expected: PASS with ramdisk fallback unchanged.

**Step 5: Commit**

```bash
git add include/kernel/vfs.h src/kernel/vfs.c src/kernel/kernel.c Makefile
git commit -m "feat: prefer disk-backed rootfs with ramdisk fallback"
```

### Task 3: Put the rootfs on a real GPT partition in the disk image

**Files:**
- Create: `grub/grub-disk.cfg`
- Modify: `Makefile`
- Test: `Makefile`

**Step 1: Write the failing integration expectations**

Expected disk image shape:

```text
EFI partition
riverix-rootfs partition
```

**Step 2: Run disk boot with the old image layout**

Run: `make check-disk`
Expected: FAIL after switching disk boot config away from the Multiboot module, because the disk image still stores rootfs only as `/boot/rootfs.img`.

**Step 3: Write the minimal implementation**

- Keep `grub/grub.cfg` for ISO boot with `module /boot/rootfs.img rootfs`.
- Add `grub/grub-disk.cfg` for disk boot without the rootfs module.
- Update `Makefile`:
  - create a second GPT partition named `riverix-rootfs`
  - copy the rootfs image into that partition with `dd ... conv=notrunc`
  - copy `grub/grub-disk.cfg` into `/boot/grub/grub.cfg` on the EFI partition
- Keep the user-space-only image assembly flow so this still works under WSL without loop mounts or root privileges.

**Step 4: Run both verification targets**

Run: `make check-disk`
Expected: PASS with no `ramdisk: rootfs module` dependency in the disk boot log.

Run: `make check`
Expected: PASS with the ISO still using the ramdisk module.

**Step 5: Commit**

```bash
git add grub/grub-disk.cfg Makefile
git commit -m "feat: boot disk image from partition-backed rootfs"
```

### Task 4: Update docs and regression notes

**Files:**
- Modify: `README.md`
- Modify: `docs/plans/2026-03-16-disk-backed-rootfs.md`

**Step 1: Document the verified behavior**

Add the new boot modes:

```text
ISO boot: GRUB module -> ramdisk -> simplefs
Disk boot: ATA disk -> GPT rootfs partition -> simplefs
```

**Step 2: Run the final checks**

Run: `make check`
Expected: PASS

Run: `make check-disk`
Expected: PASS

**Step 3: Write the minimal documentation**

- Update `README.md` with the new disk-backed rootfs behavior and the remaining gap: the disk image still uses a read-only filesystem.
- Note that this is compatible with WSL because the image build still uses only user-space tools.

**Step 4: Commit**

```bash
git add README.md docs/plans/2026-03-16-disk-backed-rootfs.md
git commit -m "docs: record disk-backed rootfs boot path"
```
