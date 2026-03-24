#include "kernel/panic.h"

#include "kernel/console.h"
#include "kernel/trace.h"

void panic(const char *message) {
    if (message != 0) {
        console_write("panic: ");
        console_write(message);
        console_write("\n");
    } else {
        console_write("panic: fatal\n");
    }

    trace_log(SYS_TRACE_CATEGORY_PANIC, SYS_TRACE_EVENT_PANIC_FATAL, 0u, 0u, 0u);

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
