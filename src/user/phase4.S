#include "syscall.inc"

.equ LOCAL_TICKS, -4
.equ LOCAL_CONSOLE_FD, -8
.equ LOCAL_FILE_FD, -12
.equ LOCAL_DUP_FD, -16
.equ LOCAL_STAT, -32
.equ LOCAL_BUF, -96
.equ LOCAL_FRAME_SIZE, 96
.equ SYS_STAT_KIND, 0
.equ SYS_STAT_SIZE, 4

.section .text
.code32

.global _start

_start:
    push %ebp
    mov %esp, %ebp
    sub $LOCAL_FRAME_SIZE, %esp
    movl $-1, LOCAL_CONSOLE_FD(%ebp)
    movl $-1, LOCAL_FILE_FD(%ebp)
    movl $-1, LOCAL_DUP_FD(%ebp)

    mov $phase4_banner, %ecx
    mov $(phase4_banner_end - phase4_banner), %edx
    call write_stdout

    mov $SYS_CHDIR, %eax
    mov $bin_dir, %ebx
    int $0x80
    test %eax, %eax
    jne read_failed

    mov $SYS_OPEN, %eax
    mov $child_name, %ebx
    mov $SYS_O_RDONLY, %ecx
    int $0x80
    cmpl $0, %eax
    jl read_failed
    mov %eax, LOCAL_FILE_FD(%ebp)

    mov $SYS_READ, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    lea LOCAL_BUF(%ebp), %ecx
    mov $4, %edx
    int $0x80
    cmpl $4, %eax
    jne read_close_failed

    lea LOCAL_BUF(%ebp), %ecx
    mov (%ecx), %eax
    cmpl $0x464C457F, %eax
    jne read_close_failed

    mov $SYS_LSEEK, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    mov $0, %ecx
    mov $SYS_SEEK_SET, %edx
    int $0x80
    test %eax, %eax
    jne read_close_failed

    mov $SYS_READ, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    lea LOCAL_BUF(%ebp), %ecx
    mov $4, %edx
    int $0x80
    cmpl $4, %eax
    jne read_close_failed

    lea LOCAL_BUF(%ebp), %ecx
    mov (%ecx), %eax
    cmpl $0x464C457F, %eax
    jne read_close_failed

    mov $SYS_CLOSE, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    int $0x80
    movl $-1, LOCAL_FILE_FD(%ebp)
    test %eax, %eax
    jne read_failed

    mov $phase4_read_ok_banner, %ecx
    mov $(phase4_read_ok_banner_end - phase4_read_ok_banner), %edx
    call write_stdout

    mov $SYS_STAT, %eax
    mov $child_name, %ebx
    lea LOCAL_STAT(%ebp), %ecx
    int $0x80
    test %eax, %eax
    jne stat_failed

    cmpl $2, LOCAL_STAT + SYS_STAT_KIND(%ebp)
    jne stat_failed
    cmpl $0, LOCAL_STAT + SYS_STAT_SIZE(%ebp)
    je stat_failed

    mov $phase4_stat_ok_banner, %ecx
    mov $(phase4_stat_ok_banner_end - phase4_stat_ok_banner), %edx
    call write_stdout

    mov $SYS_TICKS, %eax
    int $0x80
    mov %eax, LOCAL_TICKS(%ebp)

    mov $SYS_SLEEP, %eax
    mov $8, %ebx
    int $0x80
    test %eax, %eax
    jne sleep_failed

    mov $SYS_TICKS, %eax
    int $0x80
    sub LOCAL_TICKS(%ebp), %eax
    cmpl $4, %eax
    jb sleep_failed

    mov $phase4_sleep_ok_banner, %ecx
    mov $(phase4_sleep_ok_banner_end - phase4_sleep_ok_banner), %edx
    call write_stdout

    mov $SYS_CHDIR, %eax
    mov $var_dir, %ebx
    int $0x80
    test %eax, %eax
    jne write_failed

    mov $SYS_MKDIR, %eax
    mov $phase4_dir_name, %ebx
    int $0x80
    test %eax, %eax
    jne writable_skipped

    mov $SYS_CHDIR, %eax
    mov $phase4_dir_name, %ebx
    int $0x80
    test %eax, %eax
    jne cleanup_failed

    mov $SYS_OPEN, %eax
    mov $data_file_name, %ebx
    mov $(SYS_O_RDWR | SYS_O_CREATE | SYS_O_TRUNC), %ecx
    int $0x80
    cmpl $0, %eax
    jl write_failed
    mov %eax, LOCAL_FILE_FD(%ebp)

    mov $SYS_WRITE, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    mov $data_payload, %ecx
    mov $(data_payload_end - data_payload), %edx
    int $0x80
    cmpl $(data_payload_end - data_payload), %eax
    jne write_close_failed

    mov $SYS_LSEEK, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    mov $0, %ecx
    mov $SYS_SEEK_SET, %edx
    int $0x80
    test %eax, %eax
    jne write_close_failed

    mov $SYS_READ, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    lea LOCAL_BUF(%ebp), %ecx
    mov $(data_payload_end - data_payload), %edx
    int $0x80
    cmpl $(data_payload_end - data_payload), %eax
    jne write_close_failed

    lea LOCAL_BUF(%ebp), %ecx
    mov $data_payload, %edx
    mov $(data_payload_end - data_payload), %ebx
    call compare_bytes
    test %eax, %eax
    jne write_close_failed

    mov $SYS_CLOSE, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    int $0x80
    movl $-1, LOCAL_FILE_FD(%ebp)
    test %eax, %eax
    jne write_failed

    mov $phase4_write_ok_banner, %ecx
    mov $(phase4_write_ok_banner_end - phase4_write_ok_banner), %edx
    call write_stdout

    mov $SYS_OPEN, %eax
    mov $console_path, %ebx
    mov $SYS_O_WRONLY, %ecx
    int $0x80
    cmpl $0, %eax
    jl dup_failed
    mov %eax, LOCAL_CONSOLE_FD(%ebp)

    mov $SYS_OPEN, %eax
    mov $dup_file_name, %ebx
    mov $(SYS_O_RDWR | SYS_O_CREATE | SYS_O_TRUNC), %ecx
    int $0x80
    cmpl $0, %eax
    jl dup_failed
    mov %eax, LOCAL_FILE_FD(%ebp)

    mov $SYS_DUP, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    int $0x80
    cmpl $0, %eax
    jl dup_failed
    mov %eax, LOCAL_DUP_FD(%ebp)

    mov $SYS_WRITE, %eax
    mov LOCAL_DUP_FD(%ebp), %ebx
    mov $dup_payload, %ecx
    mov $(dup_payload_end - dup_payload), %edx
    int $0x80
    cmpl $(dup_payload_end - dup_payload), %eax
    jne dup_failed

    mov $SYS_LSEEK, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    mov $0, %ecx
    mov $SYS_SEEK_SET, %edx
    int $0x80
    test %eax, %eax
    jne dup_failed

    mov $SYS_READ, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    lea LOCAL_BUF(%ebp), %ecx
    mov $(dup_payload_end - dup_payload), %edx
    int $0x80
    cmpl $(dup_payload_end - dup_payload), %eax
    jne dup_failed

    lea LOCAL_BUF(%ebp), %ecx
    mov $dup_payload, %edx
    mov $(dup_payload_end - dup_payload), %ebx
    call compare_bytes
    test %eax, %eax
    jne dup_failed

    mov $SYS_DUP2, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    mov $1, %ecx
    int $0x80
    cmpl $1, %eax
    jne dup_failed

    mov $dup2_payload, %ecx
    mov $(dup2_payload_end - dup2_payload), %edx
    call write_stdout

    mov $SYS_DUP2, %eax
    mov LOCAL_CONSOLE_FD(%ebp), %ebx
    mov $1, %ecx
    int $0x80
    cmpl $1, %eax
    jne dup_failed

    mov $SYS_LSEEK, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    mov $(dup_payload_end - dup_payload), %ecx
    mov $SYS_SEEK_SET, %edx
    int $0x80
    cmpl $(dup_payload_end - dup_payload), %eax
    jne dup_failed

    mov $SYS_READ, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    lea LOCAL_BUF(%ebp), %ecx
    mov $(dup2_payload_end - dup2_payload), %edx
    int $0x80
    cmpl $(dup2_payload_end - dup2_payload), %eax
    jne dup_failed

    lea LOCAL_BUF(%ebp), %ecx
    mov $dup2_payload, %edx
    mov $(dup2_payload_end - dup2_payload), %ebx
    call compare_bytes
    test %eax, %eax
    jne dup_failed

    mov $SYS_CLOSE, %eax
    mov LOCAL_DUP_FD(%ebp), %ebx
    int $0x80
    movl $-1, LOCAL_DUP_FD(%ebp)
    test %eax, %eax
    jne dup_failed

    mov $SYS_CLOSE, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    int $0x80
    movl $-1, LOCAL_FILE_FD(%ebp)
    test %eax, %eax
    jne dup_failed

    mov $SYS_CLOSE, %eax
    mov LOCAL_CONSOLE_FD(%ebp), %ebx
    int $0x80
    movl $-1, LOCAL_CONSOLE_FD(%ebp)
    test %eax, %eax
    jne dup_failed

    mov $phase4_dup_ok_banner, %ecx
    mov $(phase4_dup_ok_banner_end - phase4_dup_ok_banner), %edx
    call write_stdout

    mov $SYS_UNLINK, %eax
    mov $data_file_name, %ebx
    int $0x80
    test %eax, %eax
    jne cleanup_failed

    mov $SYS_UNLINK, %eax
    mov $dup_file_name, %ebx
    int $0x80
    test %eax, %eax
    jne cleanup_failed

    mov $SYS_CHDIR, %eax
    mov $var_dir, %ebx
    int $0x80
    test %eax, %eax
    jne cleanup_failed

    mov $SYS_UNLINK, %eax
    mov $phase4_dir_name, %ebx
    int $0x80
    test %eax, %eax
    jne cleanup_failed

    mov $phase4_writable_ok_banner, %ecx
    mov $(phase4_writable_ok_banner_end - phase4_writable_ok_banner), %edx
    call write_stdout
    jmp exit_success

