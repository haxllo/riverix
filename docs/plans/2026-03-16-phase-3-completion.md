# Phase 3 Completion Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Finish the remaining Phase 3 memory-model work so the kernel has guarded kernel stacks, stronger paging helpers, and an explicit churn proof for repeated process creation and teardown.

**Architecture:** Build the remaining Phase 3 work around one real kernel invariant: every task should run with a kernel stack that lives in shared kernel virtual space, has an unmapped guard page below it, and can be allocated and freed without ad hoc per-task paging logic. Use paging range helpers to support that design cleanly, then prove the result by repeatedly forking and reaping user processes without exhausting task, stack, or page resources.

**Tech Stack:** freestanding C, 32-bit x86 paging, Multiboot/QEMU, serial-log verification, hand-written ring-3 assembly test programs

---

### Task 1: Add shared kernel-region paging helpers

**Files:**
- Modify: `include/kernel/paging.h`
- Modify: `src/kernel/paging.c`
- Test: `Makefile`

**Step 1: Add remaining kernel virtual-region constants**

- Define a dedicated kernel-stack region above the heap and below the recursive page-table area.
- Keep heap, stack, direct-map, and recursive regions non-overlapping.

**Step 2: Add paging range helpers**

- Add range-oriented helpers for page-presence checks, multi-page mapping, and multi-page unmapping.
- Keep the helpers generic so they can serve both kernel-stack management and future VM growth.

**Step 3: Preallocate shared page tables for the stack region**

- Extend `paging_init()` so the kernel-stack PDE range is allocated alongside the heap PDE range.
- Keep those PDEs inside the shared kernel portion copied into every address space.

**Step 4: Verify**

Run: `make check`
Expected: boot still reaches userland with no regression in existing memory or process checks.

### Task 2: Introduce a dedicated guarded kernel-stack allocator

**Files:**
- Create: `include/kernel/kstack.h`
- Create: `src/kernel/kstack.c`
- Modify: `src/kernel/kernel.c`
- Modify: `Makefile`

**Step 1: Define the stack allocator API**

- Add a small `kernel_stack_t` descriptor containing the guard base, mapped stack base, and stack top.
- Export `kstack_init()`, `kstack_selftest()`, `kstack_alloc()`, `kstack_free()`, and `kstack_is_guard_address()`.

**Step 2: Allocate guarded stacks from a dedicated region**

- Reserve fixed-size stack slots, each with one unmapped guard page below the mapped stack pages.
- Back only the mapped stack pages with `palloc` frames and map them into the shared kernel-stack region.

**Step 3: Add a selftest**

- Allocate one test stack, verify the guard page stays unmapped and the stack pages are mapped, then free it.
- Log a clear success marker for boot-time verification.

**Step 4: Verify**

Run:

```bash
make check
make check-disk
```

Expected: both paths show `kstack: ready` and `kstack: selftest ok`.

### Task 3: Move process kernel stacks onto the guarded allocator

**Files:**
- Modify: `include/kernel/proc.h`
- Modify: `src/kernel/proc.c`
- Modify: `src/kernel/idt.c`

**Step 1: Replace single-page direct-map stacks**

- Remove the one-page `kstack_phys` model from tasks.
- Store a `kernel_stack_t` allocation per task and use its `stack_top` for kernel/user return frames and TSS `esp0`.

**Step 2: Release stacks correctly**

- On task teardown, free the guarded stack allocation after the task is no longer current.
- Keep zombie reaping and current-task exit flow unchanged conceptually.

**Step 3: Improve page-fault diagnostics**

- When a kernel page fault hits a guard page, log that it was a kernel-stack guard hit before halting.
- Keep user faults on the existing kill-and-reap path.

**Step 4: Verify**

Run:

```bash
make check
make check-disk
```

Expected: scheduling and process lifecycle remain stable with guarded stacks in place.

### Task 4: Add a repeated-process churn proof and update docs

**Files:**
- Modify: `src/user/init.S`
- Modify: `Makefile`
- Modify: `README.md`
- Create: `docs/plans/2026-03-16-phase-3-completion.md`

**Step 1: Add a churn proof to init**

- After the existing child, fault, and COW proofs, fork and reap several short-lived children in a loop.
- Print a single success marker only after the full loop completes.

**Step 2: Extend verification**

- Add serial-log assertions for the new stack bring-up markers and the churn completion marker.
- Keep `make check`, `make check-disk`, and `make check-disk-persist` green.

**Step 3: Update project docs**

- Mark Phase 3 complete in the README.
- Shift the next milestone toward Phase 4 syscall and file-ABI growth.

**Step 4: Final verification**

Run:

```bash
make check
make check-disk
make check-disk-persist
```

Expected: all three pass, and repeated process creation no longer depends on single-page unguarded kernel stacks.
