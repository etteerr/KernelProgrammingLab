#include <inc/assert.h>
#include "../kern/vma.h"
#include "../inc/env.h"
#include "../inc/stdio.h"
#include "pmap.h"
#include "inc/string.h"

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
    
//    vma_arr->highest_va_vma = VMA_INVALID_POINTER;
    vma_arr->lowest_va_vma = VMA_INVALID_POINTER;
}

void vma_array_destroy(env_t* e) {
    if (e->vma_list == 0)
        return;
    
    //TODO: interate full array, remove and unmap all regions
    panic("Unimplemented");
}

inline int vma_is_empty(vma_t* vma) {
    return vma->len == 0;
}

int vma_get_relative(vma_t * vma1, vma_t * vma2) {
    uint32_t vlen1, vlen2, va1, va2;
    
    //Bootstrap values
    va1 = (uint32_t) vma1->va;
    va2 = (uint32_t) vma2->va;
    vlen1 = va1 + vma1->len;
    vlen2 = va2 + vma2->len;
    
    //Checks
    
    /* Cases overlap */
    if (va1 < va2 && vlen1 > va2)
        return VMA_RELATIVE_OVERLAP; // ( vma1 | vma2 |   vma1 )
    
    if (va1 > va2 && vlen2 > va1)
        return VMA_RELATIVE_OVERLAP; // ( vma2 | vma1 |   vma1 )
    
    /* Cases one before the other */
    if (vlen1 < va2) //If end of vma1 is before va2 begin
        return VMA_RELATIVE_BEFORE_NADJ; // | vma1 |   | vma2 |
    
    if (vlen2 < va1) //vma2 is before vma1
        return VMA_RELATIVE_AFTER_NADJ; //  | vma2 |   | vma2 |
    
    /* cases one adjacent to the other */
    if (vlen1 == va2) //If end of vma1 is before va2 begin
        return VMA_RELATIVE_BEFORE_ADJ; // | vma1 | vma2 |
    
    if (vlen2 == va1) //vma2 is before vma1
        return VMA_RELATIVE_AFTER_ADJ; //  | vma2 | vma2 |
    
    /* case: programmer fucked up */
    panic("This function is faulty! (and I'm now salty)");
}

int vma_new(env_t *e, void *va, size_t len, int perm, int type) {
    /* Create and map a empty vma and link in the va order */
    uint32_t i;
    vma_arr_t * vmar = e->vma_list;
    vma_t * entry;
    
    /* Find a free spot in the array */
    for(i = 0; i < VMA_ARRAY_SIZE; i++) {
        entry = &vmar->vmas[i];
        if (vma_is_empty(entry)) //if len is zero, we a sure its empty :) (or atleast useless)
            goto breaky;
    }
    entry = 0;
breaky:
    assert(entry);

    /* Fill entry values */
    entry->va = va;
    entry->len = len;
    entry->perm = perm;
    entry->type = type; 
    
    /* 
     * Fit it inside linked indexes 
     *  This checks if it will actually fit as well
     */
    //First entry case
    if (vmar->lowest_va_vma == VMA_INVALID_POINTER) {
        vmar->lowest_va_vma = i;
        entry->n_adj = entry->p_adj = VMA_INVALID_POINTER;
        return i;
    }
    
    //All other cases
    
    //Bootstrap loop
    uint8_t *pp, 
            //pi is the index to which entry->p_adj should point, we always insert before the cent
            pi = VMA_INVALID_POINTER;
    vma_t * cent = &vmar->vmas[vmar->lowest_va_vma]; //Select first in line
    pp = &vmar->lowest_va_vma; //pointer to current index of prev entry
    
    do {
        /* Check position of our entry vs the current iter */
        int p = vma_get_relative(entry, cent);
        
        /* Check for overlap */
        if (p==VMA_RELATIVE_OVERLAP) {
            memset((void*)entry, 0, sizeof(vma_t));
            return -1;
        }
        
        /* Check if we can insert here */
        if (p == VMA_RELATIVE_BEFORE_ADJ || p == VMA_RELATIVE_BEFORE_NADJ || p == VMA_RELATIVE_AFTER_ADJ) {
            /* Handle a possible merge case 1*/
            if (entry->perm == cent->perm && entry->type == cent->type) {
                //Identical permissions and type
                if (p == VMA_RELATIVE_BEFORE_ADJ || p == VMA_RELATIVE_AFTER_ADJ) {
                    /* Our new entry is continues with cent, with entry being before cent */
                    cprintf("VMA_new: Insertion merge for ");
                    vma_dump(entry);
                    cprintf(" and ");
                    vma_dump(cent);
                    cprintf(".\n");
                    
                    //Merge entries
                    cent->len += entry->len;
                    if (VMA_RELATIVE_BEFORE_ADJ) cent->va = entry->va; //if our entry is before cent, we use our entries va
                    
                    //Remove entry
                    memset((void*)entry, 0, sizeof(vma_t));
                    
                    //return index of cent
                    return *pp;                    
                }
            }
            
            if (p != VMA_RELATIVE_AFTER_ADJ) {
                /* If our entry is before cent, insert our entry */
                //set our pointers
                entry->n_adj = *pp;
                entry->p_adj = pi;

                //set other entry pointers
                *pp = i; //set previous pointer to our position
                cent->p_adj = i; //Set next entry back pointer to us

                //We be done here, take a break
                return i;
            }
        }
        
        /* Check if we have reached the end */
        if (cent->n_adj == VMA_INVALID_POINTER) {
            /* Append our entry at the end of the linked list */
            entry->n_adj = VMA_INVALID_POINTER;
            entry->p_adj = *pp; //previous pointer points to current, we insert after current
            //Update current
            cent->n_adj = i;
            return i;
        }
        
        /* Pancake, do increase the iterators */
        pp = &cent->n_adj; //previous link target pointer
        cent = &vmar->vmas[cent->n_adj]; //new interator value
        pi = cent->p_adj;
        
    }while(1);
            
    panic("What are we doing here? This code should not be reached!");
    return 0;
}

