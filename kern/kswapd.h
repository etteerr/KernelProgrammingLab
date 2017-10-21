//
// Created by Tom on 21/10/2017.
//

#ifndef KERNELPROGRAMMINGLAB_KSWAPD_H
#define KERNELPROGRAMMINGLAB_KSWAPD_H

#include "../inc/env.h"

void *kswapd_service(env_t * tf);
void kswapd_try_swap(page_info_t *page);

#endif //KERNELPROGRAMMINGLAB_KSWAPD_H