writable_skipped:
    mov $phase4_writable_skipped_banner, %ecx
    mov $(phase4_writable_skipped_banner_end - phase4_writable_skipped_banner), %edx
    call write_stdout
    jmp exit_success

read_close_failed:
    mov $SYS_CLOSE, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    int $0x80
    movl $-1, LOCAL_FILE_FD(%ebp)

read_failed:
    mov $phase4_read_failed_banner, %ecx
    mov $(phase4_read_failed_banner_end - phase4_read_failed_banner), %edx
    call write_stdout
    mov $0xE1, %ebx
    jmp exit_with_status

stat_failed:
    mov $phase4_stat_failed_banner, %ecx
    mov $(phase4_stat_failed_banner_end - phase4_stat_failed_banner), %edx
    call write_stdout
    mov $0xE2, %ebx
    jmp exit_with_status

sleep_failed:
    mov $phase4_sleep_failed_banner, %ecx
    mov $(phase4_sleep_failed_banner_end - phase4_sleep_failed_banner), %edx
    call write_stdout
    mov $0xE3, %ebx
    jmp exit_with_status

write_close_failed:
    mov $SYS_CLOSE, %eax
    mov LOCAL_FILE_FD(%ebp), %ebx
    int $0x80
    movl $-1, LOCAL_FILE_FD(%ebp)

