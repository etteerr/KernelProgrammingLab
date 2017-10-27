/* 
 * File:   swappy.c
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 * 
 * Created on October 16, 2017, 9:53 AM
 */

#include "swappy.h"
#include "ide.h"
#include "kernel_threads.h"
#include "inc/assert.h"
#include "pmap.h"
#include "inc/atomic_ops.h"
#include "inc/string.h"
#include "reverse_pagetable.h"
#include "vma.h"
#include "trap.h"
#include "sched.h"

typedef struct {
    void * fault_va;
    env_t * env;
} swappy_swapin_task;

/* swapped id Page structures */
typedef struct {
    volatile uint8_t ref; /* Set SWAPPY_REF_BIT_SIZE acordingly */
} __attribute__ ((packed)) swappy_swap_descriptor;

#define swappy_lock_aquire(LOCK) while(!sync_bool_compare_and_swap(&LOCK, 0, 1)) asm volatile("pause"); sync_barrier()
#define swappy_lock_release(LOCK) while(!sync_bool_compare_and_swap(&LOCK, 1, 0)) asm volatile("pause"); sync_barrier()

#define swappy_lock_aquire_yield(LOCK) while(!sync_bool_compare_and_swap(&LOCK, 0, 1)) kern_thread_yield(tf); sync_barrier()
#define swappy_lock_release_yield(LOCK) while(!sync_bool_compare_and_swap(&LOCK, 1, 0)) kern_thread_yield(tf); sync_barrier()

#define swappy_queue_size_swapout (PGSIZE/sizeof(uint32_t))
#define swappy_queue_size_swapin (PGSIZE/sizeof(swappy_swapin_task))

#define swappy_sectors_per_page (PGSIZE/SECTSIZE)
#define swappy_index_to_sector(IDX) (IDX * swappy_sectors_per_page)


/* number of bits for REF in swappy_swap_descriptor */
//Max references to a single swapped page = 2<<(SWAPPY_REF_BIT_SIZE - 1) -1
#define SWAPPY_REF_BIT_SIZE 8


/* Swappy descriptor (memory that describes what is on the swap disk) */
static swappy_swap_descriptor * swappy_desc_arr = 0;
static uint32_t descArrSize = 0;

/* Swappy lock for descriptor and writing operations */
static volatile int swappy_swap_lock = 0;

/* Swappy queue which holds pages to be swapped out */
static uint32_t * swappy_swap_queue_out = 0;
static uint32_t swappy_queue_poslock_out = 0;
static volatile uint32_t swappy_queue_read_pos_out = 0;
static volatile uint32_t swappy_queue_items_out = 0;

/* Swappy queue which holds pages to be swapped in */
static swappy_swapin_task * swappy_swap_queue_in = 0;
static uint32_t swappy_queue_poslock_in = 0;
static volatile uint32_t swappy_queue_read_pos_in = 0;
static volatile uint32_t swappy_queue_items_in = 0;

int swappy_allocate_descriptor(uint32_t descArrBytes, uint32_t required_pages) {
    if (swappy_desc_arr == 0) {
        dprintf("Allocating %d bytes (%d pages) for swap descriptor...\n", descArrBytes, required_pages);

        /* Allocate and reference */
        page_info_t *pp = alloc_consecutive_pages(required_pages, 0);

        if (!pp) {
            eprintf("Allocation failed\n");
            return -1;
        }

        for (int i = 0; i < required_pages; i++)
            page_inc_ref(pp + i);

        /* Get kernel address to beginning of allocated space */
        swappy_desc_arr = page2kva(pp);


        /* Test access to memory (set to 0, not done in allocation) */
        dprintf("Testing write to allocated memory...\n");
        memset((void*) swappy_desc_arr, 0, required_pages * PGSIZE);

        dprintf("Allocation successfull!\n");

    } else
        eprintf("Swap descriptor already allocated\n");

    return 0;
}

