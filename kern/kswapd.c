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

void kswapd_try_swap(page_info_t *pInfo);

/**
 * Counts the amount of physical pages currently in use.
 * @return int
 */
int get_mem_rss() {
    int i, rss_pages = 0;
    for(i = 0; i < npages; i++) {
        page_info_t *page = &pages[i];
        if(page->pp_ref > 0) {
            rss_pages++;
        }
    }
    return rss_pages;
}

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
    size_t headi = 0;
    dprintf("Kswapd service started as env %d.\n", tf->env_id);
    uint32_t check_ref_loop_iter = 0;
    
    while(running) {     
        /* Set new head */
        headi = (headi+1) % npages;
        page_info_t * head = &pages[headi];

        /* If page was accessed since last time we saw it, or it is free, skip it */
        /* Note: This is a short unintensive loop, loop for atleast 1000 iterations
         * or this loop's yield overhead will be enormous.
         */
        if(!head->pp_ref) {
            check_ref_loop_iter++;
            if (check_ref_loop_iter>1000) {
                check_ref_loop_iter = 0;
                kern_thread_yield(tf);
            }
            continue;
        }
        
        check_ref_loop_iter = 0;
        /* Small loop finished, now yield before the big work */
        kern_thread_yield(tf);

        
        if (!clear_last_access(head))
            continue;

        /* Don't do anything if memory pressure is low */
        if((get_mem_rss() / (float)npages) < mem_press_thresh) {
            continue;
        }

        kswapd_try_swap(head);
        
        /* Big work finished, yield */
        kern_thread_yield(tf);
    }

    dprintf("Kswapd service stopped.\n");
}

void kswapd_try_swap(page_info_t *page) {
    pte_t *pte;
    env_t *env = &envs[0];

    do {
        assert(env);

        //Set iterators to 0 for reverse pte
        uint16_t pgdir_i = 0, pte_i = 0;
        /* If any kernel thread/env holds this page in their pgdir, skip swapping it */
        if((env->env_type == ENV_TYPE_KERNEL_ENV ||
                env->env_type == ENV_TYPE_KERNEL_THREAD) &&
                reverse_pte_lookup_pgdir(env->env_pgdir, page, &pgdir_i, &pte_i)) {
            return;
        }

    } while ((env = env->env_link));
    
    /* Call swappy to page out page
     * Swappy will queue your request and remove page and all its references
     * And store the swapped page (not in that order)
     * 
     * can return swappy_error_queue_full
     */
    swappy_swap_page_out(page, 0);
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