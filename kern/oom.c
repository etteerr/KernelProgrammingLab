//
// Created by Tom on 26/10/2017.
//

#include "env.h"
#include "pmap.h"
#include "../inc/env.h"
#include "../inc/stdio.h"

/**
 * Returns amount of non-shared physical pages present in env pgdir.
 * @param env
 * @return
 */
int env_rss(env_t *env) {
    pde_t pde;
    pte_t pte;
    int i, j, env_rss = 0;

    for(i = 0; i < 1024; i++) {
        pde = env->env_pgdir[i];
        if(pde & PDE_BIT_PRESENT) {
            continue;
        }

        for(j = 0; j < 1024; j++) {
            pte = *((pte_t *)KADDR(PDE_GET_ADDRESS(pde)));
            if(pte & PTE_BIT_PRESENT) {
                page_info *page = pa2page(PTE_GET_PHYS_ADDRESS(pte));

                /* Ignored unused (ref 0) or shared (ref >1) pages */
                if(page->pp_ref == 1) {
                    env_rss++;
                }
            }
        }
    }

    return env_rss;
}

/**
 * Returns the env with the most non-shared physical pages.
 * @return
 */
env_t *find_max_rss_env() {
    int i, cur_rss, max_rss = 0;
    env_t *cur, *max = 0;

    for(i = 0; i < NENV; i++) {
        cur = &envs[i];
        cur_rss = env_rss(cur);
        if(cur_rss >= max_rss) {
            max_rss = cur_rss;
            max = cur;
        }
    }

    return max;
}

/**
 * Kills the env with the most non-shared physical pages,
 * to maximise the amount of physical memory we can reclaim directly.
 * @return
 */
int oom_kill() {
    env_t *bad_guy = find_max_rss_env();
    ddprintf("Invoking OOM killer on env id %d", bad_guy->env_id);
    env_destroy(bad_guy);
}