# Kernel/User ABI

This document records the current Riverix syscall ABI as of Phase 4. The goal is
stability for the existing numbers and honest documentation of the current
semantics and limits.

## Calling convention

- Entry: `int 0x80`
- Return value: `eax`
- Arguments:
  - arg0: `ebx`
  - arg1: `ecx`
  - arg2: `edx`
- Error reporting: most syscalls currently return `-1` for failure

## Syscall numbers

| Number | Name     | Arguments                              | Return |
|--------|----------|----------------------------------------|--------|
| 1      | `write`  | `fd`, `buffer`, `length`               | bytes written or `-1` |
| 2      | `getpid` | none                                   | pid |
| 3      | `yield`  | none                                   | `0` or `-1` |
| 4      | `exit`   | `status`                               | does not return on success |
| 5      | `waitpid`| `pid`, `status_ptr`                    | child pid or `-1` |
| 6      | `exec`   | `path`                                 | does not return on success |
| 7      | `fork`   | none                                   | child pid in parent, `0` in child, `-1` on failure |
| 8      | `open`   | `path`, `flags`                        | fd or `-1` |
| 9      | `close`  | `fd`                                   | `0` or `-1` |
| 10     | `read`   | `fd`, `buffer`, `length`               | bytes read or `-1` |
| 11     | `lseek`  | `fd`, `offset`, `whence`               | new offset or `-1` |
| 12     | `mkdir`  | `path`                                 | `0` or `-1` |
| 13     | `unlink` | `path`                                 | `0` or `-1` |
| 14     | `stat`   | `path`, `stat_ptr`                     | `0` or `-1` |
| 15     | `chdir`  | `path`                                 | `0` or `-1` |
| 16     | `dup`    | `fd`                                   | new fd or `-1` |
| 17     | `dup2`   | `oldfd`, `newfd`                       | `newfd` or `-1` |
| 18     | `sleep`  | `ticks`                                | `0` or `-1` |
| 19     | `ticks`  | none                                   | current PIT tick count |

## Flags and structs

### `open` flags

- `SYS_O_RDONLY = 0x1`
- `SYS_O_WRONLY = 0x2`
- `SYS_O_RDWR = 0x3`
- `SYS_O_CREATE = 0x4`
- `SYS_O_TRUNC = 0x8`

Notes:

- At least one access bit must be present.
- `SYS_O_TRUNC` is only meaningful on writable opens.
- Riverix currently has one fd table per process and shared open-file-description
  semantics across `fork`, `dup`, and `dup2`.

### `lseek` whence

- `SYS_SEEK_SET = 0`
- `SYS_SEEK_CUR = 1`
- `SYS_SEEK_END = 2`

### `sys_stat_t`

```c
typedef struct sys_stat {
    uint32_t kind;
    uint32_t size;
    uint32_t child_count;
} sys_stat_t;
```

Current `kind` values:

- `1`: device
- `2`: regular file
- `3`: directory

## Path semantics

- Absolute and relative paths are supported.
- Each process has a cwd string.
- `cwd` defaults to `/`.
- `cwd` is inherited across `fork`.
- `cwd` is preserved across `exec`.
- `.` and `..` are supported.
- Duplicate `/` separators are normalized.

## Current behavior notes

- `fd 0`, `fd 1`, and `fd 2` are attached to `/dev/console` at task creation.
- The ISO boot path mounts the rootfs read-only, so `open(..., O_CREATE)`,
  `mkdir`, `unlink`, and truncating writes fail there by design.
- The disk-image boot path mounts `simplefs` read-write and supports file create,
  write, truncate, seek, readback, unlink, and empty-directory removal.
- `unlink` currently refuses removal of open files instead of implementing
  deferred Unix-style unlink semantics.
- Directory file descriptors are not supported yet.
- `exec` currently accepts only static ELF32 executables from the mounted rootfs.
- `sleep` uses PIT ticks. The current kernel programs the PIT at 100 Hz.
- Kernel entry on behalf of user tasks is treated as non-preemptible until the
  syscall or trap path finishes. User-mode execution remains timer-preemptible.

## Phase 4 proof coverage

The shipped `/bin/phase4` program proves:

- `chdir` plus relative-path `open`
- `read` plus `lseek`
- `stat`
- `ticks` plus `sleep`
- disk-path-only `mkdir`, writable `open`, `unlink`, and empty-directory removal
- shared fd semantics through `dup` and `dup2`

The shipped `/bin/init` runs the full Phase 1 through Phase 4 proof sequence and
then exits cleanly so the kernel can continue on worker threads and idle.
