.section .text
.code32

.global hyperv_hypercall_asm
.type hyperv_hypercall_asm, @function
hyperv_hypercall_asm:
    push %ebx
    push %esi
    push %edi
    mov 20(%esp), %eax
    mov 24(%esp), %edx
    mov 28(%esp), %ecx
    mov 32(%esp), %ebx
    mov 36(%esp), %esi
    mov 40(%esp), %edi
    call *16(%esp)
    pop %edi
    pop %esi
    pop %ebx
    ret

.section .note.GNU-stack,"",@progbits
