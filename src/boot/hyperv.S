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

.global hyperv_fast_hypercall8_asm
.type hyperv_fast_hypercall8_asm, @function
hyperv_fast_hypercall8_asm:
    push %ebx
    mov 12(%esp), %eax
    mov 16(%esp), %edx
    mov 20(%esp), %ecx
    mov 24(%esp), %ebx
    call *8(%esp)
    pop %ebx
    ret

.section .note.GNU-stack,"",@progbits
