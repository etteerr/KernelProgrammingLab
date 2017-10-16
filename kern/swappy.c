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


/* swapped id Page structures */
typedef struct {
    unsigned page_id:16;
    unsigned next:16;
    unsigned free:1;
    //Hellopadding
} swappy_swap_descriptor ;

/* Global variables */
/* These variables are volatile and thus omit the buffer 
 * Thus they should be nearly atomic and thats enough
 */

//Describes the current status of the service (started, stopped, etc)
static volatile int swappy_status_ = 0;
//Describes swappyness (same as in linux), when to start swapping (.6 => start at 60% mem usage)
static volatile int swappy_swappyness = 0.6; //swapping starts at 60% usage


/* swappy_service variables */
static page_info_t * allocated_page = 0;
static uint8_t nallocated_pages = 0;
static const swappy_swap_descriptor * swap_desc = (swappy_swap_descriptor *) 0x4000;
static uint32_t nsectors = 0;
//Swappy tapframe
static volatile env_t *swappy_tf = 0;
static volatile uint32_t swappy_lock = 0;



void swappy_spin_lock() {
    while(sync_bool_compare_and_swap(&swappy_lock, 0, 1)) 
        asm volatile("nop\n");
}

void swappy_unlock() {
    while(sync_bool_compare_and_swap(&swappy_lock, 1, 0)) 
        asm volatile("nop\n");
}

void swappy_yield_lock(env_t *tf) {
    while(sync_bool_compare_and_swap(&swappy_lock, 0, 1)) 
        kern_thread_yield(tf);
}

/**
 * Initializes the swappy pgdir by inserting pages
 * @param tf
 */
int swappy_insert_pages(env_t* tf){
    for(int i = 0; i<nallocated_pages; i++)
        if (page_insert(tf->env_pgdir, allocated_page+i, (void*)(swap_desc) + i*PGSIZE, PTE_BIT_RW))
            return -1;
    
    return 0;
}

int swappy_setup_memory(env_t* tf){
    if (!allocated_page || !nallocated_pages) {
        if (allocated_page || nallocated_pages)
            eprintf("Unclean swappy state! There may be a memory leak!\n");
        
        /* Determine number of pages to alloc */
        nsectors = ide_num_sectors();
        uint32_t size = nsectors * sizeof(swappy_swap_descriptor);
        npages = size / PGSIZE + (size % PGSIZE != 0);
        allocated_page = alloc_consecutive_pages(npages, ALLOC_ZERO);
        
        if (allocated_page==0) {
            npages = 0;
            eprintf("Failed to allocate %u pages!\n", npages);
            swappy_unlock();
            return -1;
        }
    }
    
    if (swappy_insert_pages(tf)) {
        eprintf("Failed to insert swappy pages during initialization\n");
        return -1;
    }
    
    dprintf("%u page(s) successfully allocated and inserted!\n", npages);
    return 0;
}

/**
 * The kernel thread swappy service
 * @param tf
 */
void swappy_service(env_t *tf) {
    /* Lock during init */
    swappy_yield_lock(tf);
    
    /* Init swappy service */
    swappy_setup_memory(tf);
    
    /* unlock */
    swappy_unlock();
    
    /* Do swappy things */
}

void swappy_set_swappyness(float swappyness) {
    assert(swappyness >= 0);
    assert(swappyness <= 1);
    swappy_swappyness = swappyness;
}

int swappy_status() {
    return swappy_status_;
}
void swappy_stop() {
    
}
void swappy_start() {
    if (swappy_status_ == swappy_status_uninitialized) {
        ide_init();
    }
    
    /* Create service */
    swappy_status_ = swappy_status_starting;
    kern_thread_create(swappy_service);
}

page_info_t * swappy_retrieve_page(uint16_t page_id) {
    
    
    return 0;
}