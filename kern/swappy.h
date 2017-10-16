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
    swappy_status_uninitialized = 0,
    swappy_status_stopped,
    swappy_status_stopping,
    swappy_status_starting,
    swappy_status_started,
    swappy_status_crashed
};

page_info_t * swappy_retrieve_page(uint16_t page_id);
void swappy_set_swappyness(float swappyness);
int swappy_status();
void swappy_stop();
void swappy_start();
#endif /* SWAPPY_H */
