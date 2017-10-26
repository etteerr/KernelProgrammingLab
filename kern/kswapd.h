//
// Created by Tom on 21/10/2017.
//

#ifndef KERNELPROGRAMMINGLAB_KSWAPD_H
#define KERNELPROGRAMMINGLAB_KSWAPD_H

#include "../inc/env.h"

void kswapd_service(env_t * tf);
void kswapd_start_service();
void kswapd_stop_service();
int kswapd_try_swap(page_info_t *page, int blocking);
int kswapd_direct_reclaim();
void kwswapd_set_threshold(float threshold);

#define KSWAP_ERR_NOT_ELIGIBLE -1
#define KSWAP_ERR_SWAP_FAULT -2

#endif //KERNELPROGRAMMINGLAB_KSWAPD_H