int swappy_allocate_queue() {
    dprintf("Initializing swapout queue, allocating 1 page (%d items)...\n", swappy_queue_size_swapout);
    page_info_t *pp = page_alloc(ALLOC_ZERO);

    if (!pp) {
        eprintf("Allocation failed!\n");
        return -1;
    }

    page_inc_ref(pp);
    swappy_swap_queue_out = page2kva(pp);

    dprintf("Initializing swapin queue, allocating 1 page (%d items)...\n", swappy_queue_size_swapin);
    pp = page_alloc(ALLOC_ZERO);

    if (!pp) {
        eprintf("Allocation failed!\n");
        return -1;
    }

    page_inc_ref(pp);
    swappy_swap_queue_in = page2kva(pp);

    return 0;
}

int swappy_init() {
    dprintf("Swapper initializing...\n");

    /* Test disk */
    //    ide_start_read(0,1);
    //    char buff[SECTSIZE];
    //    ide_read_sector(buff);
    //    if (strcmp(buff, "SWAP")==0) {
    //        eprintf("Invalid disk image: SWAP keyword not found!\n");
    //        return swappy_error_invaliddisk;
    //    }
    //    dprintf("Found valid diskimage\n");

    /* Setup swappy */
    dprintf("Setting up swap enviroment...\n");
    uint32_t numsec = ide_num_sectors();
    descArrSize = numsec / swappy_sectors_per_page;
    uint32_t descArrBytes = descArrSize * sizeof (swappy_swap_descriptor);
    uint32_t required_pages = (descArrBytes / PGSIZE) + ((descArrBytes % PGSIZE) > 0);
    dprintf("Found %d sectors on disk\n", numsec);
    dprintf("Sectors per page required: %d\n", swappy_sectors_per_page);
    dprintf("Page space in SWAP storage: %d\n", descArrSize);
    dprintf("Supported number of references per swapped page: %d\n", (2 << (SWAPPY_REF_BIT_SIZE - 1)) - 1);
    dprintf("Descriptor size for SWAP storage: %d Bytes\n", descArrBytes);

    /* Allocate memory for swap descriptor */
    if (swappy_allocate_descriptor(descArrBytes, required_pages))
        return swappy_error_allocation;

    /* Allocate memory for queue */
    if (swappy_allocate_queue())
        return swappy_error_allocation;

    return swappy_error_noerror;
}

static volatile int rw_lock = 0;
/**
 * Writes a page out to disk
 * @param pp
 * @param page_id
 * @param tf enviroment pointer, if set, causes write page to call kern_yield
 */
void swappy_write_page(page_info_t* pp, uint32_t page_id, env_t * tf) {
    if (tf) {
        swappy_lock_aquire_yield(rw_lock);
    } else {
        if (!sync_bool_compare_and_swap(&rw_lock, 0, 1)) {
            page_decref(pp);
            sched_yield();
        }
    }
    
    dddprintf("Swapping %p to swap index %d\n", pp, page_id);
    ide_start_write(swappy_index_to_sector(page_id), swappy_sectors_per_page);
    char * buffer = page2kva(pp);
    for (int w = 0; w < swappy_sectors_per_page; w++) {
        if (tf) {
            while (!ide_is_ready()) kern_thread_yield(tf);
        } else
            while (!ide_is_ready()) asm volatile("pause");

        ide_write_sector(buffer + (w * SECTSIZE));
    }
    swappy_lock_release(rw_lock);
}

void swappy_read_page(page_info_t* pp, uint16_t page_id, env_t* tf) {
    if (tf) {
        swappy_lock_aquire_yield(rw_lock);
    } else {
        if (!sync_bool_compare_and_swap(&rw_lock, 0, 1)) {
            page_decref(pp);
            sched_yield();
        }
    }
    dddprintf("Unswapping index %d to page %p\n", page_id, pp);
    char * buffer = page2kva(pp);
    ide_start_read(swappy_index_to_sector(page_id), swappy_sectors_per_page);
    for (int i = 0; i < swappy_sectors_per_page; i++) {
        if (tf) {
            while (!ide_is_ready()) kern_thread_yield(tf);
        } else
            while (!ide_is_ready()) asm volatile("pause");
        ide_read_sector(buffer + (SECTSIZE * i));
    }
    swappy_lock_release(rw_lock);
}

