#!/usr/bin/env bash
set -euo pipefail

# Keep this file LF-terminated so it executes correctly from WSL checkouts on /mnt/*.

usage() {
    cat >&2 <<'EOF'
usage:
  install_disk_image.sh \
    --output <disk.img> \
    --grub-config <grub.cfg> \
    --grub-efi <BOOTX64.EFI> \
    --kernel <kernel.elf> \
    --rootfs <rootfs.img> \
    --esp-start-sector <sector> \
    --esp-size-kib <kib> \
    --rootfs-start-sector <sector> \
    --rootfs-size-sectors <sectors> \
    --rootfs-partition-label <label> \
    --esp-label <label>

This assembles a GPT/EFI raw disk image using only user-space tools, which keeps
the workflow WSL-friendly and avoids loop devices or root-only mounts.
EOF
}

require_file() {
    local path="$1"

    if [[ ! -f "$path" ]]; then
        printf 'missing required file: %s\n' "$path" >&2
        exit 1
    fi
}

require_command() {
    local command_name="$1"

    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf 'missing required command: %s\n' "$command_name" >&2
        exit 1
    fi
}

main() {
    local output=""
    local grub_config=""
    local grub_efi=""
    local kernel=""
    local rootfs=""
    local esp_start_sector=""
    local esp_size_kib=""
    local rootfs_start_sector=""
    local rootfs_size_sectors=""
    local rootfs_partition_label=""
    local esp_label=""
    local esp_offset_bytes=""
    local rootfs_size_bytes=""
    local max_rootfs_bytes=""
    local output_dir=""
    local sgdisk_bin="${SGDISK:-sgdisk}"
    local mkfs_vfat_bin="${MKFS_VFAT:-mkfs.vfat}"
    local mmd_bin="${MMD:-mmd}"
    local mcopy_bin="${MCOPY:-mcopy}"
    local dd_bin="${DD:-dd}"
    local truncate_bin="${TRUNCATE:-truncate}"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --output)
                output="${2:-}"
                shift 2
                ;;
            --grub-config)
                grub_config="${2:-}"
                shift 2
                ;;
            --grub-efi)
                grub_efi="${2:-}"
                shift 2
                ;;
            --kernel)
                kernel="${2:-}"
                shift 2
                ;;
            --rootfs)
                rootfs="${2:-}"
                shift 2
                ;;
            --esp-start-sector)
                esp_start_sector="${2:-}"
                shift 2
                ;;
            --esp-size-kib)
                esp_size_kib="${2:-}"
                shift 2
                ;;
            --rootfs-start-sector)
                rootfs_start_sector="${2:-}"
                shift 2
                ;;
            --rootfs-size-sectors)
                rootfs_size_sectors="${2:-}"
                shift 2
                ;;
            --rootfs-partition-label)
                rootfs_partition_label="${2:-}"
                shift 2
                ;;
            --esp-label)
                esp_label="${2:-}"
                shift 2
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            *)
                printf 'unknown argument: %s\n' "$1" >&2
                usage
                exit 1
                ;;
        esac
    done

    if [[ -z "$output" || -z "$grub_config" || -z "$grub_efi" || -z "$kernel" || -z "$rootfs" || -z "$esp_start_sector" || -z "$esp_size_kib" || -z "$rootfs_start_sector" || -z "$rootfs_size_sectors" || -z "$rootfs_partition_label" || -z "$esp_label" ]]; then
        usage
        exit 1
    fi

    require_file "$grub_config"
    require_file "$grub_efi"
    require_file "$kernel"
    require_file "$rootfs"
    require_command "$sgdisk_bin"
    require_command "$mkfs_vfat_bin"
    require_command "$mmd_bin"
    require_command "$mcopy_bin"
    require_command "$dd_bin"
    require_command "$truncate_bin"

    esp_offset_bytes=$(( esp_start_sector * 512 ))
    rootfs_size_bytes=$(stat -c %s "$rootfs")
    max_rootfs_bytes=$(( rootfs_size_sectors * 512 ))

    if (( rootfs_size_bytes > max_rootfs_bytes )); then
        printf 'rootfs image %s (%s bytes) exceeds partition capacity (%s bytes)\n' "$rootfs" "$rootfs_size_bytes" "$max_rootfs_bytes" >&2
        exit 1
    fi

    output_dir=$(dirname "$output")
    mkdir -p "$output_dir"
    rm -f "$output"

    "$truncate_bin" -s 128M "$output"
    "$sgdisk_bin" -o \
        -n 1:"$esp_start_sector":+64M \
        -t 1:ef00 \
        -c 1:"EFI System" \
        -n 2:"$rootfs_start_sector":+16M \
        -t 2:8300 \
        -c 2:"$rootfs_partition_label" \
        "$output" >/dev/null

    "$mkfs_vfat_bin" -F 32 -n "$esp_label" --offset="$esp_start_sector" -h "$esp_start_sector" "$output" "$esp_size_kib" >/dev/null
    "$dd_bin" if="$rootfs" of="$output" bs=512 seek="$rootfs_start_sector" conv=notrunc status=none

    "$mmd_bin" -i "$output@@$esp_offset_bytes" ::/EFI
    "$mmd_bin" -i "$output@@$esp_offset_bytes" ::/EFI/BOOT
    "$mmd_bin" -i "$output@@$esp_offset_bytes" ::/boot
    "$mmd_bin" -i "$output@@$esp_offset_bytes" ::/boot/grub

    "$mcopy_bin" -i "$output@@$esp_offset_bytes" "$grub_efi" ::/EFI/BOOT/BOOTX64.EFI
    "$mcopy_bin" -i "$output@@$esp_offset_bytes" "$kernel" ::/boot/kernel.elf
    "$mcopy_bin" -i "$output@@$esp_offset_bytes" "$rootfs" ::/boot/rootfs.img
    "$mcopy_bin" -i "$output@@$esp_offset_bytes" "$grub_config" ::/boot/grub/grub.cfg
}

main "$@"
