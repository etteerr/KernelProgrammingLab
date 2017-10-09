/* See COPYRIGHT for copyright information. */

#include "../inc/x86.h"
#include "../inc/env.h"
#include "../inc/types.h"
#include "../inc/error.h"
#include "../inc/string.h"
#include "../inc/assert.h"
#include "../inc/syscall.h"

#include "env.h"
#include "vma.h"
#include "pmap.h"
#include "trap.h"
#include "sched.h"
#include "syscall.h"
#include "console.h"
#include "../inc/memlayout.h"
#include "../inc/mmu.h"
#include "inc/atomic_ops.h"


/*
 * Print a string to the system console.
 * The string is exactly 'len' characters long.
 * Destroys the environment on memory errors.
 */
static void sys_cputs(const char *s, size_t len)
{
    /* Check that the user has permission to read memory [s, s+len).
     * Destroy the environment if not. */

    /* LAB 3: Your code here. */
    user_mem_assert(curenv, (void*) s, len, PTE_BIT_USER | PTE_BIT_PRESENT);
    /* Print the string supplied by the user. */
    cprintf("%.*s", len, s);
}

/*
 * Read a character from the system console without blocking.
 * Returns the character, or 0 if there is no input waiting.
 */
static int sys_cgetc(void)
{
    return cons_getc();
}

/* Returns the current environment's envid. */
static envid_t sys_getenvid(void)
{
    return curenv->env_id;
}

/*
 * Destroy a given environment (possibly the currently running environment).
 *
 * Returns 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid.
 */
static int sys_env_destroy(envid_t envid)
{
    int r;
    struct env *e;

    if ((r = envid2env(envid, &e, 1)) < 0)
        return r;
    if (e == curenv)
        cprintf("[%08x] exiting gracefully\n", curenv->env_id);
    else
        cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
    env_destroy(e);
    return 0;
}

/*
 * Creates a new anonymous mapping somewhere in the virtual address space.
 *
 * Supported flags: 
 *     MAP_POPULATE
 * 
 * Returns the address to the start of the new mapping, on success,
 * or -1 if request could not be satisfied.
 */
static void *sys_vma_create(size_t size, int perm, int flags)
{
    /* Virtual Memory Area allocation */
    int index = vma_new_range((env_t *)curenv, size, perm, VMA_ANON);

    if(index < 0) {
        return (void *)-1;
    }

    return curenv->vma_list->vmas[index].va;
}

/*
 * Unmaps the specified range of memory starting at 
 * virtual address 'va', 'size' bytes long.
 */
static int sys_vma_destroy(void *va, size_t size)
{
    if(size <= 0) {
        return -1;
    }
    return vma_unmap(curenv, va, size, 0);
}

/*
 * Deschedule current environment and pick a different one to run.
 */
static void sys_yield(void)
{
    sched_yield();
}

static int sys_wait(envid_t envid)
{
    /* LAB 5: Your code here */
    if(ENVX(envid) > NENV) {
        dprintf("Invalid envid %p\n", envid);
        return -1;
    }
    
    dprintf("%p shall now wait for %p\n", curenv->env_id, envid);

    env_t *env = &envs[envid];
    env_t *cur = (env_t *)curenv; /* IDE's macro unfolding is broken */

    if(!env || !cur) {
        dprintf("%p cannot wait for %p, %p does not exist!\n", curenv->env_id, envid, envid);
        return -1;
    }
    
    cur->env_status = ENV_WAITING;
    cur->waiting_for = envid;
    
    return 0;
}

/**
 * Fork a pagetable
 *  Allocate new pgtable for child
 *  Copy pgtable
 *  Make entries COW when needed
 * @param ppdir
 * @param cpdir
 * @param i
 * @return 
 */
