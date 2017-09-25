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
    
//    vma_arr->highest_va_vma = VMA_INVALID_INDEX;
    vma_arr->lowest_va_vma = VMA_INVALID_INDEX;
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

uint8_t vma_get_index(vma_t* vma) {
    uint32_t begin = (uint32_t)(vma) & 0xFFFFF000;
    uint32_t offset = (uint32_t)(vma) - begin;
    return offset/sizeof(vma_t);
}

void vma_remove(env_t *e, vma_t * vma) {
    uint8_t idx = vma_get_index(vma);
    
    vma_t *p, *n;
    p = vma->p_adj == VMA_INVALID_POINTER ? 0 : &e->vma_list->vmas[vma->p_adj];
    n = vma->n_adj == VMA_INVALID_POINTER ? 0 : &e->vma_list->vmas[vma->n_adj];
    
    /* Make p->n && p<-n*/
    p->n_adj = vma->n_adj;
    n->p_adj = vma->p_adj;
    
    /* empty region */
    memset((void*)vma, 0, sizeof(vma_t));
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
    /* vma assertions */
    assert(len);
    
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
    if (vmar->lowest_va_vma == VMA_INVALID_INDEX) {
        vmar->lowest_va_vma = i;
        entry->n_adj = entry->p_adj = VMA_INVALID_INDEX;
        return i;
    }
    
    //All other cases
    
    //Bootstrap loop
    uint8_t *pp, 
            //pi is the index to which entry->p_adj should point, we always insert before the cent
            pi = VMA_INVALID_INDEX;
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
            
            //Insert before cent (THus not if entry is after cent)
            if (p != VMA_RELATIVE_AFTER_ADJ) {
                /* If our entry is before cent, insert our entry */
                //set our pointers
                entry->n_adj = *pp;
                entry->p_adj = pi;

                //set other entry pointers
                *pp = i; //set previous pointer to our position
                cent->p_adj = i; //Set next entry back pointer to us
                
                //inc vma counter
                vmar->occupied++;

                //We be done here, take a break
                return i;
            }
        }
        
        /* Check if we have reached the end */
        if (cent->n_adj == VMA_INVALID_INDEX) {
            /* Append our entry at the end of the linked list */
            entry->n_adj = VMA_INVALID_INDEX;
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

int vma_new_anon(env_t *e, size_t len, int perm) {
    vma_t *cur, *next;
    uint8_t next_index;
    void *insert_va;
    vma_arr_t *arr = e->vma_list;

    if (arr->lowest_va_vma == VMA_INVALID_INDEX) {
        cprintf("VMA list is not populated, can't create new anonymous VMA.");
        return -1;
    }

    /* Find a gap that fits our len */
    for(cur = &arr->vmas[arr->lowest_va_vma]; cur; cur = &arr->vmas[next_index]) {
        next_index = cur->n_adj;

        /* If current VMA has no next, we assume we can insert after it */
        if(next_index == VMA_INVALID_INDEX) {
            insert_va = cur->va + cur->len;
            return vma_new(e, insert_va, len, perm, VMA_ANON);
        }

        next = &arr->vmas[next_index];
        if(next->va - (cur->va + cur->len) <= len) {
            insert_va = cur->va + cur->len;
            return vma_new(e, insert_va, len, perm, VMA_ANON);
        }
    }
    return -1;
}

int vma_unmap(env_t *e, void *va, size_t len) {
    /* assertions */
    assert(len);
    
    /* Helper variables */
    uint32_t vlen = (uint32_t) va + len;
    
    /* Determine vma entry */
    vma_t * entry, tmp1;
    while((entry=vma_lookup(e, va, len))) {
        /* This entry lies in range of va-va+len */
        uint32_t i_vlen = (uint32_t) entry->va + entry->len;
        
        /* Save entry */
        tmp1 = *entry;
        
        /* beginning is equal or after this entry's begin */
        if (va >= entry->va) {
            /* unmap, and keep beginning if va > entry->va */
            if (va > entry->va)
                entry->len = (uint32_t)(va - entry->va);
            else
                vma_remove(e, entry);

            /* remap remainder if there is a remainder */
            if (vlen < i_vlen) {
                vma_new(e,(void*)vlen, i_vlen - vlen, tmp1.perm, tmp1.type);
            }
            
            /* Rest this case */
            continue;
        }
        
        /* Tail overlaps with entry */
        if (vlen > (uint32_t) entry->va) {
            /* If overlap is complete, remove */
            if (vlen >= i_vlen)
                vma_remove(e, entry);
            else //Else, shrink entry
                entry->va =  (void*)vlen;
        }
    }
    return 0;
}

vma_t *vma_lookup(env_t *e, void *_va, size_t len) {
    /* iterate vma's till va is found to be inrange */
    
    uint32_t va = (uint32_t) _va;
    
    vma_arr_t * vml = e->vma_list;
    vma_t * vmr = e->vma_list->vmas;

    
    uint32_t i = vml->lowest_va_vma;
    
    /* If the entry pointer is invalid, there are no entries*/
    if (i == VMA_INVALID_INDEX) {
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
        if ((i=vmr[i].n_adj)==VMA_INVALID_INDEX)
            return 0;
        
        /* if we passed cva with va but did not return, there is nothing anymore*/
    }while(va <= cva);
    
    return 0;
}

void vma_dump(vma_t * vma) {
    cprintf("%#08x - %#08x [", vma->va, vma->va + vma->len);
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
        if (cur->n_adj == VMA_INVALID_INDEX)
            break;
    } while ((cur = &list->vmas[cur->n_adj]));
}
