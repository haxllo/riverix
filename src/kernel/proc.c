#include "kernel/proc.h"

#include <stdint.h>

#include "kernel/console.h"
#include "kernel/exec.h"
#include "kernel/gdt.h"
#include "kernel/kstack.h"
#include "kernel/paging.h"
#include "kernel/pit.h"
#include "kernel/syscall.h"
#include "kernel/usercopy.h"
#include "kernel/vfs.h"

#define MAX_TASKS 8u
#define TASK_EFLAGS 0x00000202u
#define PROC_IO_CHUNK 256u
#define PROC_PATH_MAX VFS_PATH_MAX
#define PROC_PATH_COMPONENT_MAX VFS_NAME_MAX
#define PROC_PATH_COMPONENTS_MAX 24u
#define PROC_DEFAULT_TTY "/dev/console"

typedef enum task_state {
    TASK_UNUSED = 0,
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_ZOMBIE,
} task_state_t;

typedef enum task_kind {
    TASK_KERNEL = 0,
    TASK_USER,
} task_kind_t;

typedef enum task_block_reason {
    TASK_BLOCK_NONE = 0,
    TASK_BLOCK_WAITPID,
    TASK_BLOCK_IO_READ,
    TASK_BLOCK_IO_WRITE,
} task_block_reason_t;

typedef struct task {
    uint32_t pid;
    uint32_t parent_pid;
    task_state_t state;
    task_kind_t kind;
    uint32_t saved_esp;
    uint32_t page_directory_phys;
    kernel_stack_t kstack;
    exec_image_t image;
    char name[SYS_NAME_MAX];
    task_entry_t entry;
    void *arg;
    uint32_t run_ticks;
    uint32_t switches;
    int32_t exit_status;
    int32_t wait_target_pid;
    uint32_t wait_status_user;
    uint32_t wake_tick;
    uint32_t reap_ready;
    char cwd[PROC_PATH_MAX];
    uint32_t uid;
    uint32_t gid;
    uint32_t session_id;
    uint32_t process_group_id;
    char tty_name[SYS_NAME_MAX];
    task_block_reason_t block_reason;
    uint32_t blocked_fd;
    uint32_t blocked_user_buffer;
    uint32_t blocked_length;
    uint32_t blocked_done;
    vfs_file_t *fd_table[VFS_MAX_FDS];
} task_t;

static task_t tasks[MAX_TASKS];
static task_t *current_task;
static task_t *idle_task;
static task_t *init_task;
static uint32_t next_pid = 1u;
static uint32_t last_logged_tick;

static void task_bootstrap(void);

static void task_reset_image(exec_image_t *image) {
    uint32_t index;

    image->entry_point = 0u;
    image->stack_top = 0u;
    image->mapped_page_count = 0u;

    for (index = 0u; index < EXEC_MAX_MAPPED_PAGES; index++) {
        image->mapped_virtuals[index] = 0u;
        image->mapped_pages[index] = 0u;
    }
}

static void proc_fill_credentials(const task_t *task, vfs_credentials_t *credentials) {
    if (credentials == 0) {
        return;
    }

    if (task == 0) {
        credentials->uid = 0u;
        credentials->gid = 0u;
        return;
    }

    credentials->uid = task->uid;
    credentials->gid = task->gid;
}

static void task_clear_blocked_io(task_t *task) {
    if (task == 0) {
        return;
    }

    task->block_reason = TASK_BLOCK_NONE;
    task->blocked_fd = 0u;
    task->blocked_user_buffer = 0u;
    task->blocked_length = 0u;
    task->blocked_done = 0u;
}

static void copy_bytes(void *destination, const void *source, uint32_t length) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    uint32_t index;

    for (index = 0u; index < length; index++) {
        dst[index] = src[index];
    }
}

static void task_reset(task_t *task) {
    uint32_t index;

    task->pid = 0u;
    task->parent_pid = 0u;
    task->state = TASK_UNUSED;
    task->kind = TASK_KERNEL;
    task->saved_esp = 0u;
    task->page_directory_phys = 0u;
    task->kstack.guard_base = 0u;
    task->kstack.stack_base = 0u;
    task->kstack.stack_top = 0u;
    task_reset_image(&task->image);
    task->name[0] = '\0';
    task->entry = 0;
    task->arg = 0;
    task->run_ticks = 0u;
    task->switches = 0u;
    task->exit_status = 0;
    task->wait_target_pid = -1;
    task->wait_status_user = 0u;
    task->wake_tick = 0u;
    task->reap_ready = 0u;
    task->cwd[0] = '/';
    task->cwd[1] = '\0';
    task->uid = 0u;
    task->gid = 0u;
    task->session_id = 0u;
    task->process_group_id = 0u;
    task->tty_name[0] = '\0';
    task_clear_blocked_io(task);

    for (index = 0u; index < VFS_MAX_FDS; index++) {
        task->fd_table[index] = 0;
    }
}

static void cpu_relax(void) {
    __asm__ volatile ("pause");
}

static task_t *task_find_by_pid(uint32_t pid) {
    uint32_t index;

    if (pid == 0u) {
        return 0;
    }

    for (index = 0u; index < MAX_TASKS; index++) {
        if (tasks[index].state == TASK_UNUSED || tasks[index].pid != pid) {
            continue;
        }

        return &tasks[index];
    }

    return 0;
}

static void task_close_files(task_t *task) {
    vfs_detach_fds(task->fd_table, VFS_MAX_FDS);
}

static void task_release_resources(task_t *task) {
    if (task == 0) {
        return;
    }

    task_close_files(task);

    if (task->page_directory_phys != 0u) {
        exec_release_image(task->page_directory_phys, &task->image);
    }

    if (task->page_directory_phys != 0u && task->page_directory_phys != paging_page_directory_phys()) {
        paging_destroy_address_space(task->page_directory_phys);
    }

    kstack_free(&task->kstack);

    if (task == init_task) {
        init_task = 0;
    }

    if (task == idle_task) {
        idle_task = 0;
    }

    task_reset(task);
}

static task_t *task_find_unused(void) {
    uint32_t index;

    for (index = 0u; index < MAX_TASKS; index++) {
        if (tasks[index].state != TASK_UNUSED) {
            continue;
        }

        task_reset(&tasks[index]);
        return &tasks[index];
    }

    return 0;
}

static int task_setup_kernel_stack(task_t *task) {
    return kstack_alloc(&task->kstack);
}

static int task_setup_stdio(task_t *task) {
    vfs_credentials_t credentials;

    proc_fill_credentials(task, &credentials);
    return vfs_attach_stdio(task->fd_table, VFS_MAX_FDS, &credentials);
}

