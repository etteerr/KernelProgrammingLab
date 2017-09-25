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
        default:
            return -E_NO_SYS;
    }
}

