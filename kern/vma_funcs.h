//
// Created by Tom on 23/09/2017.
//

#ifndef KERNELPROGRAMMINGLAB_VMA_FUNCS_H
#define KERNELPROGRAMMINGLAB_VMA_FUNCS_H

#include <inc/env.h>

int vma_new(struct env *e, void *va, size_t len, int perm);
int vma_unmap(struct env *e, void *va, size_t len);
vma_t *vma_lookup(struct env *e, void *va);
void vma_dump_all(struct env *e);

#endif //KERNELPROGRAMMINGLAB_VMA_FUNCS_H
