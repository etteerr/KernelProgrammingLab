#include <inc/assert.h>
#include "../kern/vma.h"
#include "../inc/env.h"
#include "../inc/stdio.h"
#include "pmap.h"

void vma_array_init(env_t* e) {
    assert(e->vma_list == 0);
    
    /* page allocation and mapping */
    struct page_info * pp = page_alloc(ALLOC_ZERO);
    
    //user mapping
    uint32_t ret = 
            page_insert(e->env_pgdir, pp, (void*) VMA_UVA, PTE_BIT_USER | PTE_BIT_PRESENT);
    
    //Kernel mapping
    ret |= page_insert(e->env_pgdir, pp, (void*) VMA_KVA, PTE_BIT_RW | PTE_BIT_PRESENT);
    assert(ret==0);
    
    /* update env */
    vma_arr_t * vma_arr = (vma_arr_t*) VMA_KVA;
    e->vma_list = vma_arr;
}

void vma_array_destroy(env_t* e) {
    if (e->vma_list == 0)
        return;
    
    //TODO: interate full array, remove and unmap all regions
}

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
