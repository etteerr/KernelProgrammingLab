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
    return vma_unmap(curenv, va, size);
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
    if(envid > NENV) {
        return -1;
    }

    env_t *env = &envs[envid];
    env_t *cur = (env_t *)curenv; /* IDE's macro unfolding is broken */

    if(!env || !cur) {
        return -1;
    }

    cur->env_status = ENV_WAITING;
    cur->waiting_for = envid;

    return 0;
}

void fork_vma_makecow(env_t* newenv, uint32_t va_range_start, uint32_t va_range_end){
    uint32_t len = va_range_end - va_range_start;
    vma_t *vma = vma_lookup(newenv, (void*)va_range_start, len);
    
    if (!vma) {
        vma_dump_all(newenv);
        cprintf("---------Error in vma range %#08x-%#08x---------\n", va_range_start, va_range_end);
        panic("No existing vma in range!");
    }
    
    if (vma->len == len){
        vma->flags.bit.COW = 1;
    }else {
        /* remap required */
        /* Copy original vma for future reference*/
        vma_t pvals = *vma;
        vma_unmap(newenv, (void*) va_range_start, len);
        int vma_index = vma_new(newenv, (void*) va_range_start, len, pvals.perm, pvals.type);
        vma = &newenv->vma_list->vmas[vma_index];
        vma->flags.bit.COW = 1;
        /* Check if everything permissions in vma where correct as well */
        assert(pvals.perm==VMA_PERM_WRITE);
        assert(vma->perm==VMA_PERM_WRITE);
        assert(vma->perm == pvals.perm);
    }
}

void fork_pgdir_copy_and_cow(env_t* newenv){
    /* Setup permission check register */
    uint32_t pde_huge_check = PDE_BIT_HUGE | PDE_BIT_RW | PDE_BIT_PRESENT | PDE_BIT_USER;
    uint32_t pte_small_check = PDE_BIT_RW | PDE_BIT_PRESENT | PDE_BIT_USER;
    
    /* vma range tracker: Keeps track of continues COWified pages */
    uint32_t range_start = 0;
    
    /* For all user RW pages, make COW entry (read only) */
    for(uint32_t di = 0; di<KERNBASE/(PGSIZE*1024); di++) {
        register pde_t pde = newenv->env_pgdir[di];
        
        /* Huge PAGE*/
        if (pde & PDE_BIT_HUGE) {
            if ((pde & pde_huge_check) == pde_huge_check) {
                /* Huge page with user rw permissions, COW canidate */
                pde ^= PDE_BIT_RW; //Set rw permissions to RO
                newenv->env_pgdir[di] = pde; //save changes
                
                //Next index (Otherwise we would edit vma already)
                continue;
            }
            /* COWify VMA range */
            if (range_start) {
                uint32_t range_end = di * (PGSIZE*1024);
                fork_vma_makecow(newenv, range_start, range_end);
                /* Reset range */
                range_start = 0;
            }
            
        }
        /* PAGE TABLE */
        if ((pde & pte_small_check) == pte_small_check){
            /* Page table exits and is user rw, may contain COW canidates */
            /* Get page table entry */
            pte_t * pgtable = KADDR(PDE_GET_ADDRESS(pde));
            for(uint32_t ti = 0; ti < 1024; ti++) {
                //page table entry register
                register pte_t pte = pgtable[ti];
                /* Check if pte is COW canidate */
                if ((pte & pte_small_check) == pte_small_check) {
                    if (~range_start)
                        range_start = di * (PGSIZE*1024) + ti * PGSIZE;
                    
                    pte ^= PTE_BIT_RW; //Make readonly pte entry
                    pgtable[ti] = pte;
                    
                    continue;
                }
                
                /* COWify VMA range */
                if (range_start) {
                    uint32_t range_end = di * (PGSIZE*1024) + ti * PGSIZE; //Not inclusive
                    fork_vma_makecow(newenv, range_start, range_end);
                    /* Reset range */
                    range_start = 0;
                }
            }
        }
    }
}

/**
 * Make a deep copy of cpgdir to npgdir
 *  Leaves all permissions intact.
 * @param curpg the current pgdir
 * @param newpg the new pgdir
 */
void pgdir_deepcopy(pde_t* newpg, pde_t* curpg){
    uint32_t page_huge = PDE_BIT_HUGE | PDE_BIT_PRESENT;
    uint32_t page_table = PDE_BIT_PRESENT;
    for(uint32_t pgi = 0; pgi < 1024; pgi++) {
        pde_t pde = curpg[pgi];
        
        if (page_huge == (page_huge & pde)) {
            /* Huge page, copy entry */
            newpg[pgi] = pde;
            continue;
        }
        
        if (page_table == (pde & page_table)) {
            /* Page table, allocate page and copy */
            page_info_t *pp = page_alloc(0);
            void *dst = page2kva(pp);
            void *src = page2kva(pa2page(PDE_GET_ADDRESS(pde)));
            memcpy(dst, src, PGSIZE);
            /* Set new entry in pagetable */
            pde &= 0xFFF; //keep old permissions
            pde |= page2pa(pp);
            newpg[pgi] = pde;
        }
    }
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
    
    //VMA
    memcpy(&newenv->vma_list, &curenv->vma_list, sizeof(vma_arr_t));
    //registers
    newenv->env_tf= curenv->env_tf;
    
    /* Make a deep copy of the page dir */
    pgdir_deepcopy(newenv->env_pgdir,curenv->env_pgdir);
    
    //Etc
    newenv->env_status = curenv->env_status;
    newenv->env_type = curenv->env_type;
    
    /* Copy pgdir, changing permissions to COW where applicable */
    fork_pgdir_copy_and_cow(newenv);
    
    /* make eax (return value) 0, such that it knows it is new */
    newenv->env_tf.tf_regs.reg_eax = 0;
    
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