int fork_pgtable_cow(env_t *pe, pde_t* ppdir, pde_t* cpdir, uint32_t i){
    //Define COW'able pte enrty
    uint32_t pte_small_check = PTE_BIT_PRESENT | PTE_BIT_USER;
    
    if (!(ppdir[i] & PDE_BIT_PRESENT))
        return 0;

    /* Set table pointers */
    pte_t * ppt = KADDR(PDE_GET_ADDRESS(ppdir[i]));
    pte_t * cpt = KADDR(PDE_GET_ADDRESS(cpdir[i]));
  
    for(uint32_t j = 0; j< 1024; j++) {
        /* If entry is comform COW, make it COW and Always copy it */
        if ((ppt[j] & pte_small_check) == pte_small_check)
            if (vma_lookup(pe, (void*) ((i*PGSIZE*1024) + (j*PGSIZE)), 0)->perm | VMA_PERM_WRITE)
                ppt[j] &= ~(uint32_t)PTE_BIT_RW;
            
        
        cpt[j] = ppt[j];
        
        /* Inc ref on user pages*/
//        if (ppt[j]) dprintf("(%p) %p\n", PTE_GET_PHYS_ADDRESS(ppt[j]), ppt[j]);
        if (ppt[j] & PTE_BIT_PRESENT) //if present
            if (ppt[j] & PTE_BIT_USER) //And user accessable
                if (PGNUM(PTE_GET_PHYS_ADDRESS(ppt[j])) < npages) //and refers to existing physical page
                    page_inc_ref(pa2page(PTE_GET_PHYS_ADDRESS(ppt[j]))); //Increase reference
    } //end for
    
    return 0;
}

int fork_allocate_pgtables(pde_t* cpdir, pde_t* ppdir){
    for(uint16_t i = 0; i<1024; i++) {
        /* Only for user accessable pages under utop */
        if ((uint32_t)PGADDR(i,0,0) >= UTOP)
            continue;
        
        /* Allocate page table */
        if (ppdir[i] & PDE_BIT_PRESENT && !(ppdir[i] & PDE_BIT_HUGE) && ppdir[i] & PDE_BIT_USER) {
            /* Allocate new page for childs pgtable */
            page_info_t * pp = page_alloc(0); // we copy every entry, so no zero alloc
            if (!pp) {
                dprintf("Allocation failed!\n");
                return -1;
            }
            page_inc_ref(pp);
            
            /* 
            * Insert new page (table) into child pgdir 
            * Keep parent permissions
            */
            cpdir[i] = page2pa(pp) | (ppdir[i] & 0x1F);
        }
    }
    return 0;
}

void fork_reverse_pgtable_alloc(pde_t* cpdir){
    dprintf("Reversing allocation of page tables.\n");
    for(uint32_t i = 0; i<1024; i++) {
        if (cpdir[i] & PDE_BIT_PRESENT && !(cpdir[i] & PDE_BIT_HUGE)  && cpdir[i] & PDE_BIT_USER) {
            page_info_t * pp = pa2page(PDE_GET_ADDRESS(cpdir[i]));
            if (pp->pp_ref == 1)
                page_decref(pp);
        }
    }
}

