.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEO,    1<<2
.set FLAGS,    ALIGN | MEMINFO | VIDEO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
.long 0
.long 0
.long 0
.long 0
.long 0
.long 0
.long 0
.long 0
.long 0

.section .text
.code32
.global _start
.type _start, @function

_start:
    mov $stack_top, %esp
    push %ebx
    push %eax
    call kernel_main

halt:
    cli
    hlt
    jmp halt

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.section .note.GNU-stack,"",@progbits
