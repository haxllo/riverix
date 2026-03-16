# Phase 3 Copy-On-Write Fork Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the eager-copy `fork` path with a real copy-on-write fork, then prove that shared writable pages split only when either side writes.

**Architecture:** Add physical-page reference counts in the allocator, teach paging about logical writability and copy-on-write resolution, then switch `fork` to share writable user pages as read-only COW mappings. The trap path should resolve user-mode COW write faults transparently, and kernel writes back into user buffers must also trigger COW resolution instead of corrupting shared pages.

**Tech Stack:** freestanding C, 32-bit x86 paging, ring-3 traps, QEMU, OVMF, simplefs userland packaging

---

### Task 1: Save The Slice Plan

**Files:**
- Create: `docs/plans/2026-03-16-phase-3-copy-on-write.md`

**Step 1: Lock the scope**

This slice includes:
- frame refcounts
- logical-writable page checks
- transparent COW resolution
- COW `fork`
- userspace verification

This slice excludes:
- full VM object model
- anonymous `mmap`
- guard pages
- kernel heap

### Task 2: Frame Ownership And Paging Helpers

**Files:**
- Modify: `include/kernel/palloc.h`
- Modify: `src/kernel/palloc.c`
- Modify: `include/kernel/paging.h`
- Modify: `src/kernel/paging.c`

**Step 1: Add frame refcounts**

Expose:
- retain/incref for a physical page
- current refcount query

Change free semantics so `palloc_free_page()` decrements and only returns a frame to the free pool when the last reference is gone.

**Step 2: Add paging COW helpers**

Expose:
- logical user-writable range checks (`WRITABLE` or `COW`)
- COW resolution for a page in an address space

Use a software PTE bit such as bit 9 for `PAGE_COPY_ON_WRITE`.

### Task 3: Switch Fork To COW

**Files:**
- Modify: `include/kernel/exec.h`
- Modify: `src/kernel/exec.c`
- Modify: `src/kernel/proc.c`
- Modify: `src/kernel/usercopy.c`
- Modify: `src/kernel/idt.c`

**Step 1: Share pages on fork**

In `exec_clone_image()`:
- share read-only pages directly
- convert writable user pages in the parent to read-only COW
- map the child to the same physical pages
- retain the shared frames

**Step 2: Resolve COW from faults**

In the page-fault path:
- if a user write fault hits a present COW page, resolve it and resume instead of killing the task

**Step 3: Resolve COW for kernel writes into user memory**

When the kernel writes to a user buffer, for example `waitpid(status)`:
- treat COW as logically writable
- split the page first if needed

### Task 4: Userspace Proof And Verification

**Files:**
- Modify: `src/user/init.S`
- Modify: `Makefile`
- Modify: `README.md`

**Step 1: Add a COW proof inside init**

After the existing process and fault tests:
- fork again
- child writes to a writable data item inherited from init
- child prints the modified value and exits
- parent waits and prints its unchanged original value

**Step 2: Add log assertions**

Require markers for:
- the COW split
- child observed modified value
- parent observed original value

**Step 3: Run verification**

Run:
- `make`
- `make check`
- `make check-disk`
- `make check-disk-persist`

Expected result:
- COW writes do not panic the kernel
- parent and child diverge on first write
- process/fault tests remain green
