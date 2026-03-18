#ifndef RIVERIX_EXEC_H
#define RIVERIX_EXEC_H

#include <stdint.h>

#define EXEC_MAX_MAPPED_PAGES 32u
#define EXEC_USER_STACK_TOP 0x00800000u
#define EXEC_MAX_ARGS 8u
#define EXEC_ARG_MAX 64u

typedef struct exec_image {
    uintptr_t entry_point;
    uintptr_t stack_top;
    uintptr_t mapped_virtuals[EXEC_MAX_MAPPED_PAGES];
    uint32_t mapped_pages[EXEC_MAX_MAPPED_PAGES];
    uint32_t mapped_page_count;
} exec_image_t;

int exec_load_path(uint32_t directory_phys, const char *path, exec_image_t *image);
int exec_prepare_stack(uint32_t directory_phys, exec_image_t *image, const char *const *argv, uint32_t argc);
int exec_clone_image(uint32_t source_directory_phys, uint32_t target_directory_phys, const exec_image_t *source_image, exec_image_t *target_image);
void exec_release_image(uint32_t directory_phys, exec_image_t *image);

#endif
