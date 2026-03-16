#include "syscall.inc"

.section .text
.code32

.global _start

_start:
    mov $child_banner, %ecx
    mov $(child_banner_end - child_banner), %edx
    call write_stdout

    mov $SYS_EXIT, %eax
    mov $0x2A, %ebx
    int $0x80

    mov $exit_failed_banner, %ecx
    mov $(exit_failed_banner_end - exit_failed_banner), %edx
    call write_stdout
    jmp yield_forever

write_stdout:
    push %ebx
    mov $SYS_WRITE, %eax
    mov $1, %ebx
    int $0x80
    pop %ebx
    ret

yield_forever:
    mov $SYS_YIELD, %eax
    int $0x80
    jmp yield_forever

.section .rodata
child_banner:
    .ascii "child: online\n"
child_banner_end:
exit_failed_banner:
    .ascii "child: exit failed\n"
exit_failed_banner_end:

.section .note.GNU-stack,"",@progbits
