# Phase 3 User Fault Policy Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add the first Phase 3 memory-robustness slice by decoding CPU faults clearly, killing bad userspace instead of panicking the whole kernel, and proving that behavior from a faulting user program.

**Architecture:** Keep the current scheduler and process lifecycle intact, then route user-originated CPU faults into the same exit/wait path used by normal process termination. The trap layer should remain strict for kernel faults, but user faults should become recoverable at the system level: log the cause, terminate the offending task with a deterministic status, and continue running the rest of the system.

**Tech Stack:** freestanding C, 32-bit x86, IDT/CPU exceptions, ring-3 tasks, QEMU, OVMF, simplefs rootfs packaging

---

### Task 1: Save The Slice Plan

**Files:**
- Create: `docs/plans/2026-03-16-phase-3-user-fault-policy.md`

**Step 1: Lock the cut**

This slice includes:
- better fault diagnostics
- user-fault kill-on-fault policy
- a faulting userspace ELF
- boot-time verification through init

This slice excludes:
- copy-on-write
- kernel heap
- guard pages
- broad syscall expansion

### Task 2: Trap Diagnostics And Fault Policy

**Files:**
- Modify: `src/kernel/idt.c`
- Modify: `include/kernel/proc.h` only if a new helper is strictly needed
- Modify: `src/kernel/proc.c` only if exit-path reuse needs small adjustments

**Step 1: Make fault logs actionable**

For faults, log:
- vector
- error code
- CR2 for page faults
- whether the fault came from user or kernel mode
- page-fault detail bits such as present/protection, read/write, and user/kernel

**Step 2: Route user faults into process exit**

When an exception originates from CPL3:
- log the fault
- terminate the current task through the normal process exit path
- encode the exit status deterministically, for example `0x80 | vector`
- return the next runnable task instead of halting the machine

**Step 3: Keep kernel faults fatal**

If the same fault happens in kernel mode:
- keep the current halt-on-fault behavior
- do not try to recover silently

### Task 3: Userspace Fault Verification

**Files:**
- Modify: `Makefile`
- Modify: `src/user/init.S`
- Create: `src/user/fault.S`
- Modify: `tools/mkfs_rootfs.c` only if the current generic `/bin` packaging needs adjustment

**Step 1: Add `/bin/fault`**

The new ELF should:
- print a banner
- touch an unmapped user address
- never successfully return

**Step 2: Make init verify it**

After the existing child `exec`/`waitpid` proof:
- fork again
- child `exec("/bin/fault")`
- parent waits
- parent prints the observed exit status

**Step 3: Choose a deterministic expected status**

Use the kernel fault policy status so the check can assert an exact value, for example:
- page fault vector `14`
- exit status `0x0000008E`

### Task 4: Verification And Documentation

**Files:**
- Modify: `Makefile`
- Modify: `README.md`
- Inspect: `build/qemu.log`
- Inspect: `build/qemu-disk.log`
- Inspect: `build/qemu-disk-pass1.log`
- Inspect: `build/qemu-disk-pass2.log`

**Step 1: Tighten checks**

Require new markers:
- `fault: trigger`
- trap log for the faulting user task
- `init: fault exit 0x0000008E`

**Step 2: Run verification**

Run:
- `make`
- `make check`
- `make check-disk`
- `make check-disk-persist`

Expected result:
- the system survives the user fault
- init prints the fault child’s exit code
- worker tasks continue running

**Step 3: Update repository state**

Document that:
- user faults now kill the process, not the whole system
- kernel faults still stop the machine
- copy-on-write and guard pages remain future work