static void task_prepare_kernel_frame(interrupt_frame_t *frame) {
    frame->gs = GDT_KERNEL_DATA_SELECTOR;
    frame->fs = GDT_KERNEL_DATA_SELECTOR;
    frame->es = GDT_KERNEL_DATA_SELECTOR;
    frame->ds = GDT_KERNEL_DATA_SELECTOR;
    frame->edi = 0u;
    frame->esi = 0u;
    frame->ebp = 0u;
    frame->esp = 0u;
    frame->ebx = 0u;
    frame->edx = 0u;
    frame->ecx = 0u;
    frame->eax = 0u;
    frame->int_no = 0u;
    frame->err_code = 0u;
    frame->eip = (uint32_t)(uintptr_t)task_bootstrap;
    frame->cs = GDT_KERNEL_CODE_SELECTOR;
    frame->eflags = TASK_EFLAGS;
}

static void task_prepare_user_frame(interrupt_user_frame_t *frame, uintptr_t entry_point, uintptr_t user_stack_top) {
    frame->base.gs = GDT_USER_DATA_SELECTOR;
    frame->base.fs = GDT_USER_DATA_SELECTOR;
    frame->base.es = GDT_USER_DATA_SELECTOR;
    frame->base.ds = GDT_USER_DATA_SELECTOR;
    frame->base.edi = 0u;
    frame->base.esi = 0u;
    frame->base.ebp = 0u;
    frame->base.esp = 0u;
    frame->base.ebx = 0u;
    frame->base.edx = 0u;
    frame->base.ecx = 0u;
    frame->base.eax = 0u;
    frame->base.int_no = 0u;
    frame->base.err_code = 0u;
    frame->base.eip = (uint32_t)entry_point;
    frame->base.cs = GDT_USER_CODE_SELECTOR;
    frame->base.eflags = TASK_EFLAGS;
    frame->user_esp = (uint32_t)user_stack_top;
    frame->user_ss = GDT_USER_DATA_SELECTOR;
}

static uint32_t string_length(const char *text) {
    uint32_t length = 0u;

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static int strings_equal(const char *left, const char *right) {
    uint32_t index = 0u;

    if (left == 0 || right == 0) {
        return 0;
    }

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }

        index++;
    }

    return left[index] == right[index];
}

static void string_copy(char *destination, const char *source, uint32_t max_length) {
    uint32_t index = 0u;

    if (destination == 0 || source == 0 || max_length == 0u) {
        return;
    }

    while ((index + 1u) < max_length && source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }

    destination[index] = '\0';
}

static const char *path_basename(const char *path) {
    const char *cursor = path;
    const char *last = path;

    if (path == 0 || path[0] == '\0') {
        return "";
    }

    while (*cursor != '\0') {
        if (*cursor == '/' && cursor[1] != '\0') {
            last = cursor + 1;
        }

        cursor++;
    }

    return last;
}

static void task_set_name(task_t *task, const char *name) {
    if (task == 0) {
        return;
    }

    string_copy(task->name, name != 0 ? name : "", sizeof(task->name));
}

static void task_set_tty(task_t *task, const char *tty_name) {
    if (task == 0) {
        return;
    }

    string_copy(task->tty_name, tty_name != 0 ? tty_name : "", sizeof(task->tty_name));
}

static void write_hex32_to_fd(uint32_t fd, uint32_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";
    char buffer[8];
    uint32_t shift;

    for (shift = 0u; shift < 8u; shift++) {
        uint32_t nibble = (value >> ((7u - shift) * 4u)) & 0xFu;
        buffer[shift] = hex_digits[nibble];
    }

    (void)sys_write(fd, buffer, 8u);
}

static const char *proc_effective_path(const task_t *task, const char *path, char *resolved_path, uint32_t resolved_length) {
    if (task != 0 &&
        path != 0 &&
        resolved_path != 0 &&
        resolved_length != 0u &&
        strings_equal(path, "/dev/tty")) {
        if (task->tty_name[0] == '\0') {
            return 0;
        }

        string_copy(resolved_path, task->tty_name, resolved_length);
        return resolved_path;
    }

    return path;
}

static int proc_allocate_fd_slot(task_t *task) {
    uint32_t index;

    if (task == 0) {
        return -1;
    }

    for (index = 0u; index < VFS_MAX_FDS; index++) {
        if (task->fd_table[index] == 0) {
            return (int)index;
        }
    }

    return -1;
}

static int proc_allocate_fd_slot_excluding(task_t *task, int32_t excluded_fd) {
    uint32_t index;

    if (task == 0) {
        return -1;
    }

    for (index = 0u; index < VFS_MAX_FDS; index++) {
        if ((int32_t)index == excluded_fd) {
            continue;
        }

        if (task->fd_table[index] == 0) {
            return (int)index;
        }
    }

    return -1;
}

static int proc_install_fd(task_t *task, int32_t fd, vfs_file_t *file) {
    if (task == 0 || file == 0 || fd < 0 || (uint32_t)fd >= VFS_MAX_FDS || task->fd_table[(uint32_t)fd] != 0) {
        return -1;
    }

    task->fd_table[(uint32_t)fd] = file;
    return fd;
}

static vfs_file_t *proc_lookup_fd(task_t *task, uint32_t fd) {
    if (task == 0 || fd >= VFS_MAX_FDS) {
        return 0;
    }

    return task->fd_table[fd];
}

static void proc_wake_sleepers(void) {
    uint32_t index;
    uint32_t ticks = pit_ticks();

    for (index = 0u; index < MAX_TASKS; index++) {
        task_t *task = &tasks[index];

        if (task->state != TASK_SLEEPING) {
            continue;
        }

        if ((int32_t)(ticks - task->wake_tick) < 0) {
            continue;
        }

        task->wake_tick = 0u;
        task->state = TASK_RUNNABLE;
    }
}

static int proc_push_component(char components[PROC_PATH_COMPONENTS_MAX][PROC_PATH_COMPONENT_MAX], uint32_t *component_count, const char *source, uint32_t length) {
    uint32_t index;

    if (component_count == 0 || source == 0 || length == 0u || length >= PROC_PATH_COMPONENT_MAX || *component_count >= PROC_PATH_COMPONENTS_MAX) {
        return -1;
    }

    for (index = 0u; index < length; index++) {
        components[*component_count][index] = source[index];
    }
    components[*component_count][length] = '\0';
    (*component_count)++;
    return 0;
}

static int proc_consume_path(char components[PROC_PATH_COMPONENTS_MAX][PROC_PATH_COMPONENT_MAX], uint32_t *component_count, const char *path) {
    const char *cursor = path;

    if (component_count == 0 || path == 0) {
        return -1;
    }

    while (*cursor != '\0') {
        const char *component_start;
        uint32_t component_length = 0u;

        while (*cursor == '/') {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        component_start = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
            component_length++;
        }

        if (component_length == 1u && component_start[0] == '.') {
            continue;
        }

        if (component_length == 2u && component_start[0] == '.' && component_start[1] == '.') {
            if (*component_count != 0u) {
                (*component_count)--;
            }
            continue;
        }

        if (proc_push_component(components, component_count, component_start, component_length) != 0) {
            return -1;
        }
    }

    return 0;
}

