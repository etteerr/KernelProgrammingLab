/* 
 * File:   swappy.h
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 *
 * Created on October 16, 2017, 9:53 AM
 */

#ifndef SWAPPY_H
#define SWAPPY_H
#include "kern/env.h"
enum {
    swappy_error_noerror=0,
    swappy_error_invaliddisk,
    swappy_error_allocation,
    swappy_error_noFreeSwapIndex,
    swappy_error_noRef,
    swappy_error_invalidId,
} swappy_error;

#define SWAPPY_SWAP_QUEUE 0
#define SWAPPY_SWAP_DIRECT 1

int swappy_retrieve_page(uint16_t page_id, page_info_t *pp);
int swappy_swap_page(page_info_t * pp, int swappy_swap_flag);
int swappy_init();

void swappy_start_service();
void swappy_stop_service();

void swappy_unit_test_case();
#endif /* SWAPPY_H */
