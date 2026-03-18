# Disk Layout

`riverix` now builds two install-style GPT/EFI raw disk images:

- `make disk-image` builds the normal installed image at `build/riverix-disk.img`
- `make recovery-disk-image` builds a recovery-first image at `build/riverix-recovery-disk.img`

Both images use the same layout and filesystem contents. The only difference is the
default GRUB configuration copied into the EFI System Partition.

## Partition map

1. EFI System Partition
   - GPT type: `ef00`
   - Start sector: `2048`
   - Size: `64 MiB`
   - Filesystem: FAT32
   - Label: `RIVERIX`
2. Root filesystem partition
   - GPT type: `8300`
   - Start sector: `133120`
   - Size: `16 MiB`
   - Filesystem: `simplefs`
   - Partition label: `riverix-rootfs`

The kernel's GPT discovery currently looks for the label `riverix-rootfs`, so that label
is part of the installed-system contract.

## Installed ESP contents

The image installer copies these files into the EFI System Partition:

- `/EFI/BOOT/BOOTX64.EFI`
- `/boot/kernel.elf`
- `/boot/rootfs.img`
- `/boot/grub/grub.cfg`

`/boot/rootfs.img` is present on disk images so recovery boot can mount a known-good
ramdisk rootfs even when the persistent rootfs partition is damaged or intentionally
skipped.

## Boot modes

Normal installed image:

- GRUB default entry passes `root=disk`
- The kernel mounts `riverix-rootfs` from the disk partition
- The GRUB menu also exposes a recovery entry that passes `root=ramdisk recovery=1`

Recovery image:

- GRUB default entry passes `root=ramdisk recovery=1`
- GRUB loads `/boot/rootfs.img` as the Multiboot `rootfs` module
- The kernel mounts the ramdisk rootfs and keeps the writable disk path out of the boot
  decision

The `recovery=1` flag now drives real recovery-specific userspace behavior: the kernel
still uses the explicit `root=` policy for rootfs selection, and `/bin/init` now detects
the recovery flag and runs `/etc/rc-recovery` before handing off to an interactive shell.

## Host-side install flow

The raw image build is handled by `tools/install_disk_image.sh`. It uses only user-space
tools:

- `sgdisk`
- `mkfs.vfat`
- `mtools`
- `dd`
- `truncate`

That keeps the flow WSL-friendly because it does not require loop devices, root-only
mounts, or Linux block-device plumbing.

Typical usage:

```bash
make disk-image
make recovery-disk-image
make install-image OUTPUT=/tmp/riverix-custom.img
```

To build a custom recovery-first image at an arbitrary path:

```bash
make install-image OUTPUT=/tmp/riverix-recovery.img INSTALL_GRUB_CONFIG=grub/grub-disk-recovery.cfg
```

## Verification targets

```bash
make check-disk
make check-disk-recovery
make check-disk-persist
```

- `check-disk` proves disk-root boot from the installed image
- `check-disk-recovery` proves ramdisk-root recovery boot from a disk image
- `check-disk-persist` proves the writable rootfs survives reboot