static int proc_normalize_path(const task_t *task, const char *path, char *absolute_path, uint32_t max_length) {
    char components[PROC_PATH_COMPONENTS_MAX][PROC_PATH_COMPONENT_MAX];
    uint32_t component_count = 0u;
    uint32_t offset = 0u;
    uint32_t index;

    if (task == 0 || path == 0 || absolute_path == 0 || max_length < 2u || path[0] == '\0') {
        return -1;
    }

    if (path[0] != '/' && proc_consume_path(components, &component_count, task->cwd) != 0) {
        return -1;
    }

    if (proc_consume_path(components, &component_count, path) != 0) {
        return -1;
    }

    absolute_path[offset++] = '/';
    if (component_count == 0u) {
        absolute_path[offset] = '\0';
        return 0;
    }

    for (index = 0u; index < component_count; index++) {
        uint32_t length = string_length(components[index]);
        uint32_t component_index;

        if ((offset + length + 1u) >= max_length) {
            return -1;
        }

        for (component_index = 0u; component_index < length; component_index++) {
            absolute_path[offset++] = components[index][component_index];
        }

        if ((index + 1u) < component_count) {
            absolute_path[offset++] = '/';
        }
    }

    absolute_path[offset] = '\0';
    return 0;
}

static int task_load_user_image(task_t *task, const char *path, const char *const *argv, uint32_t argc) {
    if (task == 0 || path == 0) {
        return -1;
    }

    if (exec_load_path(task->page_directory_phys, path, &task->image) != 0) {
        return -1;
    }

    if (exec_prepare_stack(task->page_directory_phys, &task->image, argv, argc) != 0) {
        exec_release_image(task->page_directory_phys, &task->image);
        task_reset_image(&task->image);
        return -1;
    }

    task_set_name(task, path_basename(path));
    return 0;
}

static task_t *proc_create_kernel_task(const char *name, task_entry_t entry, void *arg) {
    interrupt_frame_t *frame;
    task_t *task = task_find_unused();

    if (task == 0) {
        console_write("sched: task table full\n");
        return 0;
    }

    if (task_setup_kernel_stack(task) != 0) {
        console_write("sched: no memory for task stack\n");
        task_reset(task);
        return 0;
    }

    if (task_setup_stdio(task) != 0) {
        console_write("vfs: no stdio for task\n");
        task_release_resources(task);
        return 0;
    }

    frame = (interrupt_frame_t *)(task->kstack.stack_top - sizeof(interrupt_frame_t));
    task_prepare_kernel_frame(frame);
    task->kind = TASK_KERNEL;
    task->page_directory_phys = paging_page_directory_phys();
    task->saved_esp = (uint32_t)(uintptr_t)frame;
    task_set_name(task, name);
    task->entry = entry;
    task->arg = arg;
    task->pid = next_pid++;
    task->state = TASK_RUNNABLE;
    return task;
}

static task_t *proc_create_user_task(const char *path) {
    const char *argv[1];
    interrupt_user_frame_t *frame;
    task_t *task = task_find_unused();

    if (task == 0) {
        console_write("sched: task table full\n");
        return 0;
    }

    if (task_setup_kernel_stack(task) != 0) {
        console_write("sched: no memory for task stack\n");
        task_reset(task);
        return 0;
    }

    if (task_setup_stdio(task) != 0) {
        console_write("vfs: no stdio for task\n");
        task_release_resources(task);
        return 0;
    }

    task->page_directory_phys = paging_create_address_space();
    if (task->page_directory_phys == 0u) {
        console_write("paging: no address space for task\n");
        task_release_resources(task);
        return 0;
    }

    argv[0] = path_basename(path);
    if (task_load_user_image(task, path, argv, 1u) != 0) {
        task_release_resources(task);
        return 0;
    }

    frame = (interrupt_user_frame_t *)(task->kstack.stack_top - sizeof(interrupt_user_frame_t));
    task_prepare_user_frame(frame, task->image.entry_point, task->image.stack_top);
    task->kind = TASK_USER;
    task->saved_esp = (uint32_t)(uintptr_t)frame;
    task->pid = next_pid++;
    task->uid = 0u;
    task->gid = 0u;
    task->session_id = task->pid;
    task->process_group_id = task->pid;
    task_set_tty(task, PROC_DEFAULT_TTY);
    task->state = TASK_RUNNABLE;

    console_write("task: ");
    console_write(task->name);
    console_write(" as 0x");
    console_write_hex32(task->page_directory_phys);
    console_write("\n");
    return task;
}

static int proc_wait_matches(const task_t *parent, const task_t *child, int32_t target_pid) {
    if (parent == 0 || child == 0) {
        return 0;
    }

    if (child->state == TASK_UNUSED || child->parent_pid != parent->pid) {
        return 0;
    }

    if (target_pid < 0) {
        return 1;
    }

    return child->pid == (uint32_t)target_pid;
}

static int proc_has_child(const task_t *parent, int32_t target_pid) {
    uint32_t index;

    for (index = 0u; index < MAX_TASKS; index++) {
        if (proc_wait_matches(parent, &tasks[index], target_pid)) {
            return 1;
        }
    }

    return 0;
}

static int proc_user_buffer_writable(uint32_t directory_phys, uint32_t virtual_address, uint32_t length) {
    return paging_user_range_writable_in(directory_phys, virtual_address, length);
}

static task_t *proc_find_zombie_child(const task_t *parent, int32_t target_pid) {
    uint32_t index;

    for (index = 0u; index < MAX_TASKS; index++) {
        if (!proc_wait_matches(parent, &tasks[index], target_pid) || tasks[index].state != TASK_ZOMBIE) {
            continue;
        }

        return &tasks[index];
    }

    return 0;
}

static void proc_collect_reapable_zombies(void) {
    uint32_t index;

    for (index = 0u; index < MAX_TASKS; index++) {
        task_t *task = &tasks[index];

        if (task == current_task || task->state != TASK_ZOMBIE || task->reap_ready == 0u) {
            continue;
        }

        task_release_resources(task);
    }
}

static task_t *proc_pick_next(void) {
    uint32_t start_index = 0u;
    uint32_t index;

    if (current_task != 0) {
        start_index = (uint32_t)(current_task - &tasks[0]) + 1u;
    }

    for (index = 0u; index < MAX_TASKS; index++) {
        uint32_t slot = (start_index + index) % MAX_TASKS;

        if (&tasks[slot] == idle_task) {
            continue;
        }

        if (tasks[slot].state == TASK_RUNNABLE) {
            return &tasks[slot];
        }
    }

    if (idle_task != 0 && idle_task->state == TASK_RUNNABLE) {
        return idle_task;
    }

    return current_task;
}

static void proc_log_switch(const task_t *from, const task_t *to) {
    if (from == to || to == 0) {
        return;
    }

    if ((pit_ticks() - last_logged_tick) < 50u) {
        return;
    }

    last_logged_tick = pit_ticks();

    console_write("sched: switch ");
    if (from != 0 && from->name[0] != '\0') {
        console_write(from->name);
    } else {
        console_write("bootstrap");
    }
    console_write(" -> ");
    console_write(to->name[0] != '\0' ? to->name : "unnamed");
    console_write("\n");
}

