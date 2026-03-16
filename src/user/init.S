#include "syscall.inc"

.section .text
.code32

.global _start

_start:
    mov $init_banner, %ecx
    mov $(init_banner_end - init_banner), %edx
    call write_stdout

    mov $SYS_FORK, %eax
    int $0x80
    cmpl $-1, %eax
    je fork_failed
    test %eax, %eax
    je child_exec

    mov %eax, %esi
    mov $child_pid_banner, %ecx
    mov $(child_pid_banner_end - child_pid_banner), %edx
    call write_stdout
    mov %esi, %eax
    call write_hex32
    mov $newline, %ecx
    mov $1, %edx
    call write_stdout

    sub $4, %esp
    movl $0, (%esp)
    mov $SYS_WAITPID, %eax
    mov %esi, %ebx
    mov %esp, %ecx
    int $0x80
    cmpl $-1, %eax
    je wait_failed

    mov $child_exit_banner, %ecx
    mov $(child_exit_banner_end - child_exit_banner), %edx
    call write_stdout
    mov (%esp), %eax
    call write_hex32
    mov $newline, %ecx
    mov $1, %edx
    call write_stdout
    add $4, %esp
    mov $SYS_FORK, %eax
    int $0x80
    cmpl $-1, %eax
    je fault_fork_failed
    test %eax, %eax
    je fault_exec

    mov %eax, %esi
    mov $fault_pid_banner, %ecx
    mov $(fault_pid_banner_end - fault_pid_banner), %edx
    call write_stdout
    mov %esi, %eax
    call write_hex32
    mov $newline, %ecx
    mov $1, %edx
    call write_stdout

    sub $4, %esp
    movl $0, (%esp)
    mov $SYS_WAITPID, %eax
    mov %esi, %ebx
    mov %esp, %ecx
    int $0x80
    cmpl $-1, %eax
    je fault_wait_failed

    mov $fault_exit_banner, %ecx
    mov $(fault_exit_banner_end - fault_exit_banner), %edx
    call write_stdout
    mov (%esp), %eax
    call write_hex32
    mov $newline, %ecx
    mov $1, %edx
    call write_stdout
    add $4, %esp
    sub $4, %esp
    movl $0x11111111, (%esp)
    mov $SYS_FORK, %eax
    int $0x80
    cmpl $-1, %eax
    je cow_fork_failed
    test %eax, %eax
    je cow_child

    mov %eax, %esi
    sub $4, %esp
    movl $0, (%esp)
    mov $SYS_WAITPID, %eax
    mov %esi, %ebx
    mov %esp, %ecx
    int $0x80
    cmpl $-1, %eax
    je cow_wait_failed

    mov $cow_exit_banner, %ecx
    mov $(cow_exit_banner_end - cow_exit_banner), %edx
    call write_stdout
    mov (%esp), %eax
    call write_hex32
    mov $newline, %ecx
    mov $1, %edx
    call write_stdout
    add $4, %esp

    mov $cow_parent_banner, %ecx
    mov $(cow_parent_banner_end - cow_parent_banner), %edx
    call write_stdout
    mov (%esp), %eax
    call write_hex32
    mov $newline, %ecx
    mov $1, %edx
    call write_stdout
    add $4, %esp

    mov $6, %edi

churn_loop:
    mov $SYS_FORK, %eax
    int $0x80
    cmpl $-1, %eax
    je churn_fork_failed
    test %eax, %eax
    je churn_child

    mov %eax, %esi
    sub $4, %esp
    movl $0, (%esp)
    mov $SYS_WAITPID, %eax
    mov %esi, %ebx
    mov %esp, %ecx
    int $0x80
    cmpl $-1, %eax
    je churn_wait_failed
    cmpl $0x44, (%esp)
    jne churn_status_failed
    add $4, %esp
    decl %edi
    jnz churn_loop

    mov $churn_ok_banner, %ecx
    mov $(churn_ok_banner_end - churn_ok_banner), %edx
    call write_stdout
    jmp yield_forever

child_exec:
    mov $SYS_EXEC, %eax
    mov $child_path, %ebx
    int $0x80

    mov $exec_failed_banner, %ecx
    mov $(exec_failed_banner_end - exec_failed_banner), %edx
    call write_stdout

    mov $SYS_EXIT, %eax
    mov $0x7F, %ebx
    int $0x80

    mov $exit_failed_banner, %ecx
    mov $(exit_failed_banner_end - exit_failed_banner), %edx
    call write_stdout
    jmp yield_forever

fault_exec:
    mov $SYS_EXEC, %eax
    mov $fault_path, %ebx
    int $0x80

    mov $fault_exec_failed_banner, %ecx
    mov $(fault_exec_failed_banner_end - fault_exec_failed_banner), %edx
    call write_stdout

    mov $SYS_EXIT, %eax
    mov $0x7E, %ebx
    int $0x80

    mov $exit_failed_banner, %ecx
    mov $(exit_failed_banner_end - exit_failed_banner), %edx
    call write_stdout
    jmp yield_forever

cow_child:
    movl $0x22222222, (%esp)
    mov $cow_child_banner, %ecx
    mov $(cow_child_banner_end - cow_child_banner), %edx
    call write_stdout
    mov (%esp), %eax
    call write_hex32
    mov $newline, %ecx
    mov $1, %edx
    call write_stdout

    mov $SYS_EXIT, %eax
    mov $0x33, %ebx
    int $0x80

    mov $exit_failed_banner, %ecx
    mov $(exit_failed_banner_end - exit_failed_banner), %edx
    call write_stdout
    jmp yield_forever

