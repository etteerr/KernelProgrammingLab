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
static volatile int swappy_lock = 0;

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
    
    return swappy_error_noerror;
}

page_info_t * swappy_retrieve_page(uint16_t page_id){
    
    return 0;
}
int swappy_swap_page(page_info_t * pp){
    
    return 0;
}