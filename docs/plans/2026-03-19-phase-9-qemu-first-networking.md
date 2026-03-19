# Phase 9 QEMU-First Networking Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Bring up real networking in QEMU by adding one supported NIC path, a minimal Ethernet/ARP/IPv4/ICMP stack, static guest configuration, and userland proof tools.

**Architecture:** Keep networking staged instead of pretending sockets already exist. Add a narrow e1000 driver under the existing PCI/MMIO boot path, layer a tiny kernel network core above it, expose only `netinfo` and blocking `ping4` syscalls to userspace, and prove the path with QEMU user networking against the default gateway `10.0.2.2`.

**Tech Stack:** freestanding C kernel, x86 PCI config I/O, MMIO, DMA rings backed by page allocator, QEMU e1000, staged syscall ABI, C userland tools, rc-script verification

---

### Task 1: Add the kernel networking and e1000 interfaces

**Files:**
- Create: `include/kernel/e1000.h`
- Create: `src/kernel/e1000.c`
- Create: `include/kernel/net.h`
- Create: `src/kernel/net.c`
- Modify: `Makefile`

**Steps:**
1. Define a narrow e1000 driver API for probe/init, transmit, receive polling, and MAC discovery.
2. Define a kernel networking API for boot-time init, periodic polling, `netinfo`, and a staged blocking `ping4` path.
3. Add the new objects to the kernel build without changing existing storage targets yet.

**Verification:**
- Kernel still builds with the new objects linked in.

### Task 2: Implement the QEMU e1000 driver

**Files:**
- Modify: `src/kernel/e1000.c`
- Modify: `include/kernel/e1000.h`
- Modify: `src/kernel/pci.c` only if a small PCI helper is required
- Modify: `include/kernel/pci.h` only if a small PCI helper is required

**Steps:**
1. Probe the first Ethernet-class PCI device and enable bus mastering plus memory space.
2. Map BAR0 with the existing MMIO mapper.
3. Allocate and zero TX/RX rings and packet buffers from physical pages.
4. Initialize the controller in a polling configuration with RX/TX enabled.
5. Implement frame transmit and receive-drain helpers.
6. Log controller discovery, MAC address, and readiness.

**Verification:**
- Boot log shows PCI Ethernet discovery and `e1000: ready`.

### Task 3: Implement the minimal kernel network stack

**Files:**
- Modify: `src/kernel/net.c`
- Modify: `include/kernel/net.h`
- Modify: `src/kernel/kernel.c`
- Modify: `src/kernel/proc.c`
- Modify: `include/kernel/proc.h`

**Steps:**
1. Add static guest configuration for QEMU user networking:
   - address `10.0.2.15`
   - netmask `255.255.255.0`
   - gateway `10.0.2.2`
2. Add Ethernet frame encode/decode for ARP and IPv4.
3. Add ARP request/reply handling with a small cache for the gateway.
4. Add ICMP echo request/reply handling and a single staged pending-ping state.
5. Poll networking from the scheduler path so receive handling progresses without NIC interrupts.
6. Reuse the existing blocked-task syscall pattern so `ping4` sleeps and wakes cleanly instead of spinning in userspace.

**Verification:**
- Kernel can resolve the gateway MAC and complete an ICMP echo exchange.

### Task 4: Expose the staged networking ABI to userspace

**Files:**
- Modify: `include/shared/syscall_abi.h`
- Modify: `src/kernel/syscall.c`
- Modify: `include/user/unistd.h`
- Create: `include/user/net.h`
- Modify: `src/user/libc/syscall.c`

**Steps:**
1. Add `SYS_NETINFO` and `SYS_PING4`.
2. Add shared `sys_netinfo_t` and ping status constants.
3. Add kernel syscall handlers that copy `netinfo` out and block on `ping4`.
4. Add matching libc wrappers and user headers.

**Verification:**
- Small user programs can query network state and issue one ping.

### Task 5: Add the Phase 9 userland tools and boot proof

**Files:**
- Create: `src/user/netinfo.c`
- Create: `src/user/ping.c`
- Modify: `Makefile`
- Modify: `src/rootfs/etc/rc-ro`
- Modify: `src/rootfs/etc/rc-disk`
- Modify: `src/rootfs/etc/rc-recovery`

**Steps:**
1. Add `/bin/netinfo` to print MAC, IPv4, netmask, gateway, and readiness.
2. Add `/bin/ping` that parses dotted IPv4 and calls the staged `ping4` syscall.
3. Package both tools into the rootfs.
4. Add Phase 9 markers to the normal boot scripts:
   - `phase9: net begin`
   - `phase9: netinfo ok`
   - `phase9: ping ok`
   - `phase9: net end`
5. Keep recovery conservative: it can print network state, but it should not become dependent on ping success.

**Verification:**
- Disk and ISO boots log the Phase 9 markers and a successful ping to `10.0.2.2`.

### Task 6: Wire QEMU NIC args, checks, and documentation

**Files:**
- Modify: `Makefile`
- Modify: `README.md`
- Modify: `docs/kernel-user-abi.md`
- Modify: `docs/plans/2026-03-16-riverix-system-roadmap.md`

**Steps:**
1. Add QEMU user-network + e1000 args to the runtime paths that need Phase 9 proof.
2. Add a dedicated `check-net` target or extend the existing checks with the new markers.
3. Document the staged network ABI and the QEMU-first scope.
4. Mark Phase 9 complete in the roadmap only after the full verification matrix passes.

**Verification:**
- `make check`
- `make check-disk`
- `make check-disk-ahci`
- `make check-disk-recovery`
- `make check-disk-reinstall`
- `make check-disk-persist`
