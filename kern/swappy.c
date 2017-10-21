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

#define swappy_lock_aquire(LOCK) while(!sync_val_compare_and_swap(&LOCK, 0, 1)) asm volatile("pause"); sync_barrier()
#define swappy_lock_free(LOCK) while(!sync_val_compare_and_swap(&LOCK, 1, 0)) asm volatile("pause"); sync_barrier()

#define swappy_queue_size (PGSIZE/sizeof(uint32_t))

#define swappy_sectors_per_page (PGSIZE/SECTSIZE)
#define swappy_index_to_sector(IDX) (IDX * swappy_sectors_per_page)


/* number of bits for REF in swappy_swap_descriptor */
//Max references to a single swapped page = 2<<(SWAPPY_REF_BIT_SIZE - 1) -1
#define SWAPPY_REF_BIT_SIZE 8


/* swapped id Page structures */
typedef struct {
    volatile uint8_t ref; /* Set SWAPPY_REF_BIT_SIZE acordingly */
}__attribute__((packed)) swappy_swap_descriptor ;

/* Swappy descriptor (memory that describes what is on the swap disk) */
static swappy_swap_descriptor * swappy_desc_arr = 0;
static uint32_t descArrSize = 0;

/* Swappy lock for descriptor and writing operations */
static volatile int swappy_swap_lock = 0;

/* Swappy queue which holds pages to be swapped */
static uint32_t * swappy_swap_queue = 0;
static uint32_t swappy_queue_poslock = 0;
static volatile uint32_t swappy_swap_queue_read_pos = 0;
static volatile uint32_t swappy_swap_queue_items = 0;

int swappy_allocate_descriptor(uint32_t descArrBytes, uint32_t required_pages){
    if (swappy_desc_arr==0) {
        dprintf("Allocating %d bytes (%d pages) for swap descriptor...\n", descArrBytes, required_pages);
        
        /* Allocate and reference */
        page_info_t *pp = alloc_consecutive_pages(required_pages, 0);
        
        if (!pp) {
            eprintf("Allocation failed\n");
            return -1;
        }
            
        for(int i = 0; i < required_pages; i++)
            page_inc_ref(pp+i);
        
        /* Get kernel address to beginning of allocated space */
        swappy_desc_arr = page2kva(pp);
        
        
        /* Test access to memory (set to 0, not done in allocation) */
        dprintf("Testing write to allocated memory...\n");
        memset((void*)swappy_desc_arr, 0, required_pages * PGSIZE);
        
        dprintf("Allocation successfull!\n");
        
    }else
        eprintf("Swap descriptor already allocated\n");
    
    return 0;
}

int swappy_allocate_queue(){
    dprintf("Initializing swap queue, allocating 1 page...\n");
    page_info_t *pp = page_alloc(ALLOC_ZERO);
    
    if (!pp) {
        eprintf("Allocation failed!\n");
        return -1;
    }
    
    page_inc_ref(pp);
    swappy_swap_queue = page2kva(pp);
    
    return 0;
}

int swappy_init() {
    dprintf("Swapper initializing...\n");
    
    /* Test disk */
    ide_start_read(0,1);
    char buff[SECTSIZE];
    ide_read_sector(buff);
    if (strcmp(buff, "SWAP")==0) {
        eprintf("Invalid disk image: SWAP keyword not found!\n");
        return swappy_error_invaliddisk;
    }
    dprintf("Found valid diskimage\n");
    
    /* Setup swappy */
    dprintf("Setting up swap enviroment...\n");
    uint32_t numsec = ide_num_sectors();
    descArrSize = numsec/swappy_sectors_per_page;
    uint32_t descArrBytes = descArrSize * sizeof(swappy_swap_descriptor);
    uint32_t required_pages = (descArrBytes / PGSIZE) + ((descArrBytes % PGSIZE) > 0);
    dprintf("Found %d sectors on disk\n", numsec);
    dprintf("Sectors per page required: %d\n", swappy_sectors_per_page);
    dprintf("Page space in SWAP storage: %d\n", descArrSize);
    dprintf("Supported number of references per swapped page: %d\n", (2<<(SWAPPY_REF_BIT_SIZE-1))-1);
    dprintf("Descriptor size for SWAP storage: %d Bytes\n", descArrBytes);
    
    /* Allocate memory for swap descriptor */
    if (swappy_allocate_descriptor(descArrBytes, required_pages))
        return swappy_error_allocation;
    
    /* Allocate memory for queue */
    if (swappy_allocate_queue())
        return swappy_error_allocation;
    
    return swappy_error_noerror;
}

