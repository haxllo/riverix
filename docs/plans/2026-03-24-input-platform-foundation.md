# Input And Platform Foundation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace Riverix's UART-only console input path with a real kernel input subsystem that can support multiple backends, and lay the platform groundwork needed for later Hyper-V synthetic keyboard support.

**Architecture:** Add a shared kernel input queue that `/dev/console` reads from instead of polling COM1 directly. Feed that queue from backend pollers, starting with serial and i8042 keyboard input, while also adding platform detection so Hyper-V can be identified and handled explicitly rather than through incidental behavior.

**Tech Stack:** freestanding C, x86 port I/O, current Riverix VFS/proc scheduler, Multiboot boot flow

---

### Task 1: Plan And Platform Scope

**Files:**
- Create: `docs/plans/2026-03-24-input-platform-foundation.md`
- Modify: `docs/riverix-major-roadmap.md`
- Modify: `README.md`

**Step 1: Record the long-term input architecture**

- Document that the long-term design is:
  - shared input core
  - backend-driven polling/interrupt sources
  - `/dev/console` decoupled from any one transport
  - Hyper-V synthetic keyboard as a later backend, not a one-off hack in VFS

**Step 2: Record current and near-term platform truth**

- Document that:
  - framebuffer output now works on Hyper-V
  - local interactive input still needs dedicated backend support
  - i8042 is the first non-UART backend because it fits the existing kernel cleanly
  - Hyper-V Gen2 keyboard support needs a later VMBus/synthetic-device slice

### Task 2: Shared Input Core

**Files:**
- Create: `include/kernel/input.h`
- Create: `src/kernel/input.c`
- Modify: `Makefile`
- Modify: `src/kernel/kernel.c`

**Step 1: Add the kernel input API**

- Add:
  - `input_init()`
  - `input_poll()`
  - `input_try_read_char(char *out_ch)`
  - `input_has_backend()`
  - `input_backend_flags()`

**Step 2: Implement the shared ring buffer**

- Add a fixed-size ring buffer for cooked console characters.
- Protect queue operations by masking interrupts because the kernel is still single-core and may later feed the queue from interrupt context.

**Step 3: Initialize input during boot**

- Initialize the input subsystem during kernel bring-up after the console and platform state are ready.
- Log which backends are active so boot logs make platform state obvious.

### Task 3: Backend-Driven Console Input

**Files:**
- Modify: `src/kernel/vfs.c`
- Modify: `src/kernel/proc.c`

**Step 1: Route `/dev/console` reads through the input subsystem**

- Replace direct `serial_can_read()` / `serial_read_char()` polling in `console_inode_read()`.
- Preserve the current shell-facing behavior:
  - `\r` to `\n`
  - backspace editing
  - local echo
  - `VFS_ERR_WOULD_BLOCK` when no input is available

**Step 2: Poll input as part of scheduler servicing**

- Call `input_poll()` in the scheduler path before retrying blocked reads.
- This keeps blocked console reads progressing even on platforms that do not provide the exact old UART-only behavior.

### Task 4: First Real Keyboard Backend

**Files:**
- Modify: `src/kernel/input.c`
- Modify: `include/kernel/io.h` only if needed

**Step 1: Add serial as one backend, not the backend**

- Move UART byte collection behind the input subsystem.
- Keep COM1 support as a backend provider instead of a VFS special case.

**Step 2: Add an i8042 keyboard backend**

- Probe the legacy controller conservatively.
- Poll scancodes from the data/status ports.
- Translate a practical subset of set-1 scancodes into cooked console characters.
- Handle:
  - letters
  - digits
  - punctuation needed for shell use
  - enter
  - backspace
  - tab
  - shift/caps state

**Step 3: Keep unsupported keys harmless**

- Ignore unsupported extended scancodes cleanly.
- Do not let unknown bytes leak raw garbage into the shell.

### Task 5: Platform Detection Foundation

**Files:**
- Create: `include/kernel/platform.h`
- Create: `src/kernel/platform.c`
- Modify: `Makefile`
- Modify: `src/kernel/kernel.c`
- Modify: `README.md`

**Step 1: Detect hypervisor/platform identity**

- Add CPUID-based detection for:
  - no hypervisor
  - Microsoft Hyper-V
  - unknown hypervisor

**Step 2: Expose minimal platform state**

- Add helpers like:
  - `platform_init()`
  - `platform_name()`
  - `platform_is_hyperv()`

**Step 3: Log explicit platform state**

- Make boot logs say what platform Riverix thinks it is on.
- On Hyper-V, log that framebuffer output is live but the synthetic keyboard backend is still a later slice.

### Task 6: Verification And Documentation

**Files:**
- Modify: `Makefile`
- Modify: `README.md`
- Modify: `docs/riverix-major-roadmap.md`

**Step 1: Add a boot verification marker**

- Add a stable input log marker such as `input: ready backends 0x...` to the automated checks.

**Step 2: Run verification**

- Run:
  - `make check`
  - `make check-disk`

**Step 3: Document the remaining gap honestly**

- State explicitly:
  - the UART-only architecture is gone
  - i8042 keyboard support exists
  - Hyper-V Gen2 still needs the synthetic keyboard backend over the later platform bus layer

