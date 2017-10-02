#include "pmap.h"

#include "../kern/vma.h"

#include "../inc/env.h"
#include "../inc/mmu.h"
#include "../inc/types.h"
#include "../inc/stdio.h"
#include "../inc/assert.h"
#include "../inc/string.h"
#include "../inc/memlayout.h"

void __dealloc_range(env_t *e, void *va, size_t len) {
    uint32_t i = (uint32_t) va & 0xFFFFF000; //round down to pgsize
    uint32_t end = (uint32_t) va + len;

    for(; i<end; i+= PGSIZE) {
        pte_t * pte = pgdir_walk(e->env_pgdir, (void*)i, 0);
        if (pte) {
            uint32_t pa = PTE_GET_PHYS_ADDRESS(*pte);
            if (pa) {
                struct page_info * pp = pa2page(pa);
                if (pp) {
                    page_decref(pp);
                    *pte = 0;
                    tlb_invalidate(e->env_pgdir, (void*)i);
                }
            }
        }
    }
}

void vma_array_init(env_t* e) {
    assert(e->vma_list == 0);
    
    /* page allocation and mapping */
    struct page_info * pp = page_alloc(ALLOC_ZERO);
    
    //user mapping
    uint32_t ret = 
            page_insert(e->env_pgdir, pp, (void*) VMA_UVA, PTE_BIT_USER | PTE_BIT_PRESENT);
    
    //Kernel mapping
//    ret |= page_insert(e->env_pgdir, pp, (void*) VMA_KVA, PTE_BIT_RW | PTE_BIT_PRESENT);
//    assert(ret==0);
        
    /* update env & set initial values */
    vma_arr_t * vma_arr = (vma_arr_t*) page2kva(pp);
    e->vma_list = vma_arr;
    
//    vma_arr->highest_va_vma = VMA_INVALID_INDEX;
    vma_arr->lowest_va_vma = VMA_INVALID_INDEX;
}

/**
 * Cleans the vma array and all its associated pgdir entries
 * @param e
 */
void vma_array_destroy(env_t* e) {
    if (e->vma_list == 0)
        return;
    
    vma_arr_t *vma_arr = e->vma_list;
    uint32_t i;
    vma_t *cur;

    for(i = 0; i < VMA_ARRAY_SIZE; i++) {
        cur = &vma_arr->vmas[i];
        if(cur->va) {
            __dealloc_range(e, cur->va, cur->len);
        }
    }

    /* Free vma_arr_t struct, which reserves an entire page */
    __dealloc_range(e, (void*)VMA_UVA, PGSIZE);
}

inline int vma_is_empty(vma_t* vma) {
    return vma->len == 0;
}

