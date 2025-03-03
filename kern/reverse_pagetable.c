/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   reverse_pagetable.c
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 * 
 * Created on October 17, 2017, 3:47 PM
 */

#include "reverse_pagetable.h"


volatile int reverse_pagetable_look_kern = 0;

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
pte_t * reverse_pte_lookup_pgdir(pde_t * pd, page_info_t* page, uint16_t* pgdir_i, uint16_t* pte_i){
    
    for(; *pgdir_i < (UTOP/(PGSIZE*1024)); (*pgdir_i)++) {
        /* entry must be present */
        if ((pd[*pgdir_i] & PDE_BIT_PRESENT)==0) {
            *pte_i = 0;
            continue;
        }
        
        //TODO: huge support
        if (pd[*pgdir_i] & PDE_BIT_HUGE) {
            *pte_i = 0;
            continue; //DOnt support huge pages for now
        }
        
        /* Enter pgtable */
        pte_t * pt = (pte_t * )KADDR(PDE_GET_ADDRESS(pd[*pgdir_i]));
        for(; *pte_i < 1024; (*pte_i)++) {
            uint32_t pa = PTE_GET_PHYS_ADDRESS(pt[*pte_i]);
            if (pa && pa2page(pa) == page) {
                //Our page, return pte
                return &pt[*pte_i];
            }
        }
    }
    
    return 0;
}

/**
 * Find a pte that references the physical page described by [page]
 *  This function can be called repeatedly untill it returns 0 for incremental results
 * @param page the physical page, described by [page], to find
 * @param iter The non-volatile iterator
 * @return pte_t pointer if found, 0 if nothing was found
 */
pte_t * reverse_pte_lookup(page_info_t * page, uint64_t * iter) {
    if (iter == 0) {
        eprintf("Invalid usage of this function: iter must be a valid pointer!\n");
        return 0;
    }
    
    /* Make 3 iters of iter */
    uint32_t *env_i;
    uint16_t *pgdir_i, *pte_i;
    
    env_i = ((uint32_t*)iter); //0-32 bits
    pgdir_i = (((uint16_t*)iter)+2); //32-48 bits
    pte_i = (((uint16_t*)iter)+3); //48 to 64 bits
    
    /* Iterate */
    /* Always reset iterators on continue (except for the current forloop or higher)*/
    for(; *env_i < NENV; (*env_i)++) {
        /* Make sure this env exists */
        if (envs[*env_i].env_status == ENV_FREE) {
            *pgdir_i = 0;
            *pte_i = 0;
            continue;
        }
        
        /* Also make sure it will still exist in the near future :P */
        if (envs[*env_i].env_status == ENV_DYING) {
            *pgdir_i = 0;
            *pte_i = 0;
            continue;
        }
        
        /* Its pgdir should exist as well! */
        if (envs[*env_i].env_pgdir==0) {
            *pgdir_i = 0;
            *pte_i = 0;
            continue;
        }
        
        /* Iterate its pgdir */
        pte_t * res = reverse_pte_lookup_pgdir(envs[*env_i].env_pgdir,page, pgdir_i, pte_i);
        if (res)
            return res;
        
        /* Reset iterators */
        *pgdir_i = 0;
        *pte_i = 0;
    }
    
    /* Search kernel page directory*/
    if (reverse_pagetable_look_kern) {
        
        pte_t * res = reverse_pte_lookup_pgdir(kern_pgdir,page, pgdir_i, pte_i);
        
        if (res)
            return res;
        
        /* Reset iterators */
        *pgdir_i = 0;
        *pte_i = 0;
    }
    
    /* Reset iter */
    *iter = 0;
    
    return 0;
}