void swappy_read_page(uint16_t page_id, page_info_t* pp){
    char * buffer = page2kva(pp);
    ide_start_read(swappy_index_to_sector(page_id), swappy_sectors_per_page);
    for(int i = 0; i < swappy_sectors_per_page; i++)
        ide_read_sector(buffer+(SECTSIZE*i));
}

void swappy_decref(){
    sync_sub_and_fetch(&swappy_desc_arr[index].ref, 1);
}

/**
 * Retrieves page from swap and stores it in pp
 *  Does not touch the page_info reference counter!
 * @param page_id the descriptor index
 * @param pp a free allocated page to store the page
 * @return swappy_error
 */
int swappy_retrieve_page(uint16_t page_id, page_info_t *pp){
    /* Aquire lock */
    swappy_lock_aquire(swappy_swap_lock);
    
    /* load page from disk if it was referenced */
     if (!swappy_desc_arr[page_id].ref){
         /* No reference */
         return swappy_error_noRef;
     }
    
    /* Read from disk */
    swappy_read_page(page_id, pp);
    
    /* decref swap reference */
    swappy_decref();
    
    /* Release lock */
    swappy_lock_free(swappy_swap_lock);
    return 0;
}

uint32_t swappy_find_free_descriptor(){
    uint32_t i = 0;
    for(; i < descArrSize; i++) {
        if (swappy_desc_arr[i].ref == 0)
            return i;
    }
    
    return -1;
}

void swappy_write_page(page_info_t* pp, uint32_t free_index){
    ide_start_write(swappy_index_to_sector(free_index), swappy_sectors_per_page);
    char * buffer = page2kva(pp);
    for(int w=0; w<swappy_sectors_per_page; w++)
        ide_write_sector(buffer+(w*SECTSIZE));
}

uint32_t swappy_incref(uint32_t index){
    return sync_add_and_fetch(&swappy_desc_arr[index].ref, 1);
}

/**
 * Removes all references in page tables to page and decrefs pages for each
 * pte set to swap
 * @param pp
 * @param index index of swap descriptor
 */
void swappy_RemRef_mpage(page_info_t* pp, uint32_t index){
    uint64_t it = 0;
    pte_t * pte;
    while ((pte=reverse_pte_lookup(pp, &it))!=0) {
        swappy_incref(index);
        *pte &= 0x1E; //reset address, preserve settings, except present
        *pte |= index << 11; //set address of pte to index of swap
        page_decref(pp);
    }
}

int swappy_swap_out(page_info_t * pp) {    
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
    
    /* Find a free spot */
    uint32_t free_index = swappy_find_free_descriptor();
    if (free_index == (uint32_t)-1) {
        eprintf("No free index found\n");
        return swappy_error_noFreeSwapIndex;
    }
    
    /* Try to write page to disk (Cannot fail?) */
    swappy_write_page(pp, free_index);
    
    /* set all pte_t's to 0 */
    swappy_RemRef_mpage(pp, free_index);
    
    /* Free lock */
    swappy_lock_free(swappy_swap_lock);
    
    return 0;
}

void swappy_queue_insert(page_info_t* pp){
    static volatile int lock = 0;
    
    /* Aquire lock (only one queue'er should be writing) */
    swappy_lock_aquire(lock);
    
    dprintf("Swapping page %p (pa: %p; num: %d)\n", pp, page2pa(pp), PGNUM(page2pa(pp)));
    
    /* Check if there is space in the queue */
    while(swappy_swap_queue_items >= swappy_queue_size)
        asm volatile("pause");
    
    /* get write position from read position + items left */
    swappy_lock_aquire(swappy_queue_poslock);
    uint32_t writepos = swappy_swap_queue_items + swappy_swap_queue_read_pos;
    swappy_lock_free(swappy_queue_poslock);
    writepos %= swappy_queue_size;
    
    /* Write to queue */
    swappy_swap_queue[writepos] = (uint32_t) pp;
    
    /* Add to n items */
    uint32_t nitems = sync_fetch_and_add(&swappy_swap_queue_items, 1);
    
    dprintf("swap queue length: %n", nitems);
    
    /* Unlock */
    swappy_lock_free(lock);
}

int swappy_swap_page(page_info_t * pp, int flags){
    
    /* Direct swapping if needed */
    if (flags & SWAPPY_SWAP_DIRECT) {
        
        return 0;
    }
    
    /* Normal swapping (Give to a queue) */
    swappy_queue_insert(pp);
    
    return 0;
}