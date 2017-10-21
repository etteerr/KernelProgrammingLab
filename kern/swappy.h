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
};

page_info_t * swappy_retrieve_page(uint16_t page_id);
int swappy_swap_page(page_info_t * pp);
int swappy_init();
#endif /* SWAPPY_H */
