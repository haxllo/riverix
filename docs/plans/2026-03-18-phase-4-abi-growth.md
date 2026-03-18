# Phase 4 ABI Growth Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Expand the kernel/user ABI from a bring-up syscall set into a usable file/process interface with real file descriptors, pathname-based filesystem access, directory mutation, and basic timing primitives.

**Architecture:** Build Phase 4 around the existing VFS/file-description model rather than adding syscall-specific shortcuts. Keep one open-file-description object per open, share it through `fork` and `dup`, resolve relative paths through a per-process cwd string, and expose only operations the current VFS/simplefs stack can implement honestly.

**Tech Stack:** freestanding C, 32-bit x86 syscall trap ABI, simplefs, VFS file-description layer, hand-written assembly user tests, QEMU serial-log verification

---

### Task 1: Extend the syscall ABI and process fd core

**Files:**
- Modify: `include/kernel/syscall.h`
- Modify: `src/kernel/syscall.c`
- Modify: `src/kernel/proc.c`
- Modify: `include/kernel/proc.h`
- Modify: `src/user/syscall.inc`

**Step 1: Add stable syscall numbers and wrappers**

- Add syscall numbers for `open`, `close`, `read`, `lseek`, `mkdir`, `unlink`, `stat`, `chdir`, `dup`, `dup2`, `sleep`, and `ticks`.
- Keep existing numbering stable and append new calls after the current Phase 3 set.

**Step 2: Add per-process fd-table helpers**

- Implement fd allocation, install, close, read, write, seek, and duplication helpers in `proc.c`.
- Keep `fork` inheritance and `dup` semantics tied to shared `vfs_file_t` objects so offsets stay shared where they should.

**Step 3: Add sleep/tick process support**

- Add a sleeping task state plus wakeup tick tracking.
- Wake sleeping tasks from the scheduler/timer path.

**Step 4: Verify**

Run: `make check`
Expected: existing process/fault/COW tests remain green.

### Task 2: Grow the VFS interface to support pathname syscalls

**Files:**
- Modify: `include/kernel/vfs.h`
- Modify: `src/kernel/vfs.c`
- Modify: `src/kernel/simplefs.c`
- Modify: `include/kernel/simplefs.h`

**Step 1: Add missing VFS operations**

- Add public helpers for `mkdir`, `unlink`, `stat`, `seek`, `read_fd`, `close_fd`, and `dup`-style reference handling.
- Tighten permission checks so read/write honor open flags consistently.

**Step 2: Add inode-level remove/stat support**

- Extend inode ops with child removal.
- Add a small `vfs_stat_t` struct exposing file kind and size.

**Step 3: Implement simplefs remove semantics**

- Support unlinking files and empty directories.
- Free inode and data extents safely and update directory metadata.
- Reject removal of non-empty directories and synthetic entries like `/dev/console`.

**Step 4: Verify**

Run:

```bash
make check
make check-disk
```

Expected: boot remains stable and disk-backed path still passes writable-storage checks.

### Task 3: Add per-process cwd and relative path resolution

**Files:**
- Modify: `src/kernel/proc.c`
- Modify: `include/kernel/proc.h`

**Step 1: Track cwd per task**

- Add a bounded cwd string to each task with `/` as the default.
- Inherit cwd across `fork` and preserve it across `exec`.

**Step 2: Add path normalization**

- Normalize absolute and relative paths in process syscalls.
- Support `.`, `..`, duplicate slashes, and bounded output paths.

**Step 3: Wire pathname syscalls through cwd-aware resolution**

- `open`, `mkdir`, `unlink`, `stat`, and `chdir` should all accept relative paths.

**Step 4: Verify**

Run: `make check`
Expected: relative-path userspace tests can execute without kernel-only absolute paths.

### Task 4: Add a Phase 4 userspace proof and ABI documentation

**Files:**
- Modify: `src/user/init.S`
- Create: `src/user/phase4.S`
- Modify: `Makefile`
- Modify: `README.md`
- Create: `docs/kernel-user-abi.md`
- Create: `docs/plans/2026-03-18-phase-4-abi-growth.md`

**Step 1: Add a dedicated Phase 4 user test program**

- Build `/bin/phase4` as a ring-3 syscall proof.
- Test:
  - `chdir` + relative `open`
  - `read` + `lseek`
  - `stat`
  - disk-path-only `mkdir`, create/write/unlink`
  - `dup`/`dup2` redirection proof
  - `ticks` + `sleep`

**Step 2: Keep ISO and disk verification honest**

- On the ISO/ramdisk path, the program should print a clear writable-filesystem skip marker instead of pretending to mutate storage.
- On the disk path, it should print success markers for writable operations and redirection.

**Step 3: Extend automated checks**

- Add log assertions for the Phase 4 user proof on ISO, disk, and persistence boots.

**Step 4: Document the ABI**

- Record syscall numbers, arguments, return values, and current semantics in `docs/kernel-user-abi.md`.
- Update `README.md` so Phase 4 is reflected accurately and the next step points toward Phase 5 userland growth.

**Step 5: Final verification**

Run:

```bash
make check
make check-disk
make check-disk-persist
```

Expected: all three pass, and the kernel exposes a real file/process syscall floor rather than fixed boot-test helpers.