write_failed:
    mov $phase4_write_failed_banner, %ecx
    mov $(phase4_write_failed_banner_end - phase4_write_failed_banner), %edx
    call write_stdout
    mov $0xE4, %ebx
    jmp exit_with_status

dup_failed:
    cmpl $0, LOCAL_CONSOLE_FD(%ebp)
    jl 1f
    mov $SYS_DUP2, %eax
    mov LOCAL_CONSOLE_FD(%ebp), %ebx
    mov $1, %ecx
    int $0x80
1:
    mov $phase4_dup_failed_banner, %ecx
    mov $(phase4_dup_failed_banner_end - phase4_dup_failed_banner), %edx
    call write_stdout
    mov $0xE5, %ebx
    jmp exit_with_status

cleanup_failed:
    mov $phase4_cleanup_failed_banner, %ecx
    mov $(phase4_cleanup_failed_banner_end - phase4_cleanup_failed_banner), %edx
    call write_stdout
    mov $0xE6, %ebx
    jmp exit_with_status

exit_success:
    mov $0x40, %ebx

exit_with_status:
    mov $SYS_EXIT, %eax
    int $0x80

    mov $exit_failed_banner, %ecx
    mov $(exit_failed_banner_end - exit_failed_banner), %edx
    call write_stdout

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

