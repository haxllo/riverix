.section .text
.code32

.extern interrupt_dispatch

.global gdt_load
.type gdt_load, @function
gdt_load:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $gdt_load_flush
gdt_load_flush:
    ret

.global idt_load
.type idt_load, @function
idt_load:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

.global tss_load
.type tss_load, @function
tss_load:
    mov 4(%esp), %ax
    ltr %ax
    ret

.macro ISR_NOERR num
.global isr\num
.type isr\num, @function
isr\num:
    pushl $0
    pushl $\num
    jmp interrupt_common
.endm

.macro ISR_ERR num
.global isr\num
.type isr\num, @function
isr\num:
    pushl $\num
    jmp interrupt_common
.endm

.macro IRQ num vector
.global irq\num
.type irq\num, @function
irq\num:
    pushl $0
    pushl $\vector
    jmp interrupt_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31
ISR_NOERR 128

IRQ 0, 32

.global interrupt_resume
.type interrupt_resume, @function
interrupt_resume:
    mov 4(%esp), %esp
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    add $8, %esp
    iret

interrupt_common:
    pusha
    push %ds
    push %es
    push %fs
    push %gs
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    pushl %esp
    call interrupt_dispatch
    mov %eax, %esp
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    add $8, %esp
    iret

.section .note.GNU-stack,"",@progbits
