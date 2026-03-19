# Kernel/User ABI

This document records the current Riverix syscall ABI as of the current Phase 9
QEMU-first networking slice. The goal is
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
| 20     | `execv`  | `path`, `argv`                         | does not return on success |
| 21     | `readdir`| `path`, `index`, `dirent_ptr`          | `0` on success, `1` at end, `-1` on error |
| 22     | `procinfo` | `index`, `procinfo_ptr`              | `0` on success, `1` at end, `-1` on error |
| 23     | `bootinfo` | `bootinfo_ptr`                       | `0` or `-1` |
| 24     | `getcwd` | `buffer`, `length`                     | bytes written, excluding the trailing null, or `-1` |
| 25     | `reinstall_rootfs` | none                          | `0` or `-1` |
| 26     | `getuid` | none                                  | current uid |
| 27     | `getgid` | none                                  | current gid |
| 28     | `setuid` | `uid`                                 | `0` or `-1` |
| 29     | `setgid` | `gid`                                 | `0` or `-1` |
| 30     | `setsid` | none                                  | new session id or `-1` |
| 31     | `gettty` | `buffer`, `length`                    | bytes written, excluding the trailing null, or `-1` |
| 32     | `pipe`   | `fds_ptr`                             | `0` or `-1` |
| 33     | `netinfo` | `netinfo_ptr`                        | `0` or `-1` |
| 34     | `ping4`  | `ipv4_addr`, `timeout_ticks`          | `0` on reply, negative staged error on failure |

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
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t links;
} sys_stat_t;
```

Current `kind` values:

- `1`: device
- `2`: regular file
- `3`: directory

### `sys_dirent_t`

```c
typedef struct sys_dirent {
    uint32_t kind;
    uint32_t size;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t links;
    char name[32];
} sys_dirent_t;
```

### `sys_procinfo_t`

```c
typedef struct sys_procinfo {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t sid;
    uint32_t pgid;
    uint32_t state;
    uint32_t kind;
    uint32_t run_ticks;
    uint32_t uid;
    uint32_t gid;
    char name[32];
    char tty[32];
} sys_procinfo_t;
```

Current task-state values:

- `0`: unused
- `1`: runnable
- `2`: running
- `3`: blocked
- `4`: sleeping
- `5`: zombie

Current task-kind values:

- `0`: kernel task
- `1`: user task

### `sys_bootinfo_t`

```c
typedef struct sys_bootinfo {
    uint32_t root_policy;
    uint32_t flags;
} sys_bootinfo_t;
```

Current boot root-policy values:

- `0`: auto
- `1`: disk
- `2`: ramdisk

Current boot flags:

- `0x1`: recovery mode
- `0x2`: scripted reinstall mode

### `sys_netinfo_t`

```c
typedef struct sys_netinfo {
    uint32_t ready;
    uint8_t mac[6];
    uint8_t reserved0[2];
    uint32_t ipv4_addr;
    uint32_t ipv4_netmask;
    uint32_t ipv4_gateway;
    uint32_t arp_valid;
    uint8_t arp_mac[6];
    uint8_t reserved1[2];
    uint32_t arp_ipv4;
    uint32_t rx_packets;
    uint32_t tx_packets;
} sys_netinfo_t;
```

Current staged `ping4` results:

- `0`: echo reply received
- `-1`: timeout
- `-2`: unreachable
- `-3`: networking unavailable
- `-4`: ping already in progress
- `-5`: invalid request

### Mode bits

- `SYS_MODE_IRUSR = 0x100`
- `SYS_MODE_IWUSR = 0x080`
- `SYS_MODE_IXUSR = 0x040`
- `SYS_MODE_IRGRP = 0x020`
- `SYS_MODE_IWGRP = 0x010`
- `SYS_MODE_IXGRP = 0x008`
- `SYS_MODE_IROTH = 0x004`
- `SYS_MODE_IWOTH = 0x002`
- `SYS_MODE_IXOTH = 0x001`
- `SYS_MODE_ISVTX = 0x200`

## Path semantics

- Absolute and relative paths are supported.
- Each process has a cwd string.
- `cwd` defaults to `/`.
- `cwd` is inherited across `fork`.
- `cwd` is preserved across `exec`.
- `getcwd` returns the normalized current cwd string.
- `.` and `..` are supported.
- Duplicate `/` separators are normalized.
- `/dev/tty` is task-relative and resolves against the current task's controlling tty.

## Current behavior notes

- `fd 0`, `fd 1`, and `fd 2` are attached to the controlling console device at task creation.
- `/dev/console` reads now return `WOULD_BLOCK` internally when no serial input is ready, and
  the proc layer turns that into blocking task sleep/resume instead of busy-waiting in kernel mode.
- `pipe()` returns a read end and a write end backed by an in-memory ring buffer.
- `read` and `write` on pipes block in-kernel until data or space becomes available.
- `read` returns EOF when the last pipe writer closes.
- `write` fails when the last pipe reader closes.
- `setsid` creates a new session/process group and detaches the calling task from its controlling tty.
- `setuid` and `setgid` are root-only operations in the current kernel.
- The VFS enforces mode-bit checks for path traversal, open, create, unlink, truncate, and directory mutation.
- Sticky `/tmp` semantics are implemented for unlink inside sticky directories.
- The ISO boot path mounts the rootfs read-only, so `open(..., O_CREATE)`,
  `mkdir`, `unlink`, and truncating writes fail there by design.
- The disk-image boot path mounts `simplefs` read-write and supports file create,
  write, truncate, seek, readback, unlink, and empty-directory removal.
- `unlink` currently refuses removal of open files instead of implementing
  deferred Unix-style unlink semantics.
- Directory file descriptors are not supported yet.
- `exec` is now a compatibility wrapper over `execv(path, NULL)`.
- `execv` builds a simple initial user stack with `argc`, `argv[]`, and a trailing
  null pointer. Environment variables are not supported yet.
- Executables are currently static ELF32 `ET_EXEC` images from the mounted rootfs.
- `sleep` uses PIT ticks. The current kernel programs the PIT at 100 Hz.
- Kernel entry on behalf of user tasks is treated as non-preemptible until the
  syscall or trap path finishes. User-mode execution remains timer-preemptible.
- `bootinfo` exposes the parsed Multiboot boot mode that the kernel uses for
  rootfs policy and recovery-mode decisions.
- `reinstall_rootfs` is only intended to succeed while booted from recovery mode
  on the ramdisk rootfs. It rewrites the full installed `ata0p2` rootfs partition
  from the known-good `rootfs0` ramdisk block device.
- `netinfo` exposes the current staged network state for the single supported
  QEMU-first e1000 path: readiness, MAC, static IPv4 config, one ARP cache entry,
  and simple RX/TX counters.
- `ping4` is intentionally a staged syscall rather than a socket API. It blocks
  the calling task in-kernel until the network poll path sees a matching ICMP echo
  reply or the timeout expires.
- Riverix Phase 9 networking is currently static-config only:
  `10.0.2.15/24` with gateway `10.0.2.2` under QEMU user networking.

## Proof coverage

The shipped `/bin/selftest` program preserves the Phase 1 through Phase 4 kernel proof path:

- `chdir` plus relative-path `open`
- `read` plus `lseek`
- `stat`
- `ticks` plus `sleep`
- disk-path-only `mkdir`, writable `open`, `unlink`, and empty-directory removal
- shared fd semantics through `dup` and `dup2`

The shipped Phase 5 userland bootstrap then adds:

- a real C `/bin/init`
- `execv`-driven startup through `/bin/sh`
- `/etc/rc-ro` on the read-only path
- `/etc/rc-disk` on the writable disk path
- external tools `echo`, `ls`, `cat`, `mkdir`, `rm`, and `ps`
- simple shell stdout redirection with `>`

The current Phase 6 recovery-and-reinstall slice adds:

- `bootinfo` and `getcwd`
- recovery-aware `/bin/init`
- `/etc/rc-recovery` when `recovery=1` is present
- `/etc/rc-reinstall` when `recovery=1 reinstall=1` is present
- user tools `/bin/bootmode`, `/bin/pwd`, and `/bin/reinstall`
- the `reinstall_rootfs` syscall for full rootfs restoration from recovery mode

The current Phase 8 tty/permission/pipeline slice adds:

- `getuid`, `getgid`, `setuid`, `setgid`, `setsid`, `gettty`, and `pipe`
- uid/gid plus session/process-group/tty fields in `procinfo`
- mode, uid, gid, and link-count fields in `stat` and `readdir`
- shell pipeline support with `|`
- user tools `/bin/id`, `/bin/tty`, and `/bin/phase8`
