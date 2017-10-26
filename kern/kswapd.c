//
// Created by Tom on 21/10/2017.
//

#include "cpu.h"
#include "env.h"
#include "pmap.h"
#include "kswapd.h"
#include "swappy.h"
#include "kernel_threads.h"

#include "reverse_pagetable.h"
#include "../inc/stdio.h"
#include "../inc/assert.h"
#include "../inc/memlayout.h"

static char running = 1;
static float mem_press_thresh = 0.8;

/**
 * Clears last access bit in all env PTEs for given physical page,
 * and returns 1 if at least one PTE had the page marked as accessed,
 * or 0 otherwise.
 * @param page
 * @return int{0|1}
 */
int clear_last_access(page_info_t *page) {
    pte_t *pte;
    uint64_t iter = 0;
    int was_accessed = 0;

    while((pte = reverse_pte_lookup(page, &iter))) {
        if(*pte & PTE_BIT_ACCESSED) {
            was_accessed = 1;
            *pte ^= PTE_BIT_ACCESSED; /* Clear access bit */
        }
    }

    return was_accessed;
}

void kswapd_service(env_t * tf) {
    page_info_t *first = &pages[0];
    page_info_t *head = first;
    dprintf("Kswapd service started as env %d.\n", tf->env_id);

    while(running) {
        kern_thread_yield(tf);

        /* If we've reached the end, start at the beginning again */
        if(!head) {
            head = first;
            continue;
        }

        /* If page was accessed since last time we saw it, or it is free, skip it */
        if(clear_last_access(head) || !head->pp_ref) {
            continue;
        }

        /* Don't do anything if memory pressure is low */
        if((get_mem_rss() / (float)npages) < mem_press_thresh) {
            continue;
        }

        kswapd_try_swap(head, 0);

        /* Move to next page */
        head = head->pp_link;
    }

    dprintf("Kswapd service stopped.\n");
}

void kswapd_try_swap(page_info_t *page, int blocking) {
    pte_t *pte;
    env_t *env = &envs[0];
    uint16_t pgdir_i = 0, pte_i = 0;

    do {
        assert(env);

        /* If any kernel thread/env holds this page in their pgdir, skip swapping it */
        if((env->env_type == ENV_TYPE_KERNEL_ENV ||
                env->env_type == ENV_TYPE_KERNEL_THREAD) &&
                reverse_pte_lookup_pgdir(env->env_pgdir, page, &pgdir_i, &pte_i)) {
            return;
        }

        swappy_swap_page_out(page, blocking ? SWAPPY_SWAP_DIRECT : SWAPPY_SWAP_QUEUE);

    } while ((env = env->env_link));
}

void kswapd_start_service() {
    dprintf("Starting kswapd service...\n");
    running = 1;
    kern_thread_create(kswapd_service);
}

void kswapd_stop_service() {
    running = 0;
}

/* Allows configuring the threshold above which swapping will trigger.
 * Traps when called from non-priviliged code, because of permission pagefault. */
void kwswapd_set_threshold(float threshold) {
    mem_press_thresh = threshold;
}

int kswapd_direct_reclaim() {
    
}