void swappy_decref(uint32_t index) {
    sync_sub_and_fetch(&swappy_desc_arr[index].ref, 1);
}

/**
 * Retrieves page from swap and stores it in pp
 *  Does not touch the page_info reference counter!
 * @param page_id the descriptor index
 * @param pp a free allocated page to store the page
 * @return swappy_error
 */
int swappy_retrieve_page(uint16_t page_id, page_info_t *pp, env_t * tf) {
    /* Aquire lock */
    swappy_lock_aquire(swappy_swap_lock);

    /* load page from disk if it was referenced */
    if (!swappy_desc_arr[page_id].ref) {
        /* No reference */
        eprintf("No reference to swap id %d found!\n", page_id);
        swappy_lock_release(swappy_swap_lock);
        return swappy_error_noRef;
    }
    
    /* Check page id */
    assert(page_id != (uint16_t) (-1));

    /* Read from disk */
    swappy_read_page(pp, page_id, tf);

    /* decref swap reference */
    swappy_decref(page_id);

    /* Release lock */
    swappy_lock_release(swappy_swap_lock);
    return 0;
}

uint32_t swappy_find_free_descriptor() {
    uint32_t i = 0;
    for (; i < descArrSize; i++) {
        if (swappy_desc_arr[i].ref == 0)
            return i;
    }

    return -1;
}

uint32_t swappy_incref(uint32_t index) {
    return sync_add_and_fetch(&swappy_desc_arr[index].ref, 1);
}

/**
 * Removes all references in page tables to page and decrefs pages for each
 * pte set to swap
 * @param pp
 * @param index index of swap descriptor
 */
void swappy_RemRef_mpage(page_info_t* pp, uint32_t index) {
    uint64_t it = 0;
    pte_t * pte;
    uint32_t numref = 0;
    while ((pte = reverse_pte_lookup(pp, &it)) != 0) {
        swappy_incref(index);
        (*pte) &= 0x1E; //reset address, preserve settings, except present
        (*pte) |= (index + 1) << 12; //set address of pte to index of swap
        page_decref(pp);
        numref++;
    }
    if (numref){
        ddprintf("Page swapped and %d references were found.\n", numref);
    }else{ 
        eprintf("Page swapped and %d references were found.\n", numref);
    }
}

/**
 * Swaps out a page
 *  if tf is set to a ENV_TYPE_KERNEL_THREAD type env
 *  makes use of kern_yield_thread while writing to disk
 * @param pp
 * @param tf
 * @return 
 */
int swappy_swap_out(page_info_t * pp, env_t * tf) {
    if (tf)
        if (tf->env_type != ENV_TYPE_KERNEL_THREAD)
            tf = 0;
    
//    if (!pp->c0.reg.swappable) {
//        eprintf("Unswappable page: %p\n", page2pa(pp));
//        return swappy_error_unswappable_page;
//    }
    /* Aquire lock */
    swappy_lock_aquire(swappy_swap_lock);

    /* To ensure successfull workings, this order is used:
     * 
     * Try to write a page to disk
     *  - Return on failure
     * Try set all pte_t's to disk id
     *  - panic on failure
     * Finally dealloc page
     *  - panic on failure
     * 
     * This ensures that...
     *  - when a page is written to disk it is still available 
     *    in memory as the pte_t are still pointing to the oirginal page in mem.
     *  - That all pte_t's are set to swap state before the page is deallocated.
     *  - When one pte_t is set to swap, there is a valid swap entry.
     */

    /* Check if pp is referenced */
    if (!pp->pp_ref) {
        eprintf("Not allowed to swap unreferenced page!\n");
        swappy_lock_release(swappy_swap_lock);
        return swappy_error_noRef;
    }

    /* Find a free spot */
    uint32_t free_index = swappy_find_free_descriptor();
    if (free_index == (uint32_t) - 1) {
        eprintf("No free index found\n");
        swappy_lock_release(swappy_swap_lock);
        return swappy_error_noFreeSwapIndex;
    }

    /* Try to write page to disk (Cannot fail?) */
    swappy_write_page(pp, free_index, tf);

    /* set all pte_t's to 0 */
    swappy_RemRef_mpage(pp, free_index);

    /* Free lock */
    swappy_lock_release(swappy_swap_lock);

    return 0;
}