churn_child:
    mov $SYS_EXIT, %eax
    mov $0x44, %ebx
    int $0x80

    mov $exit_failed_banner, %ecx
    mov $(exit_failed_banner_end - exit_failed_banner), %edx
    call write_stdout
    jmp yield_forever

fork_failed:
    mov $fork_failed_banner, %ecx
    mov $(fork_failed_banner_end - fork_failed_banner), %edx
    call write_stdout
    jmp yield_forever

wait_failed:
    add $4, %esp
    mov $wait_failed_banner, %ecx
    mov $(wait_failed_banner_end - wait_failed_banner), %edx
    call write_stdout
    jmp yield_forever

fault_fork_failed:
    mov $fault_fork_failed_banner, %ecx
    mov $(fault_fork_failed_banner_end - fault_fork_failed_banner), %edx
    call write_stdout
    jmp yield_forever

fault_wait_failed:
    add $4, %esp
    mov $fault_wait_failed_banner, %ecx
    mov $(fault_wait_failed_banner_end - fault_wait_failed_banner), %edx
    call write_stdout
    jmp yield_forever

cow_fork_failed:
    mov $cow_fork_failed_banner, %ecx
    mov $(cow_fork_failed_banner_end - cow_fork_failed_banner), %edx
    call write_stdout
    jmp yield_forever

cow_wait_failed:
    add $4, %esp
    mov $cow_wait_failed_banner, %ecx
    mov $(cow_wait_failed_banner_end - cow_wait_failed_banner), %edx
    call write_stdout
    jmp yield_forever

churn_fork_failed:
    mov $churn_fork_failed_banner, %ecx
    mov $(churn_fork_failed_banner_end - churn_fork_failed_banner), %edx
    call write_stdout
    jmp yield_forever

churn_wait_failed:
    add $4, %esp
    mov $churn_wait_failed_banner, %ecx
    mov $(churn_wait_failed_banner_end - churn_wait_failed_banner), %edx
    call write_stdout
    jmp yield_forever

churn_status_failed:
    add $4, %esp
    mov $churn_status_failed_banner, %ecx
    mov $(churn_status_failed_banner_end - churn_status_failed_banner), %edx
    call write_stdout
    jmp yield_forever

write_stdout:
    push %ebx
    mov $SYS_WRITE, %eax
    mov $1, %ebx
    int $0x80
    pop %ebx
    ret

write_hex32:
    push %ebx
    push %ecx
    push %edx
    push %esi
    push %edi
    sub $8, %esp
    mov %eax, %esi
    mov %esp, %edi
    mov $8, %ecx

1:
    mov %esi, %edx
    shr $28, %edx
    mov hex_digits(,%edx,1), %dl
    mov %dl, (%edi)
    inc %edi
    shl $4, %esi
    loop 1b

    mov %esp, %ecx
    mov $8, %edx
    call write_stdout

    add $8, %esp
    pop %edi
    pop %esi
    pop %edx
    pop %ecx
    pop %ebx
    ret

yield_forever:
    mov $SYS_YIELD, %eax
    int $0x80
    jmp yield_forever

.section .rodata
init_banner:
    .ascii "init: online\n"
init_banner_end:
fork_failed_banner:
    .ascii "init: fork failed\n"
fork_failed_banner_end:
wait_failed_banner:
    .ascii "init: waitpid failed\n"
wait_failed_banner_end:
child_pid_banner:
    .ascii "init: child pid 0x"
child_pid_banner_end:
child_exit_banner:
    .ascii "init: child exit 0x"
child_exit_banner_end:
fault_pid_banner:
    .ascii "init: fault pid 0x"
fault_pid_banner_end:
fault_exit_banner:
    .ascii "init: fault exit 0x"
fault_exit_banner_end:
cow_child_banner:
    .ascii "init: cow child 0x"
cow_child_banner_end:
cow_parent_banner:
    .ascii "init: cow parent 0x"
cow_parent_banner_end:
cow_exit_banner:
    .ascii "init: cow exit 0x"
cow_exit_banner_end:
churn_ok_banner:
    .ascii "init: churn ok\n"
churn_ok_banner_end:
exec_failed_banner:
    .ascii "init: exec failed\n"
exec_failed_banner_end:
fault_exec_failed_banner:
    .ascii "init: fault exec failed\n"
fault_exec_failed_banner_end:
exit_failed_banner:
    .ascii "init: exit failed\n"
exit_failed_banner_end:
fault_fork_failed_banner:
    .ascii "init: fault fork failed\n"
fault_fork_failed_banner_end:
fault_wait_failed_banner:
    .ascii "init: fault waitpid failed\n"
fault_wait_failed_banner_end:
cow_fork_failed_banner:
    .ascii "init: cow fork failed\n"
cow_fork_failed_banner_end:
cow_wait_failed_banner:
    .ascii "init: cow waitpid failed\n"
cow_wait_failed_banner_end:
churn_fork_failed_banner:
    .ascii "init: churn fork failed\n"
churn_fork_failed_banner_end:
churn_wait_failed_banner:
    .ascii "init: churn waitpid failed\n"
churn_wait_failed_banner_end:
churn_status_failed_banner:
    .ascii "init: churn status failed\n"
churn_status_failed_banner_end:
newline:
    .ascii "\n"
child_path:
    .asciz "/bin/child"
fault_path:
    .asciz "/bin/fault"
hex_digits:
    .ascii "0123456789ABCDEF"

.section .note.GNU-stack,"",@progbits
