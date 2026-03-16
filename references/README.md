# Reference Set

These files were downloaded into the workspace to guide `riverix`.

## Historical Unix documentation

- `tuhs/v6_setup.txt`
- `tuhs/v7_setup.txt`
- `tuhs/v7_regen.txt`
- `tuhs/squoze_v6_installation.html`
- `tuhs/squoze_v7_installation.html`

These are design and setup references for classic Unix systems. They are here to show
system shape, bootstrap flow, device naming, filesystem layout, and rebuild workflow.
They are not vendored implementation code for `riverix`.

## Modern clearly-licensed teaching kernel

- `xv6/LICENSE`
- `xv6/xv6-riscv.tar.gz`
- extracted `xv6-riscv/`

`xv6-riscv` is a modern teaching operating system from MIT. It is included as a
clearly-licensed structural reference for process, memory, trap, and filesystem design.

## Bootstrapping references

- `osdev/Bare_Bones.html`
- `osdev/Serial_Ports.html`

These are practical bootstrapping references for Multiboot, GRUB, and early serial
output on x86.

## Policy for this repository

- Use the historical material for architecture and workflow understanding.
- Use clearly-licensed teaching code as a structural reference only.
- Keep `riverix` implementation original and intentionally small.