compare_bytes:
    push %ebx
    push %ecx
    push %edx
    push %esi
    push %edi
    mov %ecx, %esi
    mov %edx, %edi
    mov %ebx, %ecx
    xorl %eax, %eax

1:
    test %ecx, %ecx
    je 2f
    movb (%esi), %al
    cmpb %al, (%edi)
    jne 3f
    incl %esi
    incl %edi
    decl %ecx
    jmp 1b

2:
    xorl %eax, %eax
    jmp 4f

3:
    mov $-1, %eax

4:
    pop %edi
    pop %esi
    pop %edx
    pop %ecx
    pop %ebx
    ret

.section .rodata
phase4_banner:
    .ascii "phase4: online\n"
phase4_banner_end:
phase4_read_ok_banner:
    .ascii "phase4: read ok\n"
phase4_read_ok_banner_end:
phase4_stat_ok_banner:
    .ascii "phase4: stat ok\n"
phase4_stat_ok_banner_end:
phase4_sleep_ok_banner:
    .ascii "phase4: sleep ok\n"
phase4_sleep_ok_banner_end:
phase4_write_ok_banner:
    .ascii "phase4: write ok\n"
phase4_write_ok_banner_end:
phase4_dup_ok_banner:
    .ascii "phase4: dup ok\n"
phase4_dup_ok_banner_end:
phase4_writable_ok_banner:
    .ascii "phase4: writable ok\n"
phase4_writable_ok_banner_end:
phase4_writable_skipped_banner:
    .ascii "phase4: writable skipped\n"
phase4_writable_skipped_banner_end:
phase4_read_failed_banner:
    .ascii "phase4: read failed\n"
phase4_read_failed_banner_end:
phase4_stat_failed_banner:
    .ascii "phase4: stat failed\n"
phase4_stat_failed_banner_end:
phase4_sleep_failed_banner:
    .ascii "phase4: sleep failed\n"
phase4_sleep_failed_banner_end:
phase4_write_failed_banner:
    .ascii "phase4: write failed\n"
phase4_write_failed_banner_end:
phase4_dup_failed_banner:
    .ascii "phase4: dup failed\n"
phase4_dup_failed_banner_end:
phase4_cleanup_failed_banner:
    .ascii "phase4: cleanup failed\n"
phase4_cleanup_failed_banner_end:
exit_failed_banner:
    .ascii "phase4: exit failed\n"
exit_failed_banner_end:
bin_dir:
    .asciz "/bin"
var_dir:
    .asciz "/var"
console_path:
    .asciz "/dev/console"
child_name:
    .asciz "child"
phase4_dir_name:
    .asciz "p4dir"
data_file_name:
    .asciz "data.txt"
dup_file_name:
    .asciz "dup.txt"
data_payload:
    .ascii "phase4-data\n"
data_payload_end:
dup_payload:
    .ascii "dup-one\n"
dup_payload_end:
dup2_payload:
    .ascii "dup-two\n"
dup2_payload_end:

.section .note.GNU-stack,"",@progbits
