.section .text
.code32

.global _start
.extern main
.extern exit

_start:
    xorl %ebp, %ebp
    movl (%esp), %eax
    leal 4(%esp), %edx
    pushl %edx
    pushl %eax
    call main
    pushl %eax
    call exit

halt_forever:
    hlt
    jmp halt_forever
