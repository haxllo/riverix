# Phase 2 Process Lifecycle Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a minimal but real Unix-like process lifecycle to riverix with `exit`, `waitpid`, user-visible `exec`, and `fork`, then prove it from userspace with a child ELF loaded from `/bin`.

**Architecture:** Keep the existing timer-preempted scheduler and ring-3 return path, then extend it rather than replacing it. `fork` should create a new task with a cloned address space and shared file-description references, `exec` should replace the current task image in-place using a freshly loaded address space, and `waitpid` should block in the kernel and complete when a matching child exits.

**Tech Stack:** freestanding C, 32-bit x86, GRUB/Multiboot, QEMU/OVMF, ELF32, simplefs, ATA PIO, VFS fd tables

---

### Task 1: Save Phase 2 Structure

**Files:**
- Create: `docs/plans/2026-03-16-phase-2-process-lifecycle.md`
- Modify: `docs/plans/2026-03-16-riverix-system-roadmap.md` if scope changes during implementation

**Step 1: Capture the vertical slice**

Write the cut as:
- kernel task states and parent/child bookkeeping
- user-copy helpers that work across arbitrary address spaces
- eager-copy `fork`
- in-place `exec`
- blocked `waitpid`
- userspace proof with `/bin/init` and `/bin/child`

**Step 2: Keep the verification contract explicit**

Success signals:
- `init` forks a child
- child `exec`s `/bin/child`
- parent waits successfully
- exit status is observed in userspace

### Task 2: Extend Process State And VFS References

**Files:**
- Modify: `include/kernel/proc.h`
- Modify: `src/kernel/proc.c`
- Modify: `include/kernel/vfs.h`
- Modify: `src/kernel/vfs.c`

**Step 1: Add lifecycle state**

Introduce task states for:
- runnable
- running
- blocked
- zombie

Track:
- `parent_pid`
- `exit_status`
- `wait_target_pid`
- `wait_status_user`

**Step 2: Add fd inheritance helpers**

Expose a helper that clones a task fd table by retaining existing `vfs_file_t` objects so parent and child share file descriptions and offsets.

**Step 3: Split cleanup**

Keep a distinction between:
- runtime resource release for a finished task
- final slot reset when a zombie is reaped

### Task 3: Add Address-Space Copy And User-Copy Helpers

**Files:**
- Modify: `include/kernel/paging.h`
- Modify: `src/kernel/paging.c`
- Modify: `include/kernel/usercopy.h`
- Modify: `src/kernel/usercopy.c`
- Modify: `include/kernel/exec.h`
- Modify: `src/kernel/exec.c`

**Step 1: Add page metadata lookup**

Expose a paging helper that can query the physical page and flags for a user virtual address in an arbitrary page directory.

**Step 2: Add user-copy helpers that work on non-current tasks**

Implement:
- `user_copy_from_in(directory, dst, src_user, len)`
- `user_copy_to_in(directory, dst_user, src, len)`
- `user_copy_string_from(...)`

Keep current-directory wrappers for the existing syscall path.

**Step 3: Add eager image cloning**

Clone the mapped user pages recorded in `exec_image_t` into a new address space, preserving page permissions and contents.

### Task 4: Implement Lifecycle Syscalls

**Files:**
- Modify: `include/kernel/syscall.h`
- Modify: `src/kernel/syscall.c`
- Modify: `include/kernel/proc.h`
- Modify: `src/kernel/proc.c`

**Step 1: Add syscall numbers and wrappers**

Add:
- `SYS_EXIT`
- `SYS_WAITPID`
- `SYS_EXEC`
- `SYS_FORK`

**Step 2: Implement `exit`**

Mark the current task zombie, store status, wake a waiting parent when possible, and schedule away.

**Step 3: Implement `waitpid`**

If a matching zombie exists, reap it immediately. Otherwise, block the current task until a matching child exits. Return `-1` when there is no matching child.

**Step 4: Implement `exec`**

Copy a user path string, load the ELF into a fresh address space, atomically replace the current task image, and return through a rebuilt ring-3 frame instead of back to the old program.

**Step 5: Implement `fork`**

Clone the current user task image, build a copied return frame on the child kernel stack, return child pid to the parent, and return `0` to the child.

### Task 5: Add User-Space Proof

**Files:**
- Modify: `src/user/init.S`
- Create: `src/user/child.S`
- Modify: `src/user/init.ld` if needed
- Modify: `tools/mkfs_rootfs.c`
- Modify: `Makefile`

**Step 1: Build two ELFs**

Produce:
- `/bin/init`
- `/bin/child`

**Step 2: Make init prove the lifecycle**

Flow:
1. print `init: online`
2. `fork`
3. child `exec("/bin/child")`
4. parent `waitpid`
5. parent prints the observed exit status
6. init stays alive and continues yielding

**Step 3: Make child prove `exec` + `exit`**

`/bin/child` should:
- print a distinct banner
- exit with a fixed status

### Task 6: Verification And Documentation

**Files:**
- Modify: `Makefile`
- Modify: `README.md`
- Inspect: `build/qemu.log`
- Inspect: `build/qemu-disk.log`

**Step 1: Tighten automated checks**

Add log assertions for:
- child exec path
- child exit
- parent wait result

**Step 2: Run verification**

Run:
- `make check`
- `make check-disk`
- `make check-disk-persist`

Expected new log markers:
- `child: online`
- `init: child pid 0x...`
- `init: child exit 0x...`

**Step 3: Update repository state**

Document the new syscall/process baseline and the remaining gaps:
- no copy-on-write yet
- no general userland file syscalls yet
- no signal model
