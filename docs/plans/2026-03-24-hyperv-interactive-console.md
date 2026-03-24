# Hyper-V Interactive Console Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make Riverix’s new backend-driven console architecture capable of supporting real Hyper-V Gen2 local keyboard input by adding the guest-side Hyper-V platform foundation and then the synthetic keyboard backend on top of it.

**Architecture:** Split the work into two clear layers. First add reusable Hyper-V guest support: CPUID-based feature discovery, guest OS registration, hypercall-page setup, and a narrow hypervisor helper API. Then build the VMBus/synthetic-keyboard path on top of that foundation and feed keystrokes into the existing input queue instead of inventing another console path.

**Tech Stack:** freestanding C, x86 CPUID/MSR access, current Riverix paging/palloc/input subsystems, Hyper-V synthetic device model, VMBus-style shared-memory channels

---

### Task 1: Hyper-V Phase Plan And Scope

**Files:**
- Create: `docs/plans/2026-03-24-hyperv-interactive-console.md`
- Modify: `docs/riverix-major-roadmap.md`
- Modify: `README.md`

**Step 1: Freeze the current phase goal**

- State clearly that this phase is about keyboard interactivity on Hyper-V Gen2.
- State explicitly that mouse/pointer support is not part of this slice because Riverix still has no pointer/UI stack.

**Step 2: Split the work into durable layers**

- Layer A: Hyper-V guest core
- Layer B: VMBus/control path
- Layer C: synthetic keyboard backend
- Layer D: verification and shell usability on Hyper-V

### Task 2: Hyper-V Guest Core

**Files:**
- Create: `include/kernel/cpu.h`
- Create: `include/kernel/hyperv.h`
- Create: `src/kernel/hyperv.c`
- Modify: `src/kernel/platform.c`
- Modify: `src/kernel/kernel.c`
- Modify: `Makefile`

**Step 1: Centralize low-level CPU helpers**

- Add reusable inline helpers for:
  - `cpuid`
  - `rdmsr`
  - `wrmsr`

**Step 2: Add Hyper-V guest detection and state**

- Keep `platform` as the public “what machine class am I on?” layer.
- Add `hyperv` as the guest-side implementation layer used only when `platform_is_hyperv()` is true.

**Step 3: Register Riverix with Hyper-V**

- Generate a nonzero guest OS ID for Riverix.
- Program `HV_X64_MSR_GUEST_OS_ID`.
- Allocate and enable a hypercall page through `HV_X64_MSR_HYPERCALL`.

**Step 4: Expose a narrow helper API**

- Add helpers like:
  - `hyperv_present()`
  - `hyperv_hypercall_ready()`
  - `hyperv_do_hypercall(...)`
  - `hyperv_vp_index()`

### Task 3: Hyper-V Control-Path Foundation

**Files:**
- Create: `include/kernel/vmbus.h`
- Create: `src/kernel/vmbus.c`
- Modify: `src/kernel/hyperv.c`
- Modify: `src/kernel/input.c`
- Modify: `Makefile`

**Step 1: Add the minimal control-path data types**

- Define the message and offer structures Riverix needs for:
  - VMBus connect/init
  - channel offers
  - open/close bookkeeping for the keyboard device

**Step 2: Keep the first version poll-driven**

- Do not block on full synthetic-interrupt routing in the first cut.
- Poll control-path messages and channel rings from the scheduler/input path so the feature fits the current single-core kernel.

### Task 4: Synthetic Keyboard Backend

**Files:**
- Modify: `src/kernel/input.c`
- Create: `src/kernel/hyperv_keyboard.c`
- Create: `include/kernel/hyperv_keyboard.h`
- Modify: `Makefile`

**Step 1: Match the keyboard class GUID**

- Identify the Hyper-V synthetic keyboard offer by GUID.
- Open the device channel and negotiate the simple keyboard protocol.

**Step 2: Convert keyboard events into Riverix console characters**

- Handle:
  - press/release state
  - shift/caps
  - enter
  - backspace
  - punctuation needed for shell use

**Step 3: Feed the shared input queue**

- Do not add a second console path.
- Route Hyper-V keyboard bytes into the same input queue already used by serial and i8042.

### Task 5: Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/riverix-major-roadmap.md`

**Step 1: Keep the existing boot matrix green**

- Run:
  - `make check`
  - `make check-disk`

**Step 2: Add manual Hyper-V acceptance criteria**

- Hyper-V Gen2 boots visibly.
- `/bin/init` completes and hands off to `sh`.
- The local VM window can type into the `riverix` shell prompt.

**Step 3: Record the exact remaining gap if not fully done**

- If keyboard works but mouse still does not, document that explicitly instead of implying otherwise.

## Status

- Layer A is implemented: Hyper-V guest detection, guest OS registration, hypercall-page setup, SynIC message-page setup, and narrow post-message/signal-event helpers now exist.
- Layer B is implemented in a minimal poll-driven form: Riverix now negotiates VMBus, requests offers, discovers the keyboard offer, and opens a ring-backed channel without depending on synthetic interrupts.
- Layer C is implemented: the Hyper-V synthetic keyboard protocol is negotiated and keyboard events are routed into the existing `/dev/console` input queue.
- The existing QEMU automated matrix stays green after this work.
- The remaining gate is manual Hyper-V Gen2 validation of local shell typing. Mouse/pointer support remains out of scope for this slice.
