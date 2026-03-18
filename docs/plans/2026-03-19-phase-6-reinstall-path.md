# Phase 6.3 Recovery Reinstall Path Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Let recovery mode restore the installed disk rootfs from the known-good ramdisk rootfs image, and prove it with an automated reinstall verification.

**Architecture:** Do not fake a second mounted filesystem. `simplefs` is still a single-mount global implementation, so recovery reinstall should operate below VFS at the block-device layer: copy `rootfs0` onto `ata0p2` only when recovery mode is active. Expose that capability through one narrow syscall, drive it from recovery userspace, and verify it by resetting a real disk image's persistent boot counter across three boots.

**Tech Stack:** freestanding C kernel, block-device layer, VFS boot policy, syscall ABI, small C userspace, GRUB EFI configs, QEMU/OVMF verification

---

### Task 1: Add explicit reinstall boot mode plumbing

**Files:**
- Modify: `include/shared/syscall_abi.h`
- Modify: `include/kernel/bootinfo.h`
- Modify: `src/kernel/bootinfo.c`
- Modify: `include/user/boot.h`
- Modify: `src/user/bootmode.c`

**Steps:**
1. Add a second boot flag for `reinstall=1` in the shared syscall ABI.
2. Parse `reinstall=1` in the shared kernel bootinfo layer.
3. Extend boot-mode logging and `/bin/bootmode` output so recovery reinstall is visible in logs.

**Verification:**
- Recovery reinstall boot logs `boot: root ramdisk recovery reinstall`.
- `/bin/bootmode` prints `bootmode: root ramdisk recovery reinstall`.

### Task 2: Add kernel recovery reinstall support

**Files:**
- Modify: `include/kernel/vfs.h`
- Modify: `src/kernel/vfs.c`
- Modify: `include/shared/syscall_abi.h`
- Modify: `src/kernel/syscall.c`
- Modify: `include/user/boot.h`
- Modify: `include/user/unistd.h` or `include/user/boot.h`
- Modify: `src/user/libc/syscall.c`

**Steps:**
1. Track enough root-device state in VFS to identify the mounted ramdisk rootfs device.
2. Add a recovery-only helper that:
   - finds `rootfs0`
   - initializes ATA if needed
   - registers or reuses `ata0p2`
   - validates block size and block count
   - copies the entire rootfs partition in chunks from ramdisk to disk
3. Expose the helper through a new syscall and userspace wrapper.
4. Emit clear serial markers for begin/success/failure.

**Verification:**
- Recovery reinstall path logs `recovery: reinstall begin` and `recovery: reinstall ok`.
- The syscall returns failure outside recovery mode.

### Task 3: Add recovery reinstall userspace flow

**Files:**
- Create: `src/user/reinstall.c`
- Create: `src/rootfs/etc/rc-reinstall`
- Modify: `src/user/init.c`
- Modify: `Makefile`
- Create: `grub/grub-disk-reinstall.cfg`

**Steps:**
1. Add `/bin/reinstall` as a thin front-end over the new syscall.
2. Add `/etc/rc-reinstall` with explicit markers and a reinstall invocation.
3. Make `/bin/init` choose `/etc/rc-reinstall` when both `recovery=1` and `reinstall=1` are present.
4. Package the new user tool and script into the rootfs.
5. Add a recovery-reinstall GRUB config and convenience run target.

**Verification:**
- Recovery reinstall boot runs `/etc/rc-reinstall`.
- Logs contain the reinstall script markers and userland tool success marker.

### Task 4: Add automated reinstall proof

**Files:**
- Modify: `Makefile`
- Modify: `README.md`
- Modify: `docs/disk-layout.md`
- Modify: `docs/kernel-user-abi.md`

**Steps:**
1. Add a `check-disk-reinstall` target that:
   - builds a fresh normal disk image
   - boots it once and sees `storage: bootcount 0x00000001`
   - rewrites that same image's ESP `grub.cfg` to the recovery-reinstall config
   - boots recovery reinstall and sees reinstall markers
   - restores the normal GRUB config in the same image
   - boots the same image again and sees `storage: bootcount 0x00000001`
2. Document the reinstall path and the new boot flag.

**Verification:**
- `make check-disk-reinstall`
- Existing `make check`, `make check-disk`, `make check-disk-recovery`, and `make check-disk-persist` still pass.