static void proc_reparent_children(task_t *parent) {
    uint32_t index;
    uint32_t new_parent_pid = 0u;

    if (init_task != 0 && init_task != parent && init_task->state != TASK_UNUSED && init_task->state != TASK_ZOMBIE) {
        new_parent_pid = init_task->pid;
    }

    for (index = 0u; index < MAX_TASKS; index++) {
        task_t *child = &tasks[index];

        if (child->state == TASK_UNUSED || child->parent_pid != parent->pid) {
            continue;
        }

        child->parent_pid = new_parent_pid;
        if (new_parent_pid == 0u && child->state == TASK_ZOMBIE) {
            child->reap_ready = 1u;
        }
    }
}

static void proc_finish_blocked_wait(task_t *parent, task_t *child) {
    interrupt_frame_t *parent_frame;

    if (parent == 0 || child == 0) {
        return;
    }

    parent_frame = (interrupt_frame_t *)(uintptr_t)parent->saved_esp;
    if (parent->wait_status_user != 0u &&
        user_copy_to_in(parent->page_directory_phys, parent->wait_status_user, &child->exit_status, sizeof(child->exit_status)) != 0) {
        parent_frame->eax = (uint32_t)-1;
    } else {
        parent_frame->eax = child->pid;
    }

    parent->wait_target_pid = -1;
    parent->wait_status_user = 0u;
    parent->block_reason = TASK_BLOCK_NONE;
    if (parent->state == TASK_BLOCKED) {
        parent->state = TASK_RUNNABLE;
    }

    child->reap_ready = 1u;
}

static void proc_notify_parent_exit(task_t *child) {
    task_t *parent;

    if (child == 0 || child->parent_pid == 0u) {
        child->reap_ready = 1u;
        return;
    }

    parent = task_find_by_pid(child->parent_pid);
    if (parent == 0) {
        child->parent_pid = 0u;
        child->reap_ready = 1u;
        return;
    }

    if (parent->state != TASK_BLOCKED ||
        parent->block_reason != TASK_BLOCK_WAITPID ||
        !proc_wait_matches(parent, child, parent->wait_target_pid)) {
        return;
    }

    proc_finish_blocked_wait(parent, child);
}

static int proc_collect_zombie_now(task_t *parent, task_t *child, uint32_t status_user, interrupt_frame_t *frame) {
    if (status_user != 0u &&
        user_copy_to_in(parent->page_directory_phys, status_user, &child->exit_status, sizeof(child->exit_status)) != 0) {
        frame->eax = (uint32_t)-1;
        return -1;
    }

    frame->eax = child->pid;
    task_release_resources(child);
    return 0;
}

static void proc_complete_blocked_io(task_t *task, int32_t result) {
    interrupt_frame_t *frame;

    if (task == 0 || task->saved_esp == 0u) {
        return;
    }

    frame = (interrupt_frame_t *)(uintptr_t)task->saved_esp;
    frame->eax = (uint32_t)result;
    task_clear_blocked_io(task);
    if (task->state == TASK_BLOCKED) {
        task->state = TASK_RUNNABLE;
    }
}

static void proc_retry_blocked_read(task_t *task) {
    char staging[PROC_IO_CHUNK];
    vfs_file_t *file;

    if (task == 0 || task->block_reason != TASK_BLOCK_IO_READ) {
        return;
    }

    file = proc_lookup_fd(task, task->blocked_fd);
    if (file == 0) {
        proc_complete_blocked_io(task, task->blocked_done != 0u ? (int32_t)task->blocked_done : -1);
        return;
    }

    while (task->blocked_done < task->blocked_length) {
        uint32_t chunk = task->blocked_length - task->blocked_done;
        int32_t read_result;

        if (chunk > sizeof(staging)) {
            chunk = sizeof(staging);
        }

        read_result = vfs_read_file(file, staging, chunk);
        if (read_result == VFS_ERR_WOULD_BLOCK) {
            if (task->blocked_done != 0u) {
                proc_complete_blocked_io(task, (int32_t)task->blocked_done);
            }
            return;
        }

        if (read_result < 0) {
            proc_complete_blocked_io(task, task->blocked_done != 0u ? (int32_t)task->blocked_done : -1);
            return;
        }

        if (read_result == 0) {
            proc_complete_blocked_io(task, (int32_t)task->blocked_done);
            return;
        }

        if (user_copy_to_in(task->page_directory_phys,
                            task->blocked_user_buffer + task->blocked_done,
                            staging,
                            (uint32_t)read_result) != 0) {
            proc_complete_blocked_io(task, task->blocked_done != 0u ? (int32_t)task->blocked_done : -1);
            return;
        }

        task->blocked_done += (uint32_t)read_result;
        if ((uint32_t)read_result < chunk) {
            break;
        }
    }

    proc_complete_blocked_io(task, (int32_t)task->blocked_done);
}

static void proc_retry_blocked_write(task_t *task) {
    char staging[PROC_IO_CHUNK];
    vfs_file_t *file;

    if (task == 0 || task->block_reason != TASK_BLOCK_IO_WRITE) {
        return;
    }

    file = proc_lookup_fd(task, task->blocked_fd);
    if (file == 0) {
        proc_complete_blocked_io(task, task->blocked_done != 0u ? (int32_t)task->blocked_done : -1);
        return;
    }

    while (task->blocked_done < task->blocked_length) {
        uint32_t chunk = task->blocked_length - task->blocked_done;
        int32_t write_result;

        if (chunk > sizeof(staging)) {
            chunk = sizeof(staging);
        }

        if (user_copy_from_in(task->page_directory_phys,
                              staging,
                              task->blocked_user_buffer + task->blocked_done,
                              chunk) != 0) {
            proc_complete_blocked_io(task, task->blocked_done != 0u ? (int32_t)task->blocked_done : -1);
            return;
        }

        write_result = vfs_write_file(file, staging, chunk);
        if (write_result == VFS_ERR_WOULD_BLOCK) {
            if (task->blocked_done != 0u) {
                proc_complete_blocked_io(task, (int32_t)task->blocked_done);
            }
            return;
        }

        if (write_result < 0) {
            proc_complete_blocked_io(task, task->blocked_done != 0u ? (int32_t)task->blocked_done : -1);
            return;
        }

        task->blocked_done += (uint32_t)write_result;
        if ((uint32_t)write_result < chunk) {
            break;
        }
    }

    proc_complete_blocked_io(task, (int32_t)task->blocked_done);
}

static void proc_service_blocked_io(void) {
    uint32_t index;

    for (index = 0u; index < MAX_TASKS; index++) {
        task_t *task = &tasks[index];

        if (task->state != TASK_BLOCKED) {
            continue;
        }

        if (task->block_reason == TASK_BLOCK_IO_READ) {
            proc_retry_blocked_read(task);
        } else if (task->block_reason == TASK_BLOCK_IO_WRITE) {
            proc_retry_blocked_write(task);
        }
    }
}