int swappy_queue_insert_swapout(page_info_t* pp, int blocking) {
    static volatile int lock = 0;

    assert(pp->c0.reg.swappable);
    /* Aquire lock (only one queue'er should be writing) */
    swappy_lock_aquire(lock);

    ddprintf("Swapping page %p (pa: %p; num: %d)\n", pp, page2pa(pp), PGNUM(page2pa(pp)));

    /* Check if there is space in the queue */
    if (blocking) {
        while (swappy_queue_items_out >= swappy_queue_size_swapout)
            asm volatile("pause");
    }else if (swappy_queue_items_out >= swappy_queue_size_swapout) {
        ddprintf(KRED"Swappy queue full\n");
        return swappy_error_queue_full;
    }

    /* get write position from read position + items left */
    swappy_lock_aquire(swappy_queue_poslock_out);
    /* Get current writing pos */
    uint32_t writepos = swappy_queue_items_out + swappy_queue_read_pos_out;
    /* Add to n items */
    uint32_t nitems = sync_add_and_fetch(&swappy_queue_items_out, 1);
    swappy_lock_release(swappy_queue_poslock_out);
    writepos %= swappy_queue_size_swapout;

    /* Write to queue */
    swappy_swap_queue_out[writepos] = (uint32_t) pp;


    dddprintf("swapout queue length: %d\n", nitems);

    /* Unlock */
    swappy_lock_release(lock);
    
    return swappy_error_noerror;
}

void swappy_thread_retrieve_page(env_t* tf, swappy_swapin_task task) {
    static volatile int lock = 0;

    swappy_lock_aquire(lock);

    pte_t opte = *pgdir_walk(task.env->env_pgdir, task.fault_va, 0);
    uint32_t pageId = SWAPPY_PTE_TO_PAGEID(opte);

    /*  asserts  */
    assert(task.env->env_status == ENV_WAITING_SWAP);
    assert((opte & PTE_BIT_PRESENT) == 0);

    /* Allocate page for env */
    dprintf("Allocating page for env %d: va %p...\n", task.env->env_id, task.fault_va);
    page_info_t *pp; //will be overwritten so no zero
    while( (pp = page_alloc(0)) == 0) {
        if (tf) {
            kern_thread_yield(tf);
        }else{
            task.env->env_status = ENV_RUNNABLE;
            swappy_lock_release(lock);
            sched_yield();
        }
    }

    /* Swap in */
    if (pp) {
        dprintf("Allocation successfull, swapping in page from index %d...\n", pageId);
        /* Allocation succesfull, set page to be swappable */
        if (swappy_retrieve_page(pageId, pp, tf)) {
            eprintf("Error while swapping page %p!\n", pp);
            panic("Error while swapping!");
        }

        /* Insert page and make env runnable */
        dprintf("Page swapin for env %d: %p successfull, inserting...\n", task.env->env_id, task.fault_va);
        dprintf("First value in page: %p (read address: %p)\n", *((uint32_t*)page2kva(pp)), task.fault_va);
        page_insert(task.env->env_pgdir, pp, task.fault_va, opte & 0x1FF);
        task.env->env_status = ENV_RUNNABLE;
        pp->c0.reg.swappable = 1;

    } else {
        eprintf("Failed to allocate new page for env %d!\n", task.env->env_id);
        murder_env(task.env, (uint32_t) task.fault_va);
    }

    swappy_lock_release(lock);
}

