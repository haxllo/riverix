# Phase 10 Robustness And Observability Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Finish the roadmap with a coherent robustness layer: central panic handling, a kernel trace ring with user-visible inspection, an automated soak boot path, and final architecture documentation.

**Architecture:** Keep the implementation additive and staged instead of rewriting the kernel into a new shape. Add one small panic subsystem for fatal kernel halts, one fixed-size trace ring for structured observability, and one explicit soak boot mode that runs a bounded high-churn userland stress program. Use the existing bootinfo, syscall, rc-script, and QEMU verification patterns so the final phase strengthens the architecture instead of bypassing it.

**Tech Stack:** freestanding C kernel, staged syscall ABI, QEMU/OVMF boot flow, serial-log verification, C userland tools, markdown architecture docs

**Status:** Implemented on `main` and verified with `make check`, `make check-disk`,
`make check-disk-ahci`, `make check-disk-recovery`, `make check-disk-reinstall`,
`make check-disk-persist`, and `make check-soak`.

---

### Task 1: Add a central panic path and structured trace core

**Files:**
- Create: `include/kernel/panic.h`
- Create: `src/kernel/panic.c`
- Create: `include/kernel/trace.h`
- Create: `src/kernel/trace.c`
- Modify: `Makefile`
- Modify: `src/kernel/kernel.c`
- Modify: `src/kernel/idt.c`
- Modify: `src/kernel/proc.c`

**Steps:**
1. Add a tiny `panic()` entrypoint that prints a final reason, disables interrupts, and halts.
2. Replace the scattered raw fatal `cli; hlt` loops in the main fatal paths with the shared panic layer.
3. Add a fixed-size trace ring with sequence numbers, PIT ticks, categories, event ids, and three numeric args.
4. Add early `trace_init()` during boot.
5. Add trace hooks for:
   - fatal traps / panic path
   - process lifecycle events like fork/exec/exit
   - rootfs / block bring-up events
   - network ping and reply events
   - one memory-state snapshot during boot

**Verification:**
- Kernel still boots and logs the existing markers.
- Fatal kernel paths now emit `panic:` instead of falling into raw loops.

### Task 2: Expose trace inspection through the syscall ABI

**Files:**
- Modify: `include/shared/syscall_abi.h`
- Modify: `src/kernel/syscall.c`
- Modify: `src/user/libc/syscall.c`
- Create: `include/user/trace.h`

**Steps:**
1. Add `SYS_TRACEINFO` and `SYS_TRACEREAD`.
2. Add shared `sys_trace_info_t` and `sys_trace_record_t`.
3. Add kernel handlers to copy trace metadata and one trace record to userspace.
4. Add libc wrappers and user headers.

**Verification:**
- A user program can confirm the trace ring is populated and can read back the newest record.

### Task 3: Add the Phase 10 user tools

**Files:**
- Create: `src/user/trace.c`
- Create: `src/user/phase10.c`
- Modify: `Makefile`
- Modify: `src/rootfs/etc/rc-ro`
- Modify: `src/rootfs/etc/rc-disk`

**Steps:**
1. Add `/bin/trace` to dump recent trace records in a readable format.
2. Add `/bin/phase10` as a bounded stress program that:
   - exercises pipes
   - exercises fork/wait
   - exercises disk writes on the writable rootfs path
   - exercises `ping`
   - validates that trace records are readable
3. Run the quick Phase 10 proof from the normal rc scripts so the standard checks cover it.

**Verification:**
- ISO and disk boots log `phase10: start`, `phase10: trace ok`, and `phase10: ok`.

### Task 4: Add an explicit soak boot mode and automated soak target

**Files:**
- Modify: `include/kernel/bootinfo.h`
- Modify: `src/kernel/bootinfo.c`
- Modify: `include/shared/syscall_abi.h`
- Modify: `include/user/boot.h`
- Modify: `src/user/init.c`
- Create: `src/rootfs/etc/rc-soak`
- Create: `grub/grub-disk-soak.cfg`
- Modify: `Makefile`

**Steps:**
1. Add a new boot flag for `soak=1`.
2. Teach `/bin/init` to select `/etc/rc-soak` when the flag is set.
3. Add a dedicated soak script that runs `phase10 soak`.
4. Add a dedicated `check-soak` target and soak disk image/GRUB config.
5. Make the soak target assert:
   - soak mode detected
   - multiple iteration markers appear
   - final soak success marker appears
   - no `panic:` marker appears

**Verification:**
- `make check-soak` passes.

### Task 5: Add final architecture docs and close the roadmap

**Files:**
- Create: `docs/subsystems.md`
- Modify: `README.md`
- Modify: `docs/kernel-user-abi.md`
- Modify: `docs/plans/2026-03-16-riverix-system-roadmap.md`

**Steps:**
1. Add a subsystem map that points readers to the boot, VM, storage, VFS, proc, network, panic, and trace files.
2. Document the trace ABI and the soak boot path.
3. Mark Phase 10 complete in the roadmap once all verification gates pass.

**Verification:**
- `make check`
- `make check-disk`
- `make check-disk-ahci`
- `make check-disk-recovery`
- `make check-disk-reinstall`
- `make check-disk-persist`
- `make check-soak`
