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

#define swappy_lock_aquire(LOCK) while(!sync_val_compare_and_swap(&LOCK, 0, 1)) asm volatile("pause")
#define swappy_lock_free(LOCK) while(!sync_val_compare_and_swap(&LOCK, 1, 0)) asm volatile("pause")

#define swappy_queue_size (PGSIZE/sizeof(uint32_t))

/* number of bits for ID and REF in swappy_swap_descriptor */
//Max swap pages supported = 2<<(SWAPPY_ID_BIT_SIZE - 1) -1
#define SWAPPY_ID_BIT_SIZE 16
//Max references to a single swapped page = 2<<(SWAPPY_REF_BIT_SIZE - 1) -1
#define SWAPPY_REF_BIT_SIZE 8

/* swapped id Page structures */
typedef struct {
    unsigned page_id:SWAPPY_ID_BIT_SIZE;
    unsigned ref:SWAPPY_REF_BIT_SIZE;
}__attribute__((packed)) swappy_swap_descriptor ;

static swappy_swap_descriptor * swappy_desc_arr = 0;

static uint32_t * swappy_swap_queue = 0;
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
    uint32_t sectorsPerPage = PGSIZE/SECTSIZE;
    uint32_t descArrSize = numsec/sectorsPerPage;
    uint32_t descArrBytes = descArrSize * sizeof(swappy_swap_descriptor);
    uint32_t required_pages = (descArrBytes / PGSIZE) + ((descArrBytes % PGSIZE) > 0);
    dprintf("Found %d sectors on disk\n", numsec);
    dprintf("Sectors per page required: %d\n", sectorsPerPage);
    dprintf("Page space in SWAP storage: %d\n", descArrSize);
    dprintf("Supported number of pages in swap: %d\n", (2<<(SWAPPY_ID_BIT_SIZE-1))-1);
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

page_info_t * swappy_retrieve_page(uint16_t page_id){
    
    return 0;
}
void swappy_queue_insert(page_info_t* pp){
    static volatile int lock = 0;
    
    /* Aquire lock (only one queue'er should be writing) */
    swappy_lock_aquire(lock);
    
    dprintf("Swapping page %p (pa: %p; num: %d)\n", pp, page2pa(pp), PGNUM(page2pa(pp)));
    
    /* Check if there is space in the queue */
    uint32_t items;
    while((items=swappy_swap_queue_items) >= swappy_queue_size)
        asm volatile("pause");
    
    /* get write position from read position + items left */
    uint32_t writepos = items + swappy_swap_queue_read_pos;
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