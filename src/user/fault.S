#include "syscall.inc"

.section .text
.code32

.global _start

_start:
    mov $fault_banner, %ecx
    mov $(fault_banner_end - fault_banner), %edx
    call write_stdout

    mov $0x00900000, %eax
    mov (%eax), %ebx

    mov $survived_banner, %ecx
    mov $(survived_banner_end - survived_banner), %edx
    call write_stdout

    mov $SYS_EXIT, %eax
    mov $1, %ebx
    int $0x80

yield_forever:
    mov $SYS_YIELD, %eax
    int $0x80
    jmp yield_forever

write_stdout:
    push %ebx
    mov $SYS_WRITE, %eax
    mov $1, %ebx
    int $0x80
    pop %ebx
    ret

.section .rodata
fault_banner:
    .ascii "fault: trigger\n"
fault_banner_end:
survived_banner:
    .ascii "fault: survived\n"
survived_banner_end:

.section .note.GNU-stack,"",@progbits
