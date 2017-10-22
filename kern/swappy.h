/* 
 * File:   swappy.h
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 *
 * Created on October 16, 2017, 9:53 AM
 */

#ifndef SWAPPY_H
#define SWAPPY_H
#include "kern/env.h"
#include "pmap.h"

#define SWAPPY_PTE_TO_PAGEID(PTE) ((PTE_GET_PHYS_ADDRESS(PTE) - 1) >> 12)

enum {
    swappy_error_noerror = 0,
    swappy_error_invaliddisk,
    swappy_error_allocation,
    swappy_error_noFreeSwapIndex,
    swappy_error_noRef,
    swappy_error_invalidId,
} swappy_error;

/* Swappy_swap_flags */
#define SWAPPY_SWAP_QUEUE 0
#define SWAPPY_SWAP_DIRECT 1

/**
 * Queues a page for swapping (or direct swapping if SWAPPY_SWAP_DIRECT is given)
 * @param pp
 * @param swappy_swap_flag
 * @return 
 */
int swappy_swap_page_out(page_info_t * pp, int swappy_swap_flag);
/**
 * Queues a page for swapping (or direct swapping if SWAPPY_SWAP_DIRECT is given)
 * @param pageid
 * @param env
 * @param fault_va
 * @param swappy_swap_flag
 * @return 
 */
int swappy_swap_page_in(uint32_t pageid, env_t * env, void * fault_va, int swappy_swap_flag);
/**
 * Initializes swap queus and descriptors (+- 26 pages)
 * @return 
 */
int swappy_init();

/**
 * Starts the swappy service (swappy_init must be called before this)
 */
void swappy_start_service();
/**
 * Stops the swappy services
 */
void swappy_stop_service();

/**
 * Tests the direct swapping functions
 * note: Services are not tested!
 *       Neither are the traps
 */
void swappy_unit_test_case();
#endif /* SWAPPY_H */
