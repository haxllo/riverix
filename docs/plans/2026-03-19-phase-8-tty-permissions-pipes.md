# Phase 8 TTY, Permissions, And Pipes Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Complete Phase 8 by adding basic session/controlling-tty groundwork, a real uid/gid + mode permission model, and blocking pipes with shell pipeline support.

**Architecture:** Keep the existing kernel shape and grow it vertically. Add process credentials and tty/session metadata to `proc`, propagate those through `fork`/`exec`, move file metadata and permission checks into VFS/simplefs, then add in-memory pipe file objects plus blocking syscall paths that can suspend and resume tasks correctly. Finish by making the shell and bootstrap scripts use the new primitives so the phase is proven through real userspace instead of only kernel logs.

**Tech Stack:** freestanding C kernel, x86 interrupt/syscall path, simplefs on-disk format, ring-3 userland, GRUB/QEMU verification

---

### Task 1: Add process credentials and TTY/session groundwork

**Files:**
- Modify: `include/shared/syscall_abi.h`
- Modify: `include/kernel/proc.h`
- Modify: `src/kernel/proc.c`
- Modify: `src/kernel/syscall.c`
- Modify: `include/user/unistd.h`
- Modify: `src/user/libc/syscall.c`
- Modify: `include/kernel/vfs.h`
- Modify: `src/kernel/vfs.c`

**Steps:**
1. Extend task state in `proc.c` with `uid`, `gid`, `sid`, `pgid`, and controlling-tty metadata.
2. Initialize `/bin/init` as root on a controlling console tty, inherit those fields across `fork`, and preserve them across `exec`.
3. Add syscall ABI for at least `getuid`, `getgid`, `setuid`, `setgid`, and `setsid`, plus `pipe`.
4. Add a synthetic `/dev/tty` path in VFS that resolves against the current task’s controlling tty.
5. Expose the new task/session/credential fields through `procinfo` so userspace can inspect them.

**Verification:**
- `/bin/tty` can resolve and print the current controlling tty.
- `ps` can show session/process-group/uid state.
- `setsid` creates a fresh session without breaking stdio.

### Task 2: Add file metadata and enforce permissions

**Files:**
- Modify: `include/shared/simplefs_format.h`
- Modify: `src/kernel/simplefs.c`
- Modify: `tools/mkfs_rootfs.c`
- Modify: `include/kernel/vfs.h`
- Modify: `src/kernel/vfs.c`
- Modify: `src/kernel/proc.c`
- Modify: `include/shared/syscall_abi.h`
- Modify: `include/user/sys/stat.h`
- Modify: `src/user/ls.c`

**Steps:**
1. Extend the on-disk and in-memory inode metadata with mode bits, uid, gid, and link count.
2. Bump the simplefs format version and make `mkfs_rootfs` emit root-owned directories/files with sensible defaults:
   - directories `0755`
   - `/bin/*` and executable scripts `0755`
   - normal config/data files `0644`
   - add `/tmp` as a world-writable directory for non-root proofs
3. Add VFS permission checks for path traversal, read, write, create, unlink, truncate, and directory mutation.
4. Preserve root override semantics so current bootstrap still works while non-root users are constrained.
5. Extend `stat`/`readdir` user-visible structures so tools can inspect ownership and mode cleanly.

**Verification:**
- root can still boot and run all existing scripts.
- a non-root task can read public files but cannot mutate root-owned protected paths.
- a non-root task can create and remove files under `/tmp`.

### Task 3: Add real pipe file objects and blocking read/write resume

**Files:**
- Modify: `include/kernel/vfs.h`
- Modify: `src/kernel/vfs.c`
- Modify: `include/kernel/proc.h`
- Modify: `src/kernel/proc.c`
- Modify: `src/kernel/syscall.c`
- Modify: `include/shared/syscall_abi.h`
- Modify: `include/user/unistd.h`
- Modify: `src/user/libc/syscall.c`

**Steps:**
1. Add pipe-backed `vfs_file_t` objects with a shared in-memory ring buffer and explicit reader/writer reference counts.
2. Add `vfs_create_pipe()` and `proc_pipe()` helpers that install paired read/write descriptors.
3. Extend task blocking state in `proc` so a task can block on a pipe read/write wait channel and later resume with a correct syscall return value.
4. Convert `read` and `write` syscall handling into resumable kernel paths for pipe-backed fds while keeping existing regular-file/device behavior intact.
5. Wake blocked readers/writers when the pipe buffer transitions, when ends close, and when EOF/broken-pipe semantics apply.

**Verification:**
- `pipe()` returns two working fds.
- a reader blocks until a writer produces data.
- EOF is delivered when the last writer closes.
- broken-pipe writes fail cleanly when the last reader closes.

### Task 4: Grow userspace around the new semantics

**Files:**
- Modify: `src/user/sh.c`
- Modify: `src/user/ps.c`
- Create: `src/user/id.c`
- Create: `src/user/tty.c`
- Create: `src/user/phase8.c`
- Modify: `Makefile`
- Modify: `src/rootfs/etc/rc-ro`
- Modify: `src/rootfs/etc/rc-disk`

**Steps:**
1. Upgrade the shell parser so it can split a command line into pipeline stages, create pipes between stages, and still support final stdout redirection.
2. Clean up the interactive prompt so it shows a clearer shell identity, for example `riverix:/path#` or `$` based on uid. This is the “clean look” slice for the current serial/VGA environment; actual terminal font selection remains host-controlled.
3. Extend `ps` to print uid/session/process-group/tty columns.
4. Add `id` and `tty` tools.
5. Add a dedicated `/bin/phase8` proof binary that drops to a non-root uid, proves permission enforcement, and confirms `/tmp` works.
6. Update the rc scripts to exercise a real shell pipeline such as `echo ... | cat`.

**Verification:**
- shell pipelines work on both ramdisk and disk boot paths.
- prompt stays readable and stable.
- `/bin/phase8` exits cleanly only when uid/mode enforcement behaves correctly.

### Task 5: Verify, document, and publish

**Files:**
- Modify: `Makefile`
- Modify: `README.md`
- Modify: `docs/kernel-user-abi.md`
- Modify: `docs/plans/2026-03-16-riverix-system-roadmap.md`

**Steps:**
1. Extend the existing `make check*` targets with Phase 8 markers for:
   - `/dev/tty` or tty tool output
   - uid/permission proof
   - pipeline proof
2. Keep ISO, ATA disk, AHCI disk, recovery, reinstall, and persistence checks green.
3. Update docs so the current state and roadmap reflect that Phase 8 is complete and Phase 9 is next.
4. Commit and push only after the full matrix passes.

**Verification:**
- `make check`
- `make check-disk`
- `make check-disk-ahci`
- `make check-disk-recovery`
- `make check-disk-reinstall`
- `make check-disk-persist`