int swappy_queue_insert_swapin(swappy_swapin_task task, int blocking) {
    static volatile int lock = 0;

    /* Aquire lock (only one queue'er should be writing) */
    swappy_lock_aquire(lock);


    /* Check if there is space in the queue */
    if (blocking) {
        while (swappy_queue_items_in >= swappy_queue_size_swapin)
            asm volatile("pause");
    }else if(swappy_queue_items_in >= swappy_queue_size_swapin)
        return swappy_error_queue_full;

    /* get write position from read position + items left */
    swappy_lock_aquire(swappy_queue_poslock_in);
    uint32_t writepos = swappy_queue_items_in + swappy_queue_read_pos_in;
    swappy_lock_release(swappy_queue_poslock_in);
    writepos %= swappy_queue_size_swapin;

    /* Write to queue */
    swappy_swap_queue_in[writepos] = task;

    /* Add to n items */
    uint32_t nitems = sync_add_and_fetch(&swappy_queue_items_in, 1);

    dprintf("swapin queue length: %d\n", nitems);

    /* Unlock */
    swappy_lock_release(lock);
    
    return swappy_error_noerror;
}

int swappy_swap_page_out(page_info_t * pp, int flags) {

    assert(pp);
    
    assert(pp->pp_ref);
    
    assert(!(pp->c0.reg.kernelPage || pp->c0.reg.unclaimable || pp->c0.reg.bios));
    
    /* Direct swapping if needed */
    if (flags & SWAPPY_SWAP_DIRECT) {
        return swappy_swap_out(pp, 0);
    }

    /* Normal swapping (Give to a queue) */
    return swappy_queue_insert_swapout(pp, flags & SWAPPY_SWAP_BLOCKING);
}

int swappy_swap_page_in(env_t * env, void * fault_va, int flags) {
    /* Assemble task */
    swappy_swapin_task task;
    task.env = env;
    task.fault_va = fault_va;

    /* Direct swapping if needed */
    if (flags & SWAPPY_SWAP_DIRECT) {
        swappy_thread_retrieve_page((env_t*) 0, task);
        return 0;
    }

    /* Normal swapping (Give to a queue) */
    return swappy_queue_insert_swapin(task, flags & SWAPPY_SWAP_BLOCKING);
}

/* Swappy service running variable */
static int running = 0;

void swappy_service_swapout(env_t * tf) {
    static int lock = 0;

    dprintf("Swappy service swapout started as env %d.\n", tf->env_id);

    while (running) {
        /* yield */
        kern_thread_yield(tf);

        /* No items, nothing to swap */
        if (!swappy_queue_items_out)
            continue;

        /* Assert pointers */
        assert(swappy_swap_queue_out);

        /* Aquire locks */
        swappy_lock_aquire(lock);
        swappy_lock_aquire(swappy_queue_poslock_out);

        /* Get first in line in queue */
        page_info_t * pp = (page_info_t *) swappy_swap_queue_out[swappy_queue_read_pos_out];
        uint32_t items_remain = sync_sub_and_fetch(&swappy_queue_items_out, 1);
        swappy_queue_read_pos_out = (swappy_queue_read_pos_out+1) % swappy_queue_size_swapout;

        /* Release position lock, we have our page data */
        swappy_lock_release(swappy_queue_poslock_out);

        /* Swap out */
        if (pp) {
            if (swappy_swap_out(pp, tf)) {
                eprintf("Error while swapping page %p!\n", pp);
                panic("Error while swapping!");
            }
            ddprintf("%d items remaining in swapout queue\n", items_remain);
        } else {
            eprintf("0 pointer in queue at index %d!\n", swappy_queue_read_pos_out);
            //Todo: remove panic
            panic("Panic!");
        }

        /* release lock */
        swappy_lock_release(lock);
    }

    dprintf("Swappy service swapout has stopped\n");
}