int fork_pgdir_copy_and_cow(env_t * penv ,env_t* cenv){
    /* Setup permission check register */
    uint32_t pde_huge_check = PDE_BIT_HUGE | PDE_BIT_PRESENT | PDE_BIT_USER;
    
    /* page dir pointers */
    pde_t * ppdir = penv->env_pgdir;
    pde_t * cpdir = cenv->env_pgdir;
    
    /* Duplicate */
    memcpy(cpdir, ppdir, PGSIZE);
    
    /* Allocate first, any error can be reverted! */
    if (fork_allocate_pgtables(cpdir,ppdir)) {
        dprintf("Out of Memory!\n");
        fork_reverse_pgtable_alloc(cpdir);
        return 0;
    }
    
    /* 
     * - Duplicate pgdir
     * \- edit pgdir entry to COW when pde_huge_check comfirms
     */
    for(uint32_t i = 0; i<1024; i++) {   
        /* Look only at present entries */
        if (!(ppdir[i] & PDE_BIT_PRESENT))
            continue;
        
        /* Pages under utop */
        if ((uint32_t)PGADDR(i,0,0) >= UTOP)
            continue;
        
        /* If page is huge and must be cow'ed: remove w bit*/
        if ((ppdir[i] & pde_huge_check) == pde_huge_check)
            if (vma_lookup(penv, (void*)(i*PGSIZE*1024), 0)->perm & VMA_PERM_WRITE)
                /* Remove write bit */
                cpdir[i] = (ppdir[i] &= ~(uint32_t)PDE_BIT_RW);

        /* Increase page reference of the huge page */
        if (ppdir[i] & PDE_BIT_HUGE)
            if (ppdir[i] & PDE_BIT_USER)
                page_inc_ref(pa2page(PDE_GET_ADDRESS(ppdir[i])));

        /* If it is not huge, copy pgtable */
        if (!(ppdir[i] & PDE_BIT_HUGE))
            if (fork_pgtable_cow(penv, ppdir, cpdir, i))
                return -1;
    }
    
    return 0;
}


static int sys_fork(void)
{
    /* fork() that follows COW semantics */
    /* LAB 5: Your code here */
    env_t *newenv;
    
    /* Allocate env  & duplicate shared info */
    if (env_alloc(&newenv, curenv->env_id)) {
        cprintf("Failed to fork new child process!\n");
        return -1;
    }
    
    //registers
    newenv->env_tf = curenv->env_tf;
    
    //Etc
//    newenv->env_status = ENV_RUNNABLE; Not yet!!!
    newenv->env_type = curenv->env_type;
    
    /* Copy pgdir, changing permissions to COW where applicable 
     * for both the parent and the child
     */
    if (fork_pgdir_copy_and_cow(curenv, newenv)) {
        dprintf("forking pgdir failed!\n");
        env_free(newenv);
        return -1;
    }
    dprintf("Forking pgdir success!\n");
    
    /* Child now inherits the COW thingies */
    memcpy(newenv->vma_list, curenv->vma_list, sizeof(vma_arr_t));
    
    /* make eax (return value) 0, such that it knows it is new */
    newenv->env_tf.tf_regs.reg_eax = 0;
    
    /* Change uvpt to reflect the child page table in the child pgdir */
    newenv->env_pgdir[PDX(UVPT)] = PADDR(newenv->env_pgdir) | PTE_P | PTE_U;
    
    /* Flush tlb */
    tlbflush();
    
    /* Dump child vma */
    vma_dump_all(newenv);
    
    /* Child is now runnable! */
    if (sync_bool_compare_and_swap(&newenv->env_status, ENV_NOT_RUNNABLE, ENV_RUNNABLE)==0)
        panic("Set runnable failed!");
    
    //Return the new env id so that the forker knows who he spawned
    return newenv->env_id;
}

/* Dispatches to the correct kernel function, passing the arguments. */
int32_t syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3,
        uint32_t a4, uint32_t a5)
{
    /*
     * Call the function corresponding to the 'syscallno' parameter.
     * Return any appropriate return value.
     * LAB 3: Your code here.
     */


    switch (syscallno) {
        case SYS_yield:
            sys_yield();
            return 0;
        case SYS_wait:
            return sys_wait(a1);
        case SYS_cputs:
            sys_cputs((char *)a1,a2);
            return 0;
        case SYS_cgetc:
            return sys_cgetc();
        case SYS_getenvid:
            return sys_getenvid();
        case SYS_env_destroy:
            return sys_env_destroy(a1);
        case SYS_vma_create:
            return (uint32_t) sys_vma_create(a1, a2, a3);
        case SYS_vma_destroy:
            return sys_vma_destroy((void *)a1, a2);
        case SYS_fork:
            return sys_fork();
        default:
            return -E_NO_SYS;
    }
}

