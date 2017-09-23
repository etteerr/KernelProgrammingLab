#include "../kern/vma.h"
#include "../inc/env.h"
#include "../inc/stdio.h"

int vma_new(env_t *e, void *va, size_t len, int perm) {

    return 0;
}

int vma_unmap(env_t *e, void *va, size_t len) {

    return 0;
}

vma_t *vma_lookup(env_t *e, void *va) {

    return 0;
}

void vma_dump_all(env_t *e) {
    vma_arr_t *list = e->vma_list;
    vma_t *cur = &list->vmas[list->lowest_va_vma];

    do {
        cprintf("%08x - %08x", cur->va, cur->va + cur->len);
    } while ((cur = &list->vmas[cur->n_adj]));
}