void swappy_service_swapin(env_t * tf) {
    static int lock = 0;

    dprintf("Swappy service swapin started as env %d.\n", tf->env_id);

    while (running) {
        /* yield */
        kern_thread_yield(tf);

        /* No items, nothing to swap */
        if (!swappy_queue_items_in)
            continue;

        /* Assert pointers */
        assert(swappy_swap_queue_in);

        /* Aquire locks */
        swappy_lock_aquire(lock);
        swappy_lock_aquire(swappy_queue_poslock_in);

        /* Get first in line in queue */
        swappy_swapin_task task = swappy_swap_queue_in[swappy_queue_read_pos_in];
        sync_sub_and_fetch(&swappy_queue_items_in, 1);
        sync_add_and_fetch(&swappy_queue_read_pos_in, 1);

        /* Release position lock, we have our page data */
        swappy_lock_release(swappy_queue_poslock_in);

        /* get pte */
        swappy_thread_retrieve_page(tf, task);

        /* release lock */
        swappy_lock_release(lock);
    }

    dprintf("Swappy service swapin has stopped\n");
}

void swappy_start_service() {
    dprintf("Starting swappy services...\n");

    running = 1;

    kern_thread_create(swappy_service_swapout);
    kern_thread_create(swappy_service_swapin);
}

void swappy_stop_service() {
    dprintf("Stopping swappy services\n");
    running = 0;
}

void swappy_unit_test_case() {
    dprintf("Starting test...\n");
    
    uint32_t testaddr = 0x0d000000;
    uint32_t test_table = testaddr/(4 << 20);
    /* store first kern_pgdir entry */
    pde_t kpde = kern_pgdir[test_table];

    /* Set hack to include kernel pgdir */
    reverse_pagetable_look_kern = 1;

    /* Alloc page and insert */
    dprintf("Allocate and insert page.\n");
    page_info_t * pp = page_alloc(ALLOC_ZERO);
    dprintf("Allocated page %p->%p\n", pp, page2pa(pp));
    pp->c0.reg.swappable = 1;
    page_insert(kern_pgdir, pp, (void*) testaddr, PTE_BIT_RW);

    /* assert page is present */
    dprintf("Assert page is present.\n");
    assert(*pgdir_walk(kern_pgdir, (void*) testaddr, 0) && PTE_BIT_PRESENT);

    /* Write test value to page */
    dprintf("Write to allocated memory.\n");
    uint32_t * mem = (uint32_t *) testaddr;
    *mem = 0xDEADBEEF;

    /* Swap out */
    dprintf("Swap page.\n");
    if (swappy_swap_page_out(pp, SWAPPY_SWAP_DIRECT))
        panic("Swappy returned error!");

    /* Assert page is swapped */
    dprintf("Assert page is swapped.\n");
    assert(pp->pp_ref == 0);
    pte_t pte = *pgdir_walk(kern_pgdir, (void*) testaddr, 0);
    assert((pte & PTE_BIT_PRESENT) == 0);

    /* Alloc page again (make sure we already have the pte!) */
    dprintf("Allocate new page & insert.\n");
    pp = page_alloc(ALLOC_ZERO);
    dprintf("Allocated page %p->%p\n", pp, page2pa(pp));
    page_insert(kern_pgdir, pp, (void*) testaddr, 0);

    /* retrieve page */
    dprintf("retrieve page from swap.\n");
    uint32_t id = SWAPPY_PTE_TO_PAGEID(pte);
    if (swappy_retrieve_page(id, pp, 0))
        panic("Swappy returned a error!");

    /* Check if mem is still valid */
    dprintf("Check memory is still the same.\n");
    assert(*mem == 0xDEADBEEF);

    /* REstore pte to 0 */
    dprintf("Cleaning...\n");
    *pgdir_walk(kern_pgdir, (void*) testaddr, 0) = 0;

    /* Free page */
    page_decref(pp);

    /* REstore hack */
    reverse_pagetable_look_kern = 0;

    /* Clean the pgdir at testaddr */
    page_info_t* addr = pa2page(PDE_GET_ADDRESS(kern_pgdir[test_table]));
    page_decref(addr);
    kern_pgdir[test_table] = kpde;
    dprintf("Test successfull!.\n");
}
