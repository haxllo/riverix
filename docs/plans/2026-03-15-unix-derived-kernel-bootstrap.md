# Unix-Derived Kernel Bootstrap Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a bootable Unix-inspired kernel baseline that can compile, boot under QEMU, print diagnostics, and serve as the foundation for memory management, interrupts, processes, and a filesystem.

**Architecture:** Use a 32-bit x86 Multiboot 1 kernel with GRUB handling the loader path. Keep the repository freestanding and minimal: one assembly entry point, one linker script, a tiny console stack, and no imported historical Unix code. Grow the system in Unix order after boot: memory, traps, scheduler, syscalls, then VFS and storage.

**Tech Stack:** C11, GNU assembler via `.S`, GNU ld, GRUB Multiboot, QEMU i386, Make

---

### Task 1: Create the repository skeleton and reference index

**Files:**
- Create: `README.md`
- Create: `references/README.md`
- Create: `.gitignore`

**Step 1: Write the failing test**

Run: `make`
Expected: FAIL because no `Makefile` exists yet.

**Step 2: Add project metadata**

- Document the project goal, current milestone, and build prerequisites in `README.md`.
- Document what was downloaded into `references/README.md` and why it is safe to use.
- Ignore the generated `build/` directory in `.gitignore`.

**Step 3: Verify the docs exist**

Run: `find . -maxdepth 2 -type f | sort`
Expected: the new documentation files appear.

**Step 4: Commit**

```bash
git add README.md references/README.md .gitignore
git commit -m "docs: add kernel bootstrap metadata"
```

### Task 2: Add the boot pipeline and linker layout

**Files:**
- Create: `Makefile`
- Create: `linker.ld`
- Create: `grub/grub.cfg`
- Create: `src/boot/boot.S`

**Step 1: Write the failing test**

Run: `make build/kernel.elf`
Expected: FAIL because boot entry and linker files do not exist.

**Step 2: Add the boot path**

- Add a Multiboot header in `src/boot/boot.S`.
- Define `_start`, install a temporary stack, and call `kernel_main`.
- Create `linker.ld` to place the kernel at 1 MiB with `.multiboot` near the front.
- Add `grub/grub.cfg` with one `multiboot /boot/kernel.elf` entry.
- Add `Makefile` rules to compile freestanding 32-bit objects, link `build/kernel.elf`, and package `build/riverix.iso`.

**Step 3: Run the build**

Run: `make`
Expected: either a successful kernel link or a failure indicating the missing C kernel symbol.

**Step 4: Commit**

```bash
git add Makefile linker.ld grub/grub.cfg src/boot/boot.S
git commit -m "build: add multiboot kernel pipeline"
```

### Task 3: Add early console output

**Files:**
- Create: `include/kernel/console.h`
- Create: `include/kernel/io.h`
- Create: `include/kernel/serial.h`
- Create: `include/kernel/vga.h`
- Create: `src/kernel/console.c`
- Create: `src/kernel/serial.c`
- Create: `src/kernel/vga.c`
- Create: `src/kernel/kernel.c`

**Step 1: Write the failing test**

Run: `make`
Expected: FAIL because `kernel_main` and console symbols do not exist.

**Step 2: Add minimal implementation**

- Implement COM1 serial initialization and byte output.
- Implement VGA text mode output.
- Implement a tiny console layer that mirrors output to both backends.
- Implement `kernel_main` to print a banner, the Multiboot magic value, and the Multiboot info pointer before halting.

**Step 3: Run the build**

Run: `make`
Expected: PASS with `build/kernel.elf` and `build/riverix.iso` created.

**Step 4: Boot under QEMU**

Run: `make check`
Expected: PASS after the serial log contains `riverix: kernel_main reached`.

**Step 5: Commit**

```bash
git add include src Makefile
git commit -m "feat: boot kernel and print early diagnostics"
```

### Task 4: Parse boot memory information

**Files:**
- Modify: `include/kernel/console.h`
- Create: `include/kernel/multiboot.h`
- Create: `include/kernel/memory.h`
- Create: `src/kernel/memory.c`
- Modify: `src/kernel/kernel.c`
- Test: `build/qemu.log`

**Step 1: Write the failing test**

Run: `make check`
Expected: PASS on boot but no memory region information in the serial log.

**Step 2: Add minimal implementation**

- Define the subset of Multiboot structures needed for flags, memlower/memupper, and memory map walking.
- Print detected usable and reserved memory regions to the console.
- Keep the parser read-only at this stage.

**Step 3: Run the verification**

Run: `make check`
Expected: PASS with memory information printed to `build/qemu.log`.

**Step 4: Commit**

```bash
git add include/kernel/multiboot.h include/kernel/memory.h src/kernel/memory.c src/kernel/kernel.c
git commit -m "feat: report multiboot memory map"
```

### Task 5: Add interrupts and a timer tick

**Files:**
- Create: `include/kernel/gdt.h`
- Create: `include/kernel/idt.h`
- Create: `include/kernel/pic.h`
- Create: `include/kernel/pit.h`
- Create: `src/boot/interrupts.S`
- Create: `src/kernel/gdt.c`
- Create: `src/kernel/idt.c`
- Create: `src/kernel/pic.c`
- Create: `src/kernel/pit.c`
- Modify: `src/kernel/kernel.c`

**Step 1: Write the failing test**

Run: `make check`
Expected: PASS on boot but no periodic timer output.

**Step 2: Add minimal implementation**

- Install a flat kernel GDT.
- Build an IDT with exception stubs and one IRQ0 timer handler.
- Remap the PIC, initialize the PIT, and increment a global tick counter.
- Print a heartbeat every fixed number of ticks.

**Step 3: Run the verification**

Run: `make check`
Expected: PASS with repeated timer heartbeat output in the log.

**Step 4: Commit**

```bash
git add include/kernel/gdt.h include/kernel/idt.h include/kernel/pic.h include/kernel/pit.h src/boot/interrupts.S src/kernel/gdt.c src/kernel/idt.c src/kernel/pic.c src/kernel/pit.c src/kernel/kernel.c
git commit -m "feat: add descriptor tables and timer tick"
```

### Task 6: Build the first Unix-shaped kernel subsystems

**Files:**
- Create: `include/kernel/palloc.h`
- Create: `include/kernel/proc.h`
- Create: `include/kernel/syscall.h`
- Create: `include/kernel/vfs.h`
- Create: `src/kernel/palloc.c`
- Create: `src/kernel/proc.c`
- Create: `src/kernel/syscall.c`
- Create: `src/kernel/vfs.c`
- Modify: `README.md`

**Step 1: Write the failing test**

Run: `make check`
Expected: PASS on boot but no allocator, process table, or VFS initialization messages.

**Step 2: Add minimal implementation**

- Add a bump allocator followed by a page-frame allocator.
- Add a small process table and a kernel task abstraction.
- Define a syscall entry plan even if the handler is still stubbed.
- Define a VFS object model: superblock, inode, dentry, file, ops table.

**Step 3: Run the verification**

Run: `make check`
Expected: PASS with subsystem initialization messages in the serial log.

**Step 4: Commit**

```bash
git add include/kernel/palloc.h include/kernel/proc.h include/kernel/syscall.h include/kernel/vfs.h src/kernel/palloc.c src/kernel/proc.c src/kernel/syscall.c src/kernel/vfs.c README.md
git commit -m "feat: scaffold unix-style core subsystems"
```
