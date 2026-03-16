# Kernel Heap Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a shared kernel heap backed by pageable kernel virtual memory, then move the first real kernel consumer onto it.

**Architecture:** Reserve a shared kernel-heap virtual range inside the kernel page-directory layout so every address space sees the same heap mappings. Build a small first-fit heap allocator on top of page-backed growth from `palloc`, then prove it with a boot selftest and one live migration away from static storage.

**Tech Stack:** freestanding C, 32-bit x86 paging, Multiboot/QEMU bring-up, Make-based serial-log checks

---

### Task 1: Reserve a shared kernel-heap virtual range

**Files:**
- Modify: `include/kernel/paging.h`
- Modify: `src/kernel/paging.c`
- Test: `Makefile`

**Step 1: Add heap layout constants**

- Define `KERNEL_HEAP_BASE` and `KERNEL_HEAP_LIMIT` in `include/kernel/paging.h`.
- Keep the range above the direct map and below the recursive directory slot.

**Step 2: Preallocate shared heap page tables**

- In `src/kernel/paging.c`, allocate empty page tables for the heap PDE range during `paging_init()`.
- Keep those PDEs inside the shared kernel portion copied into every new address space.

**Step 3: Protect the existing direct map layout**

- Cap the direct-map span so it does not collide with the reserved heap PDE range.

**Step 4: Verify at boot**

Run: `make check`
Expected: boot still reaches `paging: enabled` and later kernel init steps.

### Task 2: Add a page-backed kernel heap allocator

**Files:**
- Create: `include/kernel/kheap.h`
- Create: `src/kernel/kheap.c`
- Modify: `Makefile`
- Modify: `src/kernel/kernel.c`

**Step 1: Add the public heap API**

- Declare `kheap_init()`, `kheap_selftest()`, `kmalloc()`, `kzalloc()`, `kfree()`, and a small stats helper in `include/kernel/kheap.h`.

**Step 2: Implement minimal allocator behavior**

- Use a first-fit free list with split/coalesce.
- Grow the heap by mapping whole pages from `palloc` into the reserved heap virtual range.
- Zero newly mapped pages before exposing them to allocations.

**Step 3: Bring the heap up during boot**

- Call `kheap_init()` and `kheap_selftest()` from `src/kernel/kernel.c` after paging is enabled and before higher-level subsystems use dynamic allocation.

**Step 4: Cover it in boot checks**

- Add `kheap: ready` and `kheap: selftest ok` assertions to the serial-log checks in `Makefile`.

**Step 5: Verify**

Run:

```bash
make check
make check-disk
```

Expected: both paths show the heap bring-up markers and still boot into userland.

### Task 3: Move a real subsystem onto the heap

**Files:**
- Modify: `src/kernel/vfs.c`
- Test: `Makefile`

**Step 1: Pick one small live consumer**

- Replace the fixed global VFS file-table array with a heap-backed table allocated during `vfs_init()`.

**Step 2: Preserve boot behavior**

- Keep zero-init semantics and fail cleanly if heap allocation fails during VFS bring-up.

**Step 3: Verify**

Run:

```bash
make check
make check-disk
make check-disk-persist
```

Expected: ISO path, disk path, and persistence path all pass without regressing process, fault, or COW checks.

### Task 4: Document the new memory baseline

**Files:**
- Modify: `README.md`
- Create: `docs/plans/2026-03-16-phase-3-kernel-heap.md`

**Step 1: Update the project status**

- Note that the kernel now has a shared heap virtual region and a first heap-backed subsystem.

**Step 2: Refresh near-term priorities**

- Move the next milestone toward broader dynamic-kernel structures and safer VM growth rather than more static tables.

**Step 3: Final verification**

Run:

```bash
make check
make check-disk
make check-disk-persist
```

Expected: all three pass and the README matches the actual booted system.
