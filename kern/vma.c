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
        
    /* update env & set initial values */
    vma_arr_t * vma_arr = (vma_arr_t*) VMA_KVA;
    e->vma_list = vma_arr;
    
    vma_arr->highest_va_vma = VMA_INVALID_POINTER;
    vma_arr->lowest_va_vma = VMA_INVALID_POINTER;
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
    /* iterate vma's till va is found to be inrange */
    
    vma_arr_t * vml = e->vma_list;
    vma_t * vmr = e->vma_list->vmas;
    vma_t * res = 0;

    
    uint32_t i = vml->lowest_va_vma;
    
    if (i == VMA_INVALID_POINTER)
        return 0;
    
    void *endva, * pva = 0;
    pva = vmr[i].va;
    
    /* Loop while pva has not passed the va*/
    while(va >= pva ) {
        endva = pva + (void *) vmr[i].len;
        
        /* If the endva has passed the va here, this is our vma entry*/
        if (va <= endva)
            return &vmr[i];
        
        /* If the next pointer is invalid, ow no... nothing found!*/
        if (vmr[i].n_adj == VMA_INVALID_POINTER)
            return 0;
        
        /* Next address range in line */
        i = vmr[i].n_adj;
    }
    
    return 0;
}

void vma_dump_all(env_t *e) {
    vma_arr_t *list = e->vma_list;
    vma_t *cur = &list->vmas[list->lowest_va_vma];

    do {
        cprintf("%08x - %08x", cur->va, cur->va + cur->len);
    } while ((cur = &list->vmas[cur->n_adj]));
}
