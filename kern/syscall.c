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
int fork_pgtable_cow(pde_t* ppdir, pde_t* cpdir, uint16_t i){
    //Define COW'able pte enrty
    uint32_t pte_small_check = PTE_BIT_RW | PTE_BIT_PRESENT | PTE_BIT_USER;
    
    if (!(ppdir[i] & PDE_BIT_PRESENT))
        return 0;

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
    
    /* Set table pointers */
    pte_t * ppt = KADDR(PDE_GET_ADDRESS(ppdir[i]));
    pte_t * cpt = page2kva(pp);
  
    for(uint32_t j = 0; j< 1024; j++) {
        /* If entry is comform COW, make it COW and Always copy it */
        if ((ppt[j] & pte_small_check) == pte_small_check) {
            ppt[j] ^= PTE_BIT_RW;
            dprintf("VA %p now COW.\n", i*PGSIZE*1024 + j*PGSIZE);
        }
        
        cpt[j] = ppt[j];
        
        /* Inc ref on user pages*/
        if (ppt[j] & PTE_BIT_PRESENT) //if present
            if (PGNUM(PTE_GET_PHYS_ADDRESS(ppt[j])) < npages) //and refers to existing physical page
                if (pa2page(PTE_GET_PHYS_ADDRESS(ppt[j]))->pp_ref) //And parent has referenced it
                    page_inc_ref(pa2page(PTE_GET_PHYS_ADDRESS(ppt[j]))); //Increase reference
    }
    
    return 0;
}

int fork_pgdir_copy_and_cow(env_t * penv ,env_t* cenv){
    /* Setup permission check register */
    uint32_t pde_huge_check = PDE_BIT_HUGE | PDE_BIT_RW | PDE_BIT_PRESENT | PDE_BIT_USER;
    
    /* 
     * - Duplicate pgdir
     * \- edit pgdir entry to COW when pde_huge_check comfirms
     */
    pde_t * ppdir = penv->env_pgdir;
    pde_t * cpdir = cenv->env_pgdir;
    for(uint16_t i = 0; i<1024; i++) {
        if (ppdir[i] & PDE_BIT_PRESENT) {
            if ((ppdir[i] & pde_huge_check) == pde_huge_check)
                /* Remove write bit */
                ppdir[i] ^= PDE_BIT_RW;
            
            //Copy entry from parent to child
            cpdir[i] = ppdir[i];
            
            /* Increase page reference of huge pages */
            if (ppdir[i] & PDE_BIT_HUGE)
                if (pa2page(PDE_GET_ADDRESS(ppdir[i]))->pp_ref)
                    page_inc_ref(pa2page(PDE_GET_ADDRESS(ppdir[i])));
            
            /* If it is not huge, copy pgtable */
            if (!(ppdir[i] & PDE_BIT_HUGE))
                if (i!=PDX(UVPT))
                    if (fork_pgtable_cow(ppdir, cpdir, i))
                        return -1;
        }
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
    newenv->env_status = ENV_RUNNABLE;
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