static void idle_entry(void *arg) {
    (void)arg;

    (void)sys_write(1u, "task: idle online\n", 18u);

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void worker_entry(void *arg) {
    const char *name = (const char *)arg;
    uint32_t last_heartbeat_tick = 0u;
    uint32_t work_counter = 0u;
    uint32_t pid = sys_getpid();

    (void)sys_write(1u, "task: ", 6u);
    (void)sys_write(1u, name, string_length(name));
    (void)sys_write(1u, " online pid 0x", 14u);
    write_hex32_to_fd(1u, pid);
    (void)sys_write(1u, "\n", 1u);

    for (;;) {
        uint32_t ticks = pit_ticks();

        work_counter++;
        if ((ticks - last_heartbeat_tick) >= 200u) {
            last_heartbeat_tick = ticks;
            (void)sys_write(1u, "task: ", 6u);
            (void)sys_write(1u, name, string_length(name));
            (void)sys_write(1u, " work 0x", 8u);
            write_hex32_to_fd(1u, work_counter);
            (void)sys_write(1u, "\n", 1u);
            (void)sys_yield();
        }

        cpu_relax();
    }
}

static void task_bootstrap(void) {
    task_t *task = current_task;

    if (task == 0 || task->entry == 0) {
        console_write("sched: bootstrap has no current task\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    task->entry(task->arg);

    console_write("sched: task returned unexpectedly\n");
    task->state = TASK_ZOMBIE;
    task->exit_status = -1;
    task->reap_ready = 1u;

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void proc_init(void) {
    uint32_t index;

    for (index = 0u; index < MAX_TASKS; index++) {
        task_reset(&tasks[index]);
    }

    current_task = 0;
    init_task = 0;
    idle_task = proc_create_kernel_task("idle", idle_entry, 0);
    if (idle_task == 0) {
        console_write("sched: idle task unavailable\n");
        return;
    }

    gdt_set_kernel_stack((uint32_t)idle_task->kstack.stack_top);
    last_logged_tick = 0u;

    console_write("sched: ready\n");
}

void proc_start_boot_tasks(void) {
    init_task = proc_create_user_task("/bin/init");
    (void)proc_create_kernel_task("worker-a", worker_entry, "worker-a");
    (void)proc_create_kernel_task("worker-b", worker_entry, "worker-b");
}

uint32_t proc_schedule(interrupt_frame_t *frame) {
    task_t *next_task;

    if (current_task != 0) {
        current_task->saved_esp = (uint32_t)(uintptr_t)frame;
        current_task->run_ticks++;

        if (current_task->state == TASK_RUNNING) {
            current_task->state = TASK_RUNNABLE;
        }
    }

    proc_wake_sleepers();
    proc_service_blocked_io();
    proc_collect_reapable_zombies();

    next_task = proc_pick_next();
    if (next_task == 0) {
        return (uint32_t)(uintptr_t)frame;
    }

    proc_log_switch(current_task, next_task);
    current_task = next_task;
    paging_switch_directory(current_task->page_directory_phys);
    gdt_set_kernel_stack((uint32_t)current_task->kstack.stack_top);
    current_task->state = TASK_RUNNING;
    current_task->switches++;

    return current_task->saved_esp;
}

uint32_t proc_current_pid(void) {
    if (current_task == 0) {
        return 0u;
    }

    return current_task->pid;
}

uint32_t proc_current_ticks(void) {
    return pit_ticks();
}

int proc_preemptible_from_interrupt(const interrupt_frame_t *frame) {
    if (frame == 0 || current_task == 0) {
        return 1;
    }

    if (current_task->kind == TASK_USER && !interrupt_from_user(frame)) {
        return 0;
    }

    return 1;
}

int32_t proc_open_fd(const char *path, uint32_t flags) {
    char absolute_path[PROC_PATH_MAX];
    char resolved_path[PROC_PATH_MAX];
    vfs_file_t *file = 0;
    const char *effective_path;
    vfs_credentials_t credentials;
    int32_t fd;

    if (current_task == 0 || path == 0 || proc_normalize_path(current_task, path, absolute_path, sizeof(absolute_path)) != 0) {
        return -1;
    }

    if ((flags & VFS_O_RDWR) == 0u || (flags & ~(VFS_O_RDWR | VFS_O_CREATE | VFS_O_TRUNC)) != 0u) {
        return -1;
    }

    fd = proc_allocate_fd_slot(current_task);
    if (fd < 0) {
        return -1;
    }

    proc_fill_credentials(current_task, &credentials);
    effective_path = proc_effective_path(current_task, absolute_path, resolved_path, sizeof(resolved_path));
    if (effective_path == 0) {
        return -1;
    }

    if (vfs_open_path(&file, effective_path, flags & ~VFS_O_CREATE, &credentials) != 0) {
        if ((flags & VFS_O_CREATE) == 0u ||
            vfs_create_path(&file, effective_path, flags & ~VFS_O_CREATE, VFS_MODE_FILE_DEFAULT, &credentials) != 0) {
            return -1;
        }
    }

    if ((flags & VFS_O_TRUNC) != 0u && (flags & VFS_O_WRONLY) != 0u) {
        if (vfs_truncate_file(file, 0u) != 0) {
            vfs_close_file(file);
            return -1;
        }
    }

    if (proc_install_fd(current_task, fd, file) < 0) {
        vfs_close_file(file);
        return -1;
    }

    return fd;
}

int32_t proc_close_fd(uint32_t fd) {
    vfs_file_t *file;

    if (current_task == 0 || fd >= VFS_MAX_FDS) {
        return -1;
    }

    file = current_task->fd_table[fd];
    if (file == 0) {
        return -1;
    }

    current_task->fd_table[fd] = 0;
    vfs_close_file(file);
    return 0;
}

int32_t proc_read_fd(uint32_t fd, void *buffer, uint32_t length) {
    if (current_task == 0) {
        return -1;
    }

    return vfs_read_fd(current_task->fd_table, VFS_MAX_FDS, fd, buffer, length);
}

int32_t proc_write_fd(uint32_t fd, const char *buffer, uint32_t length) {
    if (current_task == 0) {
        return -1;
    }

    return vfs_write_fd(current_task->fd_table, VFS_MAX_FDS, fd, buffer, length);
}

int32_t proc_seek_fd(uint32_t fd, int32_t offset, uint32_t whence) {
    vfs_file_t *file = proc_lookup_fd(current_task, fd);

    if (file == 0) {
        return -1;
    }

    return vfs_seek_file(file, offset, whence);
}

int32_t proc_mkdir(const char *path) {
    char absolute_path[PROC_PATH_MAX];
    vfs_credentials_t credentials;

    if (current_task == 0 || path == 0 || proc_normalize_path(current_task, path, absolute_path, sizeof(absolute_path)) != 0) {
        return -1;
    }

    proc_fill_credentials(current_task, &credentials);
    return vfs_mkdir_path(absolute_path, VFS_MODE_DIR_DEFAULT, &credentials);
}

int32_t proc_unlink(const char *path) {
    char absolute_path[PROC_PATH_MAX];
    vfs_credentials_t credentials;

    if (current_task == 0 || path == 0 || proc_normalize_path(current_task, path, absolute_path, sizeof(absolute_path)) != 0) {
        return -1;
    }

    proc_fill_credentials(current_task, &credentials);
    return vfs_unlink_path(absolute_path, &credentials);
}

int32_t proc_stat(const char *path, vfs_stat_t *stat) {
    char absolute_path[PROC_PATH_MAX];
    char resolved_path[PROC_PATH_MAX];
    const char *effective_path;
    vfs_credentials_t credentials;

    if (current_task == 0 || path == 0 || stat == 0 || proc_normalize_path(current_task, path, absolute_path, sizeof(absolute_path)) != 0) {
        return -1;
    }

    proc_fill_credentials(current_task, &credentials);
    effective_path = proc_effective_path(current_task, absolute_path, resolved_path, sizeof(resolved_path));
    if (effective_path == 0) {
        return -1;
    }

    return vfs_stat_path(effective_path, &credentials, stat);
}

int32_t proc_readdir(const char *path, uint32_t index, sys_dirent_t *entry) {
    char absolute_path[PROC_PATH_MAX];
    vfs_dirent_info_t kernel_entry;
    vfs_credentials_t credentials;
    int32_t result;

    if (current_task == 0 || path == 0 || entry == 0 || proc_normalize_path(current_task, path, absolute_path, sizeof(absolute_path)) != 0) {
        return -1;
    }

    proc_fill_credentials(current_task, &credentials);
    result = vfs_readdir_path(absolute_path, &credentials, index, &kernel_entry);
    if (result != 0) {
        return result;
    }

    entry->kind = kernel_entry.kind;
    entry->size = kernel_entry.size;
    entry->mode = kernel_entry.mode;
    entry->uid = kernel_entry.uid;
    entry->gid = kernel_entry.gid;
    entry->links = kernel_entry.links;
    string_copy(entry->name, kernel_entry.name, sizeof(entry->name));
    return 0;
}

int32_t proc_procinfo(uint32_t index, sys_procinfo_t *info) {
    uint32_t slot;
    uint32_t seen = 0u;

    if (info == 0) {
        return -1;
    }

    for (slot = 0u; slot < MAX_TASKS; slot++) {
        task_t *task = &tasks[slot];

        if (task->state == TASK_UNUSED) {
            continue;
        }

        if (seen != index) {
            seen++;
            continue;
        }

        info->pid = task->pid;
        info->parent_pid = task->parent_pid;
        info->sid = task->session_id;
        info->pgid = task->process_group_id;
        info->state = (uint32_t)task->state;
        info->kind = (uint32_t)task->kind;
        info->run_ticks = task->run_ticks;
        info->uid = task->uid;
        info->gid = task->gid;
        string_copy(info->name, task->name, sizeof(info->name));
        string_copy(info->tty, task->tty_name, sizeof(info->tty));
        return 0;
    }

    return 1;
}

int32_t proc_getcwd(char *buffer, uint32_t length) {
    uint32_t cwd_length;

    if (current_task == 0 || buffer == 0 || length == 0u) {
        return -1;
    }

    cwd_length = string_length(current_task->cwd);
    if ((cwd_length + 1u) > length) {
        return -1;
    }

    string_copy(buffer, current_task->cwd, length);
    return (int32_t)cwd_length;
}

int32_t proc_chdir(const char *path) {
    char absolute_path[PROC_PATH_MAX];
    vfs_credentials_t credentials;
    vfs_stat_t stat;

    if (current_task == 0 || path == 0 || proc_normalize_path(current_task, path, absolute_path, sizeof(absolute_path)) != 0) {
        return -1;
    }

    proc_fill_credentials(current_task, &credentials);
    if (vfs_stat_path(absolute_path, &credentials, &stat) != 0 ||
        stat.kind != VFS_INODE_DIR ||
        vfs_access_path(absolute_path, &credentials, VFS_ACCESS_EXEC) != 0) {
        return -1;
    }

    string_copy(current_task->cwd, absolute_path, sizeof(current_task->cwd));
    return 0;
}

int32_t proc_dup(uint32_t fd) {
    vfs_file_t *file = proc_lookup_fd(current_task, fd);
    int32_t new_fd;

    if (file == 0) {
        return -1;
    }

    new_fd = proc_allocate_fd_slot(current_task);
    if (new_fd < 0) {
        return -1;
    }

    vfs_retain_file(file);
    current_task->fd_table[(uint32_t)new_fd] = file;
    return new_fd;
}

int32_t proc_dup2(uint32_t oldfd, uint32_t newfd) {
    vfs_file_t *file = proc_lookup_fd(current_task, oldfd);

    if (file == 0 || current_task == 0 || newfd >= VFS_MAX_FDS) {
        return -1;
    }

    if (oldfd == newfd) {
        return (int32_t)newfd;
    }

    if (current_task->fd_table[newfd] != 0) {
        (void)proc_close_fd(newfd);
    }

    vfs_retain_file(file);
    current_task->fd_table[newfd] = file;
    return (int32_t)newfd;
}

uint32_t proc_getuid(void) {
    return current_task != 0 ? current_task->uid : 0u;
}

uint32_t proc_getgid(void) {
    return current_task != 0 ? current_task->gid : 0u;
}

int32_t proc_setuid(uint32_t uid) {
    if (current_task == 0 || current_task->uid != 0u) {
        return -1;
    }

    current_task->uid = uid;
    return 0;
}

int32_t proc_setgid(uint32_t gid) {
    if (current_task == 0 || current_task->uid != 0u) {
        return -1;
    }

    current_task->gid = gid;
    return 0;
}

int32_t proc_setsid(void) {
    if (current_task == 0) {
        return -1;
    }

    current_task->session_id = current_task->pid;
    current_task->process_group_id = current_task->pid;
    current_task->tty_name[0] = '\0';
    return (int32_t)current_task->session_id;
}

int32_t proc_gettty(char *buffer, uint32_t length) {
    uint32_t tty_length;

    if (current_task == 0 || buffer == 0 || length == 0u || current_task->tty_name[0] == '\0') {
        return -1;
    }

    tty_length = string_length(current_task->tty_name);
    if ((tty_length + 1u) > length) {
        return -1;
    }

    string_copy(buffer, current_task->tty_name, length);
    return (int32_t)tty_length;
}

int32_t proc_pipe(int32_t *fds) {
    vfs_file_t *read_file = 0;
    vfs_file_t *write_file = 0;
    int32_t read_fd;
    int32_t write_fd;

    if (current_task == 0 || fds == 0) {
        return -1;
    }

    read_fd = proc_allocate_fd_slot(current_task);
    if (read_fd < 0) {
        return -1;
    }

    write_fd = proc_allocate_fd_slot_excluding(current_task, read_fd);
    if (write_fd < 0) {
        return -1;
    }

    if (vfs_create_pipe(&read_file, &write_file) != 0 ||
        proc_install_fd(current_task, read_fd, read_file) < 0 ||
        proc_install_fd(current_task, write_fd, write_file) < 0) {
        if (read_file != 0) {
            vfs_close_file(read_file);
        }
        if (write_file != 0) {
            vfs_close_file(write_file);
        }
        current_task->fd_table[(uint32_t)read_fd] = 0;
        if ((uint32_t)write_fd < VFS_MAX_FDS) {
            current_task->fd_table[(uint32_t)write_fd] = 0;
        }
        return -1;
    }

    fds[0] = read_fd;
    fds[1] = write_fd;
    return 0;
}

static int proc_copy_exec_args(const char *default_argv0,
                               uint32_t argv_user,
                               const char **argv,
                               char arg_storage[EXEC_MAX_ARGS][EXEC_ARG_MAX],
                               uint32_t *argc_out) {
    uint32_t index = 0u;

    if (argv == 0 || argc_out == 0) {
        return -1;
    }

    if (argv_user != 0u) {
        for (index = 0u; index < EXEC_MAX_ARGS; index++) {
            uint32_t arg_address = 0u;

            if (user_copy_from_in(current_task->page_directory_phys,
                                  &arg_address,
                                  argv_user + (index * sizeof(uint32_t)),
                                  sizeof(uint32_t)) != 0) {
                return -1;
            }

            if (arg_address == 0u) {
                break;
            }

            if (user_copy_string_from_in(current_task->page_directory_phys, arg_storage[index], arg_address, EXEC_ARG_MAX) != 0) {
                return -1;
            }

            argv[index] = arg_storage[index];
        }

        if (index == EXEC_MAX_ARGS) {
            uint32_t terminator = 0u;

            if (user_copy_from_in(current_task->page_directory_phys,
                                  &terminator,
                                  argv_user + (index * sizeof(uint32_t)),
                                  sizeof(uint32_t)) != 0 ||
                terminator != 0u) {
                return -1;
            }
        }
    }

    if (index == 0u) {
        argv[0] = default_argv0;
        index = 1u;
    }

    *argc_out = index;
    return 0;
}

uint32_t proc_sys_exit(interrupt_frame_t *frame, int32_t status) {
    if (current_task == 0) {
        return (uint32_t)(uintptr_t)frame;
    }

    task_close_files(current_task);
    proc_reparent_children(current_task);

    current_task->exit_status = status;
    current_task->wait_target_pid = -1;
    current_task->wait_status_user = 0u;
    task_clear_blocked_io(current_task);
    current_task->state = TASK_ZOMBIE;
    current_task->reap_ready = current_task->parent_pid == 0u ? 1u : 0u;

    proc_notify_parent_exit(current_task);

    return proc_schedule(frame);
}

uint32_t proc_sys_waitpid(interrupt_frame_t *frame, int32_t pid, uint32_t status_user) {
    task_t *child;

    if (current_task == 0 || current_task->kind != TASK_USER || !interrupt_from_user(frame)) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    if (pid == 0 || (status_user != 0u &&
        !proc_user_buffer_writable(current_task->page_directory_phys, status_user, sizeof(int32_t)))) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    child = proc_find_zombie_child(current_task, pid);
    if (child != 0) {
        (void)proc_collect_zombie_now(current_task, child, status_user, frame);
        return (uint32_t)(uintptr_t)frame;
    }

    if (!proc_has_child(current_task, pid)) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    current_task->wait_target_pid = pid;
    current_task->wait_status_user = status_user;
    current_task->block_reason = TASK_BLOCK_WAITPID;
    current_task->state = TASK_BLOCKED;
    return proc_schedule(frame);
}

uint32_t proc_sys_read(interrupt_frame_t *frame, uint32_t fd, uint32_t buffer_user, uint32_t length) {
    char staging[PROC_IO_CHUNK];
    vfs_file_t *file;
    uint32_t done = 0u;

    if (current_task == 0 || buffer_user == 0u) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    if (length == 0u) {
        frame->eax = 0u;
        return (uint32_t)(uintptr_t)frame;
    }

    file = proc_lookup_fd(current_task, fd);
    if (file == 0) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    while (done < length) {
        uint32_t chunk = length - done;
        int32_t read_result;

        if (chunk > sizeof(staging)) {
            chunk = sizeof(staging);
        }

        read_result = vfs_read_file(file, staging, chunk);
        if (read_result == VFS_ERR_WOULD_BLOCK) {
            if (done != 0u || !interrupt_from_user(frame)) {
                frame->eax = done != 0u ? done : (uint32_t)-1;
                return (uint32_t)(uintptr_t)frame;
            }

            current_task->block_reason = TASK_BLOCK_IO_READ;
            current_task->blocked_fd = fd;
            current_task->blocked_user_buffer = buffer_user;
            current_task->blocked_length = length;
            current_task->blocked_done = 0u;
            current_task->state = TASK_BLOCKED;
            return proc_schedule(frame);
        }

        if (read_result < 0) {
            frame->eax = done != 0u ? done : (uint32_t)-1;
            return (uint32_t)(uintptr_t)frame;
        }

        if (read_result == 0) {
            break;
        }

        if (interrupt_from_user(frame)) {
            if (user_copy_to(buffer_user + done, staging, (uint32_t)read_result) != 0) {
                frame->eax = done != 0u ? done : (uint32_t)-1;
                return (uint32_t)(uintptr_t)frame;
            }
        } else {
            copy_bytes((void *)(uintptr_t)(buffer_user + done), staging, (uint32_t)read_result);
        }

        done += (uint32_t)read_result;
        if ((uint32_t)read_result < chunk) {
            break;
        }
    }

    frame->eax = done;
    return (uint32_t)(uintptr_t)frame;
}

uint32_t proc_sys_write(interrupt_frame_t *frame, uint32_t fd, uint32_t buffer_user, uint32_t length) {
    char staging[PROC_IO_CHUNK];
    const char *buffer = (const char *)(uintptr_t)buffer_user;
    vfs_file_t *file;
    uint32_t done = 0u;

    if (current_task == 0 || buffer_user == 0u) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    if (length == 0u) {
        frame->eax = 0u;
        return (uint32_t)(uintptr_t)frame;
    }

    file = proc_lookup_fd(current_task, fd);
    if (file == 0) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    while (done < length) {
        uint32_t chunk = length - done;
        int32_t write_result;

        if (chunk > sizeof(staging)) {
            chunk = sizeof(staging);
        }

        if (interrupt_from_user(frame)) {
            if (user_copy_from(staging, buffer_user + done, chunk) != 0) {
                frame->eax = done != 0u ? done : (uint32_t)-1;
                return (uint32_t)(uintptr_t)frame;
            }
        } else {
            copy_bytes(staging, &buffer[done], chunk);
        }

        write_result = vfs_write_file(file, staging, chunk);
        if (write_result == VFS_ERR_WOULD_BLOCK) {
            if (done != 0u || !interrupt_from_user(frame)) {
                frame->eax = done != 0u ? done : (uint32_t)-1;
                return (uint32_t)(uintptr_t)frame;
            }

            current_task->block_reason = TASK_BLOCK_IO_WRITE;
            current_task->blocked_fd = fd;
            current_task->blocked_user_buffer = buffer_user;
            current_task->blocked_length = length;
            current_task->blocked_done = 0u;
            current_task->state = TASK_BLOCKED;
            return proc_schedule(frame);
        }

        if (write_result < 0) {
            frame->eax = done != 0u ? done : (uint32_t)-1;
            return (uint32_t)(uintptr_t)frame;
        }

        done += (uint32_t)write_result;
        if ((uint32_t)write_result < chunk) {
            break;
        }
    }

    frame->eax = done;
    return (uint32_t)(uintptr_t)frame;
}

uint32_t proc_sys_pipe(interrupt_frame_t *frame, uint32_t fds_user) {
    int32_t fds[2];

    if (current_task == 0 || fds_user == 0u) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    if (proc_pipe(fds) != 0) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    if (interrupt_from_user(frame)) {
        if (user_copy_to(fds_user, fds, sizeof(fds)) != 0) {
            (void)proc_close_fd((uint32_t)fds[0]);
            (void)proc_close_fd((uint32_t)fds[1]);
            frame->eax = (uint32_t)-1;
            return (uint32_t)(uintptr_t)frame;
        }
    } else {
        copy_bytes((void *)(uintptr_t)fds_user, fds, sizeof(fds));
    }

    frame->eax = 0u;
    return (uint32_t)(uintptr_t)frame;
}

uint32_t proc_sys_exec(interrupt_frame_t *frame, uint32_t path_user) {
    return proc_sys_execv(frame, path_user, 0u);
}

uint32_t proc_sys_execv(interrupt_frame_t *frame, uint32_t path_user, uint32_t argv_user) {
    char path[PROC_PATH_MAX];
    char absolute_path[PROC_PATH_MAX];
    char arg_storage[EXEC_MAX_ARGS][EXEC_ARG_MAX];
    const char *argv[EXEC_MAX_ARGS];
    uint32_t argc = 0u;
    vfs_credentials_t credentials;
    exec_image_t old_image;
    exec_image_t new_image;
    interrupt_user_frame_t *new_frame;
    uint32_t old_directory;
    uint32_t new_directory;

    if (current_task == 0 || current_task->kind != TASK_USER || !interrupt_from_user(frame)) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    task_reset_image(&new_image);

    if (user_copy_string_from(path, path_user, sizeof(path)) != 0) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    if (proc_normalize_path(current_task, path, absolute_path, sizeof(absolute_path)) != 0 ||
        proc_copy_exec_args(path_basename(absolute_path), argv_user, argv, arg_storage, &argc) != 0) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    proc_fill_credentials(current_task, &credentials);
    if (vfs_access_path(absolute_path, &credentials, VFS_ACCESS_EXEC) != 0) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    new_directory = paging_create_address_space();
    if (new_directory == 0u) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    if (exec_load_path(new_directory, absolute_path, &new_image) != 0 ||
        exec_prepare_stack(new_directory, &new_image, argv, argc) != 0) {
        exec_release_image(new_directory, &new_image);
        paging_destroy_address_space(new_directory);
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    old_directory = current_task->page_directory_phys;
    old_image = current_task->image;

    current_task->page_directory_phys = new_directory;
    current_task->image = new_image;
    task_set_name(current_task, path_basename(absolute_path));
    new_frame = (interrupt_user_frame_t *)(current_task->kstack.stack_top - sizeof(interrupt_user_frame_t));
    task_prepare_user_frame(new_frame, new_image.entry_point, new_image.stack_top);
    current_task->saved_esp = (uint32_t)(uintptr_t)new_frame;

    paging_switch_directory(new_directory);
    gdt_set_kernel_stack((uint32_t)current_task->kstack.stack_top);

    exec_release_image(old_directory, &old_image);
    if (old_directory != 0u && old_directory != paging_page_directory_phys()) {
        paging_destroy_address_space(old_directory);
    }

    return current_task->saved_esp;
}

uint32_t proc_sys_fork(interrupt_frame_t *frame) {
    interrupt_user_frame_t *child_frame;
    const interrupt_user_frame_t *parent_frame = (const interrupt_user_frame_t *)(uintptr_t)frame;
    task_t *child = task_find_unused();

    if (current_task == 0 || current_task->kind != TASK_USER || !interrupt_from_user(frame) || child == 0) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    if (task_setup_kernel_stack(child) != 0) {
        task_reset(child);
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    if (vfs_clone_fds(child->fd_table, current_task->fd_table, VFS_MAX_FDS) != 0) {
        task_release_resources(child);
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    child->page_directory_phys = paging_create_address_space();
    if (child->page_directory_phys == 0u) {
        task_release_resources(child);
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    if (exec_clone_image(current_task->page_directory_phys, child->page_directory_phys, &current_task->image, &child->image) != 0) {
        task_release_resources(child);
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    child_frame = (interrupt_user_frame_t *)(child->kstack.stack_top - sizeof(interrupt_user_frame_t));
    copy_bytes(child_frame, parent_frame, sizeof(*child_frame));
    child_frame->base.eax = 0u;

    child->kind = TASK_USER;
    child->saved_esp = (uint32_t)(uintptr_t)child_frame;
    task_set_name(child, current_task->name);
    child->pid = next_pid++;
    child->parent_pid = current_task->pid;
    string_copy(child->cwd, current_task->cwd, sizeof(child->cwd));
    child->uid = current_task->uid;
    child->gid = current_task->gid;
    child->session_id = current_task->session_id;
    child->process_group_id = current_task->process_group_id;
    string_copy(child->tty_name, current_task->tty_name, sizeof(child->tty_name));
    child->state = TASK_RUNNABLE;

    frame->eax = child->pid;

    console_write("proc: fork child pid 0x");
    console_write_hex32(child->pid);
    console_write("\n");

    return (uint32_t)(uintptr_t)frame;
}

uint32_t proc_sys_sleep(interrupt_frame_t *frame, uint32_t ticks) {
    if (current_task == 0) {
        frame->eax = (uint32_t)-1;
        return (uint32_t)(uintptr_t)frame;
    }

    if (ticks == 0u) {
        frame->eax = 0u;
        return proc_schedule(frame);
    }

    current_task->wake_tick = pit_ticks() + ticks;
    current_task->state = TASK_SLEEPING;
    frame->eax = 0u;
    return proc_schedule(frame);
}
