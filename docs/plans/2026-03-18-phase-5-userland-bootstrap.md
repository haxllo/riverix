# Phase 5 Userland Bootstrap Plan

## Goal

Replace the current proof-only `/bin/init` path with a real userland bootstrap:

- keep the existing assembly selftests as regression programs
- add a small C userland toolchain and libc
- boot through a real `/bin/init`
- run a shell script through `/bin/sh`
- leave an interactive shell on `/dev/console`

The result should be a usable minimal Unix-like environment instead of a single hardcoded
test harness.

## Scope

This phase includes:

- a shared syscall ABI header used by both kernel and userland
- serial-backed `/dev/console` input
- `execv` with `argc`/`argv` stack setup
- directory enumeration and process enumeration syscalls for `ls` and `ps`
- a freestanding static C userland runtime
- `/bin/init`, `/bin/sh`, and core tools
- rootfs script and text payloads needed to bootstrap userland

This phase does not include:

- a tty subsystem
- job control
- pipes
- signals
- a package manager
- dynamic linking

## Architecture

### 1. Preserve the existing proof programs

The current assembly `/bin/init` should be moved to `/bin/selftest` rather than deleted.
That keeps the Phase 1 through Phase 4 regression coverage intact while letting Phase 5
install a new C `/bin/init`.

### 2. Share the syscall contract cleanly

Move syscall numbers, open flags, seek constants, and user-visible structs into a shared
header under `include/shared/`. Kernel wrappers and user wrappers should both depend on
that header rather than duplicating numbers in assembly and C.

### 3. Keep the first console input path small but real

Use COM1 polling for receive support and make `/dev/console` return line-buffered reads.
That is sufficient for `/bin/sh` over QEMU `-serial stdio` without introducing a tty layer
too early.

### 4. Add the missing ABI for real shell execution

The shell needs:

- `execv(path, argv)`
- directory entry enumeration
- process enumeration

Those should be added as narrow syscalls rather than bending the existing ABI into
shell-specific hacks.

### 5. Make the first C userland static and freestanding

Add:

- `crt0.S`
- a user linker script with RX and RW load segments
- a small libc with syscall wrappers and string/memory helpers

Keep user programs statically linked and avoid any host libc dependency.

### 6. Bootstrap through scriptable init

The new `/bin/init` should:

1. run `/bin/selftest` and wait for it
2. run `/bin/sh /etc/rc` and wait for it
3. `execv("/bin/sh", ["sh", 0])` for the interactive shell

This preserves automated regression coverage while moving the steady-state system onto a
real shell.

## Implementation Breakdown

### Part A. Kernel ABI and input

- add `include/shared/syscall_abi.h`
- update kernel syscall headers and dispatch to use the shared ABI
- add COM1 RX polling in `serial.c`
- make `/dev/console` readable from serial input
- add user-visible directory and process listing syscalls

### Part B. Exec arguments

- add `execv`
- copy path and argument strings from userspace into kernel buffers
- build `argc`/`argv` on the initial user stack
- keep `exec(path)` as a compatibility wrapper over `execv`
- update task naming on `exec` so `ps` reflects the active image

### Part C. Userland build/runtime

- add a generic user linker script with text/rodata and data/bss segments
- add `crt0.S` and libc objects
- support mixed assembly and C user programs in `Makefile`
- keep the old assembly proof binaries buildable

### Part D. Rootfs content

- extend rootfs generation so it can ship `/etc/rc` and `/etc/motd`
- keep `/bin/*` ELF packaging intact

### Part E. Programs

- `/bin/init`
- `/bin/sh`
- `/bin/echo`
- `/bin/ls`
- `/bin/cat`
- `/bin/mkdir`
- `/bin/rm`
- `/bin/ps`

The first shell should support:

- script mode via `sh /etc/rc`
- interactive mode via stdin
- builtins: `cd`, `exit`
- launching `/bin/<name>` for bare commands
- simple `>` stdout redirection

No quoting, pipes, or globbing in this phase.

## Verification

`make check` and `make check-disk` should prove:

- the Phase 1 through Phase 4 selftest still runs
- the shell script runs from `/etc/rc`
- the shell launches multiple user programs
- `ls`, `cat`, and `ps` produce output
- on the disk path, shell-driven `mkdir`, `rm`, and redirected file creation work

`make check-disk-persist` should keep proving the writable disk path survives reboot.

## Exit Criterion

The system boots into a real `/bin/init`, uses `/bin/sh` to run a startup script, and can
be used from a minimal interactive shell with real external tools instead of only from a
single hardcoded proof binary.