const uint8_t vma_get_index(vma_t* vma) {
    uint32_t begin = (uint32_t)(vma) & 0xFFFFF000;
    begin += sizeof(uint8_t) * 2;
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

void vma_set_backing(env_t* e, int vma_index, void * addr, uint32_t len) {
        vma_t * vma = &e->vma_list->vmas[vma_index];
        cprintf("Set backing on %#08x-%#08x to %#08x for %#08x\n", vma->va, (uint32_t)vma->va+vma->len, addr, len);

        vma->backed_addr = addr;
        vma->backsize = len;
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
    
    /* pg allign */
    len = ROUNDUP(len, PGSIZE);
    
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
    entry->va = (void*)((uint32_t)va & 0xFFFFF000);
    entry->backed_start_offset = (uint16_t)((uint32_t)va & 0xFFF);
    entry->len = len;
    entry->perm = perm;
    entry->type = type; 
    entry->backed_addr = 0;
    entry->backsize = 0;
    
    
   if (vma_lookup(e, va, len)!=0) {
       cprintf("Assertion failed in vma_new!\n");
       vma_dump_all(e);
        cprintf("To be inserted: ");
        if (entry)
            vma_dump(entry);
        cprintf("\n");
        memset((void*)entry, 0, sizeof(vma_t));
        return VMA_ERR_VMA_EXISTS;
   }
    
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
    
    
    /* Loop over all entries to find a comfortable spot */
    //Current, previous and next entry
    vma_t *centry, *pentry, *nentry;
    
    //bootstrap
    centry = pentry = 0;
    nentry = &vmar->vmas[vmar->lowest_va_vma];
    
    /* Check if we map in the first area */
    int bmap =  vma_get_relative(entry, nentry);
    
    /* Case: we map before the first vma, TODO: merge*/
    if (bmap == VMA_RELATIVE_BEFORE_NADJ || bmap == VMA_RELATIVE_AFTER_ADJ) {
        //Insert before first entry
        entry->n_adj = vmar->lowest_va_vma;
        entry->p_adj = VMA_INVALID_INDEX;
        vmar->lowest_va_vma = i;
        nentry->p_adj = i;
        return i;
    }
    
    //Loop while next != 0 (nentry)
    while(nentry) {
        //Shift and create pointers
        pentry = centry;
        centry = nentry;
        nentry = centry->n_adj == VMA_INVALID_POINTER ? 0 : &vmar->vmas[centry->n_adj];
        
        /* Quick and dirty position determination */
        int check = centry->va < va;
        check &= nentry ? nentry->va > va : 1;
        if (check) { 
            /* Insertion splot reached: insert between centry & nentry */
            
            //Our positional relation vs the current entry
            int pos_c = vma_get_relative(entry, centry);
            //Our positional relation vs the next entry
            int pos_n = nentry ? vma_get_relative(entry, nentry) : 0;
            
            
            /* Check merge conditions */
            
            //If we are adjacent to the current ( [current][us] )
            if (pos_c == VMA_RELATIVE_AFTER_ADJ) {
                /* merge with centry if permissions match */
                if (entry->perm == centry->perm && entry->type == centry->type && centry->backsize == 0) {
                    centry->len += entry->len;
                    
                    /* Clear our allocated entry */
                    memset((void*)entry, 0, sizeof(vma_t));
                    
                    /* return index */
                    return vma_get_index(centry);
                }
            }
            
            //If we are adjacent to the next entry ( [us][next] )
            if (pos_n == VMA_RELATIVE_BEFORE_ADJ) {
                /* merge with centry if permissions match */
                if (entry->perm == nentry->perm && entry->type == nentry->type && nentry->backsize == 0) {
                    nentry->len += (uint32_t)(nentry->va - entry->va);
                    nentry->va = entry->va;
                    
                    /* Clear our allocated entry */
                    memset((void*)entry, 0, sizeof(vma_t));
                    
                    /* return index */
                    return vma_get_index(pentry);
                }
            }
            
            /* Insert entry */
            if (va > centry->va) {
                entry->n_adj = nentry ? vma_get_index(nentry) : VMA_INVALID_POINTER;
                entry->p_adj = vma_get_index(centry);
                centry->n_adj = i;
                if (nentry) nentry->p_adj = i;
            }else {
                entry->n_adj = vma_get_index(centry);
                entry->p_adj = pentry ? vma_get_index(pentry) : VMA_INVALID_POINTER;
                centry->p_adj = i;
                if (pentry) pentry->n_adj = i;
            }
            
            return i;
        }
    }
    
    vma_dump_all(e);
    cprintf("To be inserted: ");
    if (entry->va)
        vma_dump(entry);
    cprintf("\n");
    panic("What are we doing here? This code should not be reached!");
    return 0;
}

int vma_new_range(env_t *e, size_t len, int perm, int type) {
    vma_t *cur, *next;
    uint8_t next_index;
    void *insert_va;
    vma_arr_t *arr = e->vma_list;
    
    if (len == 0) {
        cprintf("vma_new_range: len 0 will not be served\n");
        return -1;
    }

    if (arr->lowest_va_vma == VMA_INVALID_INDEX) {
        cprintf("VMA list is not populated, can't create new anonymous VMA.");
        return -1;
    }

    /* Find a gap that fits our len */
    uint64_t i = USTABDATA;
    uint64_t max = 0xFFFFFFFF - len;
    for(; i<max;) {
        /* Check if free */
        vma_t* res = vma_lookup(e, (void*)((uint32_t)i), len);
        
        /* We're back at start somehow */
        if (i==0)
            break;
        
        /* We're above USTACKTOP */
        if (i>= USTACKTOP)
            break;
        
        /* Check if vma_lookup found something */
        if (res==0) {
            /* Free space for us! */
            return vma_new(e, (void*)((uint32_t)i), len, perm ,type);
        }
        
        /* Is in use by vma res */
        i = (uint64_t)((uint32_t)res->va) + (uint64_t)res->len;
    }
    
    cprintf("vma_new_range: Did not find space with len %#08x\n", len);

    
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
            if (va > entry->va) {
                entry->len = (uint32_t)(va - entry->va);
                __dealloc_range(e, va, tmp1.len - entry->len);
            }
            else {
                vma_remove(e, entry);
                __dealloc_range(e, entry->va, entry->len);
            }

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
            if (vlen >= i_vlen) {
                vma_remove(e, entry);
                __dealloc_range(e, entry->va, entry->len);
            }
            else {//Else, shrink entry
                entry->va =  (void*)vlen;
                __dealloc_range(e, tmp1.va, vlen - (uint32_t)tmp1.va);
            }
        }
    }
    return 0;
}

vma_t *vma_lookup(env_t *e, void *_va, size_t len) {
    /* iterate vma's till va is found to be inrange */
    
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
    uint32_t va = (uint32_t) _va;
    endva = va +  len;
    
    if (va > endva) {
        cprintf("vma_lookup: Invalid length for start va!\n");
        return 0;
    }
        
    /* Loop while pva has not passed the va*/
    do {
        cva = (uint32_t) vmr[i].va;
        cendva = cva + vmr[i].len;
        assert(cva < cendva);
        
        /*If our current Va passes cva but does not pass cendva, we have our target*/
        if (va >= cva && va < cendva)
            return &vmr[i];
        
        /* Extra: if our endva passes cva, the given range spans atleast one vma*/
        if (va <= cva && endva > cva)
            return &vmr[i];
        
        /* Next index*/
        if ((i=vmr[i].n_adj)==VMA_INVALID_INDEX)
            return 0;
        
        /* if we passed cva with va but did not return, there is nothing anymore*/
    }while(va >= cva);
        
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
    
    /* Stop if there are no entries*/
    if (list->lowest_va_vma == VMA_INVALID_INDEX) {
        cprintf("\tNone.\n");
        return;
    }
    
    /* print entries */
    do {
        cprintf("\t");
        vma_dump(cur);
        cprintf("\n");
        if (cur->n_adj == VMA_INVALID_INDEX)
            break;
    } while ((cur = &list->vmas[cur->n_adj]));
}