int vma_unmap(env_t *e, void *va, size_t len) {
    panic("Unimplemented");
    return 0;
}

vma_t *vma_lookup(env_t *e, void *_va, size_t len) {
    /* iterate vma's till va is found to be inrange */
    
    uint32_t va = (uint32_t) _va;
    
    vma_arr_t * vml = e->vma_list;
    vma_t * vmr = e->vma_list->vmas;

    
    uint32_t i = vml->lowest_va_vma;
    
    /* If the entry pointer is invalid, there are no entries*/
    if (i == VMA_INVALID_POINTER) {
        //Assert there are no entries and return
        assert(vml->occupied==0);
        return 0;
    }
    
    uint32_t cendva,  cva = 0,  endva;
    endva = va +  len;
        
    /* Loop while pva has not passed the va*/
    do {
        cva = (uint32_t) vmr[i].va;
        cendva = cva + vmr[i].len;
        
        /*If our current Va passes cva but does not pass cendva, we have our target*/
        if (va >= cva && va < cendva)
            return &vmr[i];
        
        /* Extra: if our endva passes cva, the given range spans atleast one vma*/
        if (endva >= cva)
            return &vmr[i];
        
        /* Next index*/
        if ((i=vmr[i].n_adj)==VMA_INVALID_POINTER)
            return 0;
        
        /* if we passed cva with va but did not return, there is nothing anymore*/
    }while(va <= cva);
    
    return 0;
}

void vma_dump(vma_t * vma) {
    cprintf("%08x - %08x [", vma->va, vma->va + vma->len);
    (vma->perm & VMA_PERM_READ)  ?  cprintf("r") : cprintf("-");
    (vma->perm & VMA_PERM_WRITE) ?  cprintf("w") : cprintf("-");
    (vma->perm & VMA_PERM_EXEC)  ?  cprintf("x") : cprintf("-");
    if (vma->type == VMA_ANON) cprintf(" anon");
    if (vma->type == VMA_BINARY) cprintf(" binary");
    if (vma->type == VMA_UNUSED) cprintf(" unused");
    cprintf("]");
}

void vma_dump_all(env_t *e) {
    vma_arr_t *list = e->vma_list;
    vma_t *cur = &list->vmas[list->lowest_va_vma];
    /* print header */
    cprintf("VMA dump for env %d\n", e->env_id);
    /* print entries */
    do {
        cprintf("\t");
        vma_dump(cur);
        cprintf("\n");
    } while ((cur = &list->vmas[cur->n_adj]));
}
