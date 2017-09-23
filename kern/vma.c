#include <kern/vma.h>
#include <kern/vma_funcs.h>

int vma_new(struct env *e, void *va, size_t len, int perm) {
    return 0;
}

int vma_unmap(struct env *e, void *va, size_t len) {
    return 0;
}

vma_t *vma_lookup(struct env *e, void *va) {
    return NULL;
}

void vma_dump_all(struct env *e) {
}
