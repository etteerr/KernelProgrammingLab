/* 
 * File:   reverse_pagetable.h
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 *
 * Created on October 17, 2017, 3:47 PM
 */

#ifndef REVERSE_PAGETABLE_H
#define REVERSE_PAGETABLE_H
#include "inc/memlayout.h"
#include "pmap.h"
#include "env.h"


/* if set to 1, makes reverse_pte_lookup look in the kernel pgdir */
extern volatile int reverse_pagetable_look_kern;

/**
 * Find a pte that references the physical page described by [page]
 *  This function can be called repeatedly untill it returns 0 for incremental results
 * @param page the physical page, described by [page], to find
 * @param iter The non-volatile iterator
 * @return pte_t pointer if found, 0 if nothing was found
 */
pte_t * reverse_pte_lookup(page_info_t * page, uint64_t * iter) ;

/**
 * Looksup a given physical page [page] entry in given pagetable [pd]
 *  To enable repeated coninued iterations and returns (yield in python)
 *  Two pointers to iterators must be given: [pgdir_i] and [pte_i]
 *  Initialize [pgdir_i] and [pte_i] to zero everytime this function returns 0
 *  and on the first call
 *  This function can be called repeatedly untill it returns 0 for incremental results
 * @param pd page directory to search
 * @param page physical discriptor of the page to find
 * @param pgdir_i iterator for the page directory
 * @param pte_i iterator for the page table
 * @return pte_t pointer if found, 0 if nothing was found
 */
pte_t * reverse_pte_lookup_pgdir(pde_t * pd, page_info_t* page, uint16_t* pgdir_i, uint16_t* pte_i);

#endif /* REVERSE_PAGETABLE_H */
