/* See COPYRIGHT for copyright information. */

#include "../inc/x86.h"
#include "../inc/env.h"
#include "../inc/elf.h"
#include "../inc/mmu.h"
#include "../inc/trap.h"
#include "../inc/types.h"
#include "../inc/error.h"
#include "../inc/string.h"
#include "../inc/assert.h"
#include "../inc/memlayout.h"

#include "env.h"
#include "vma.h"
#include "cpu.h"
#include "pmap.h"
#include "trap.h"
#include "sched.h"
#include "monitor.h"
#include "spinlock.h"
#include "inc/atomic_ops.h"

struct env *envs = NULL;            /* All environments */
static struct env *env_free_list;   /* Free environment list */
                                    /* (linked by env->env_link) */

#define ENVGENSHIFT 12      /* >= LOGNENV */

/*
 * Global descriptor table.
 *
 * Set up global descriptor table (GDT) with separate segments for
 * kernel mode and user mode.  Segments serve many purposes on the x86.
 * We don't use any of their memory-mapping capabilities, but we need
 * them to switch privilege levels.
 *
 * The kernel and user segments are identical except for the DPL.
 * To load the SS register, the CPL must equal the DPL.  Thus,
 * we must duplicate the segments for the user and the kernel.
 *
 * In particular, the last argument to the SEG macro used in the
 * definition of gdt specifies the Descriptor Privilege Level (DPL)
 * of that descriptor: 0 for kernel and 3 for user.
 */
struct segdesc gdt[NCPU + 5] =
{
    /* 0x0 - unused (always faults -- for trapping NULL far pointers) */
    SEG_NULL,

    /* 0x8 - kernel code segment */
    [GD_KT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 0),

    /* 0x10 - kernel data segment */
    [GD_KD >> 3] = SEG(STA_W, 0x0, 0xffffffff, 0),

    /* 0x18 - user code segment */
    [GD_UT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 3),

    /* 0x20 - user data segment */
    [GD_UD >> 3] = SEG(STA_W, 0x0, 0xffffffff, 3),

    /* 0x28 - Per-CPU TSS descriptors (starting from GD_TSS0) are initialized
     *        in trap_init_percpu() */
    [GD_TSS0 >> 3] = SEG_NULL
};

struct pseudodesc gdt_pd = {
    sizeof(gdt) - 1, (unsigned long) gdt
};

/*
 * Converts an envid to an env pointer.
 * If checkperm is set, the specified environment must be either the
 * current environment or an immediate child of the current environment.
 *
 * RETURNS
 *   0 on success, -E_BAD_ENV on error.
 *   On success, sets *env_store to the environment.
 *   On error, sets *env_store to NULL.
 */
int envid2env(envid_t envid, struct env **env_store, bool checkperm)
{
    struct env *e;

    lock_env();
    assert_lock_env();

    /* If envid is zero, return the current environment. */
    if (envid == 0) {
        *env_store = curenv;
        unlock_env();
        return 0;
    }

    /*
     * Look up the env structure via the index part of the envid,
     * then check the env_id field in that struct env
     * to ensure that the envid is not stale
     * (i.e., does not refer to a _previous_ environment
     * that used the same slot in the envs[] array).
     */
    e = &envs[ENVX(envid)];
    if (e->env_status == ENV_FREE || e->env_id != envid) {
        *env_store = 0;
        unlock_env();
        return -E_BAD_ENV;
    }

    /*
     * Check that the calling environment has legitimate permission
     * to manipulate the specified environment.
     * If checkperm is set, the specified environment
     * must be either the current environment
     * or an immediate child of the current environment.
     */
    if (checkperm && e != curenv && e->env_parent_id != curenv->env_id) {
        *env_store = 0;
        unlock_env();
        return -E_BAD_ENV;
    }

    *env_store = e;
    unlock_env();
    return 0;
}

/*
 * Mark all environments in 'envs' as free, set their env_ids to 0,
 * and insert them into the env_free_list.
 * Make sure the environments are in the free list in the same order
 * they are in the envs array (i.e., so that the first call to
 * env_alloc() returns envs[0]).
 */
void env_init(void)
{
    /* Set up envs array. */
    /* LAB 3: Your code here. */
    ssize_t i;
    struct env* uenv;
    for(i = NENV - 1; i >= 0; i--) {
        uenv = &envs[i];
        memset(uenv, 0, sizeof(struct env));

        /* ENV_FREE is 0, and id is 0 already as well, but for clarity:  */
        uenv->env_status |= ENV_FREE;
        uenv->env_id = 0;

        if(i < (NENV - 1)) {
            uenv->env_link = &envs[i+1];
        }
        env_free_list = uenv;
    }

    /* Per-CPU part of the initialization */
    env_init_percpu();
}

/* Load GDT and segment descriptors. */
void env_init_percpu(void)
{
    lgdt(&gdt_pd);
    /* The kernel never uses GS or FS, so we leave those set to the user data
     * segment. */
    asm volatile("movw %%ax,%%gs" :: "a" (GD_UD|3));
    asm volatile("movw %%ax,%%fs" :: "a" (GD_UD|3));
    /* The kernel does use ES, DS, and SS.  We'll change between the kernel and
     * user data segments as needed. */
    asm volatile("movw %%ax,%%es" :: "a" (GD_KD));
    asm volatile("movw %%ax,%%ds" :: "a" (GD_KD));
    asm volatile("movw %%ax,%%ss" :: "a" (GD_KD));
    /* Load the kernel text segment into CS. */
    asm volatile("ljmp %0,$1f\n 1:\n" :: "i" (GD_KT));
    /* For good measure, clear the local descriptor table (LDT), since we don't
     * use it. */
    lldt(0);
}

/*
 * Initialize the kernel virtual memory layout for environment e.
 * Allocate a page directory, set e->env_pgdir accordingly,
 * and initialize the kernel portion of the new environment's address space.
 * Do NOT (yet) map anything into the user portion
 * of the environment's virtual address space.
 *
 * Returns 0 on success, < 0 on error.  Errors include:
 *  -E_NO_MEM if page directory or table could not be allocated.
 */
static int env_setup_vm(struct env *e)
{
    int i;
    struct page_info *p = NULL;

    /* Allocate a page for the page directory */
    if (!(p = page_alloc(ALLOC_ZERO)))
        return -E_NO_MEM;

    /*
     * Now, set e->env_pgdir and initialize the page directory.
     *
     * Hint:
     *    - The VA space of all envs is identical above UTOP
     *  (except at UVPT, which we've set below).
     *  See inc/memlayout.h for permissions and layout.
     *  Can you use kern_pgdir as a template?  Hint: Yes.
     *  (Make sure you got the permissions right in Lab 2.)
     *    - The initial VA below UTOP is empty.
     *    - You do not need to make any more calls to page_alloc.
     *    - Note: In general, pp_ref is not maintained for physical pages mapped
     *      only above UTOP, but env_pgdir is an exception -- you need to
     *      increment env_pgdir's pp_ref for env_free to work correctly.
     *    - The functions in kern/pmap.h are handy.
     */

    /* LAB 3: Your code here. */
    page_inc_ref(p);
    e->env_pgdir = page2kva(p);
    memcpy(e->env_pgdir, kern_pgdir, PGSIZE);

    /* UVPT maps the env's own page table read-only.
     * Permissions: kernel R, user R */
    e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_P | PTE_U;
    return 0;
}

/*
 * Allocates and initializes a new environment.
 * On success, the new environment is stored in *newenv_store.
 *
 * Returns 0 on success, < 0 on failure.  Errors include:
 *  -E_NO_FREE_ENV if all NENVS environments are allocated
 *  -E_NO_MEM on memory exhaustion
 */
int env_alloc(struct env **newenv_store, envid_t parent_id, enum env_type envtype)
{
    int32_t generation;
    int r;
    struct env *e;

    lock_env();

    if (!(e = env_free_list)) {
        unlock_env();
        return -E_NO_FREE_ENV;
    }

    /* Allocate and set up the page directory for this environment. */
    if ((r = env_setup_vm(e)) < 0) {
        unlock_env();
        return r;
    }

    /* Create VMA list for this environment */
    if (vma_array_init(e)) {
        dprintf("env_alloc failed: No free pages for vma_array!\n");
        //Undo env_setup
        page_decref(pa2page(PADDR(e->env_pgdir)));
        unlock_env();
        return -E_NO_MEM;
    }

    /* Generate an env_id for this environment. */
    generation = (e->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
    if (generation <= 0)    /* Don't create a negative env_id. */
        generation = 1 << ENVGENSHIFT;
    e->env_id = generation | (e - envs);

    /* Set the basic status variables. */
    e->env_parent_id = parent_id;
    e->env_type = envtype;
    e->env_status = ENV_NOT_RUNNABLE; //Not initialized so not runnable!
    e->env_runs = 0;
    e->remain_cpu_time = MAX_TIME_SLICE;

    /*
     * Clear out all the saved register state, to prevent the register values of
     * a prior environment inhabiting this env structure from "leaking" into our
     * new environment.
     */
    memset(&e->env_tf, 0, sizeof(e->env_tf));

    /*
     * Set up appropriate initial values for the segment registers.
     * GD_UD is the user data segment selector in the GDT, and
     * GD_UT is the user text segment selector (see inc/memlayout.h).
     * The low 2 bits of each segment register contains the
     * Requestor Privilege Level (RPL); 3 means user mode.  When
     * we switch privilege levels, the hardware does various
     * checks involving the RPL and the Descriptor Privilege Level
     * (DPL) stored in the descriptors themselves.
     */
    switch (envtype) {
        case ENV_TYPE_USER:
            e->env_tf.tf_ds = GD_UD | 3;
            e->env_tf.tf_cs = GD_UT | 3;
            /* Enable interrupts while in user mode. */
            e->env_tf.tf_eflags |= FL_IF;
            break;
        case ENV_TYPE_KERNEL_ENV:
        case ENV_TYPE_KERNEL_THREAD:
            /* Set kernel rights*/
            e->env_tf.tf_ds = GD_KD | 0;
            e->env_tf.tf_cs = GD_KT | 0;

            /* Alloc 1 page for stack, so we can use it in env_pop_tf on initial run */
            page_info_t *pp = page_alloc(ALLOC_ZERO);
            if (!pp) {
                panic("Page alloc for kernel env failed!");
            }
            page_insert(e->env_pgdir, pp, (void*) USTACKTOP-PGSIZE, PTE_BIT_RW);

            break;
        default:
            panic("Invalid env type %d", envtype);
    }

    /* Set remaining tf variables */
    e->env_tf.tf_es = e->env_tf.tf_ds; // Same as tf_ds
    e->env_tf.tf_ss = e->env_tf.tf_ds; // Same as tf_ds
    e->env_tf.tf_esp = USTACKTOP;


    /* commit the allocation */
    env_free_list = e->env_link;
    *newenv_store = e;

    char *type = envtype ? "kernel" : "user";
    cprintf("[%08x] new env %08x of type %s\n", curenv ? curenv->env_id : 0, e->env_id, type);


    unlock_env();
    return 0;
}

/*
 * Allocate len bytes of physical memory for environment env, and map it at
 * virtual address va in the environment's address space.
 * Does not zero or otherwise initialize the mapped pages in any way.
 * Pages should be writable by user and kernel.
 * Panic if any allocation attempt fails.
 */
static void region_alloc(struct env *e, void *va, size_t len)
{
    /*
     * LAB 3: Your code here.
     * (But only if you need it for load_icode.)
     *
     * Hint: It is easier to use region_alloc if the caller can pass
     *   'va' and 'len' values that are not page-aligned.
     *   You should round va down, and round (va + len) up.
     *   (Watch out for corner-cases!)
     */
    //Set print header
    dprintf("region_alloc: %#08x to %#08x\n", va, va+len);

    //Assertions
    assert(len>0);
    assert(e);

    /* Bootstrap */
    //rounded down virtual address
    uint32_t rva = ((uint32_t) va ) & ~0xFFF; //bit mask lower bits to round down
    //ROunded up length
    size_t rlen = ROUNDUP(len, PGSIZE);
    //number of physical pages to allocate
    uint32_t numpages = rlen/PGSIZE;

    /* Allocate */
    //Allocated start page
    dprintf("\tAllocating %d pages... \n", numpages);
    struct page_info * pp = alloc_consecutive_pages(numpages,  0);
    dprintf("Success!\n");

    //TODO: try allocating separate pages if consecutive does not succeed
    //assert allocation successful
    assert(pp);

    /* Page table determination */
    //Determine amount of page table entries required
    uint32_t pages4M, pages4K;
    //Determine 4K pages in total
    pages4K = numpages;
    //Determine number of 4M pages in total
    pages4M = pages4K / 1024;

    // --------------- Remove 4M support --------------- //
    pages4M = 0;

    /* setup va tables */
    dprintf("\tSetting up %u 4M and %u 4K pages at %#08x to %#08x... \n",
    pages4M, pages4K-pages4M*1024, rva, rva+rlen);

    uint32_t i = 0;
    uint32_t res = 0;
    for(; i<pages4M; i++)
        res |= page_insert(
                e->env_pgdir, //the env pgdir
                pp+(i*1024), //origin address of pp + offset 4M pages
                (void*)(rva + (i*1024*PGSIZE)),
                PDE_BIT_HUGE | PDE_BIT_RW | PDE_BIT_USER | PDE_BIT_PRESENT
                );
    //Convert i * 4M pages to i * 4K pages
    i *= 1024;

    //i == number of 4k pages mapped via 4M, iterate till all leftovers are done
    for(; i<pages4K; i++)
        res |= page_insert(
                    e->env_pgdir, //the env pgdir
                    pp+i, //origin address of pp + offset 4M pages
                    (void*)(rva + (i*PGSIZE)),
                    PDE_BIT_RW | PDE_BIT_USER | PDE_BIT_PRESENT
                    );

    //Check if there where any errors
    assert(res==0);
    dprintf("Success!\n");

    /* Check virtual pages */
    dprintf("\tChecking access (R/W) to va range... \n");

    //Create 32 bit sized bites (volatile hack to prevent optimizations)
    volatile uint32_t * data;
    //Print Check variables

    //Check RW access to virtual memory
    for(data=(uint32_t*)va; data<(uint32_t*)(va+len); data++) {
        volatile uint32_t tmp = *data; //volatile prevents optimization
        *data = tmp;
    }
    //If no pagefault happend, check succes!
    dprintf("Successful!\n");

    /* Invalidate TLB */
    //Legacy code if lcr3 changes, keep this line!
    tlb_invalidate(e->env_pgdir, va);
}

/*
 * Set up the initial program binary, stack, and processor flags for a user
 * process.
 * This function is ONLY called during kernel initialization, before running the
 * first user-mode environment.
 *
 * This function loads all loadable segments from the ELF binary image into the
 * environment's user memory, starting at the appropriate virtual addresses
 * indicated in the ELF program header.
 * At the same time it clears to zero any portions of these segments that are
 * marked in the program header as being mapped but not actually present in the
 * ELF file - i.e., the program's bss section.
 *
 * All this is very similar to what our boot loader does, except the boot loader
 * also needs to read the code from disk. Take a look at boot/main.c to get
 * ideas.
 *
 * Finally, this function maps one page for the program's initial stack.
 *
 * load_icode panics if it encounters problems.
 *  - How might load_icode fail?  What might be wrong with the given input?
 */
static void load_icode(struct env *e, uint8_t *binary)
{
    /*
     * Hints:
     *  Load each program segment into virtual memory at the address specified
     *  in the ELF section header.
     *  You should only load segments with ph->p_type == ELF_PROG_LOAD.
     *  Each segment's virtual address can be found in ph->p_va and its size in
     *  memory can be found in ph->p_memsz.
     *  The ph->p_filesz bytes from the ELF binary, starting at 'binary +
     *  ph->p_offset', should be copied to virtual address ph->p_va.
     *  Any remaining memory bytes should be cleared to zero.
     *  (The ELF header should have ph->p_filesz <= ph->p_memsz.)
     *  Use functions from the previous lab to allocate and map pages.
     *
     *  All page protection bits should be user read/write for now.
     *  ELF segments are not necessarily page-aligned, but you can assume for
     *  this function that no two segments will touch the same virtual page.
     *
     *  You may find a function like region_alloc useful.
     *
     *  Loading the segments is much simpler if you can move data directly into
     *  the virtual addresses stored in the ELF binary.
     *  So which page directory should be in force during this function?
     *
     *  You must also do something with the program's entry point, to make sure
     *  that the environment starts executing there.
     *  What?  (See env_run() and env_pop_tf() below.)
     */

    /* LAB 3: Your code here. */

    /* Now map one page for the program's initial stack at virtual address
     * USTACKTOP - PGSIZE. */
//    region_alloc(e, (void*)(USTACKTOP - PGSIZE), PGSIZE);

    /* LAB 3: Your code here. */
    struct elf *elf_header = (struct elf *)binary;
    assert(elf_header->e_magic == ELF_MAGIC);

    /* Save curenv, so we can temporarily use this env's VMA in trap.c,
     * even though env_run() hasn't been called yet. */
    env_t *prev_curenv = curenv;
    curenv = e;

    /* Get end of code space variable*/
    uint32_t eoc_mem = 0;

    struct elf_proghdr *ph = (struct elf_proghdr *) ((uint8_t *) elf_header + elf_header->e_phoff);
    struct elf_proghdr *eph = ph + elf_header->e_phnum;
    for (; ph < eph; ph++)
        if(ph->p_type == ELF_PROG_LOAD) {
            assert(ph->p_memsz >= ph->p_filesz);
            assert(ph->p_va + ph->p_memsz <= UTOP);

            /* VMA mapping */
            int perm = 0;
//            perm |= ph->p_flags & ELF_PROG_FLAG_EXEC  ? VMA_PERM_EXEC  : 0;
//            perm |= ph->p_flags & ELF_PROG_FLAG_WRITE ? VMA_PERM_WRITE : 0;
//            perm |= ph->p_flags & ELF_PROG_FLAG_READ  ? VMA_PERM_READ  : 0;
            perm = VMA_PERM_WRITE | VMA_PERM_READ | VMA_PERM_EXEC;
            int vma_index = vma_new(e, (void*)ph->p_va, ph->p_memsz, perm, VMA_BINARY); //elf binary

            /* Set vma backing */
//            vma_set_backing(e, vma_index, binary + ph->p_offset, ph->p_filesz);


            /* set end of code space variable*/
            if (ph->p_va+ph->p_memsz > eoc_mem)
                eoc_mem = ph->p_va+ph->p_memsz;

            /* Allocate region (prevents fault OD allocations) */
            /* We may not allocate code region like this, it implies write permissions */
            region_alloc(e, (void *)ph->p_va, ph->p_memsz);

            /* We can use virtual addresses because the uenv's pgdir has been loaded */
            memcpy((void *)ph->p_va, binary + ph->p_offset, ph->p_filesz);

            /* Zero out remaining bytes */
            memset((void *)ph->p_va + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
        }

    /* Add ELF entry to environment's instruction pointer */
    e->env_tf.tf_eip = elf_header->e_entry;

    /* vmatest binary uses the following */
    /* 1. Map one RO page of VMA for UTEMP at virtual address UTEMP.
     * 2. Map one RW page of VMA for UTEMP+PGSIZE at virtual address UTEMP+PGSIZE. */

    vma_new(e, UTEMP, PGSIZE, VMA_PERM_READ, VMA_ANON);
    vma_new(e, UTEMP+PGSIZE, PGSIZE, VMA_PERM_READ | VMA_PERM_WRITE, VMA_ANON);

    /* General (anon) mappings */
    vma_new(e, (void*)(USTACKTOP-PGSIZE), PGSIZE, VMA_PERM_READ | VMA_PERM_WRITE, VMA_ANON); //stack
    /* Map end of code to stack as heap. Stack and heap get merged */
//    vma_new(e, (void*)(eoc_mem + PGSIZE), 4<<20, VMA_PERM_READ | VMA_PERM_WRITE, VMA_ANON); //heap

//    vma_dump_all(e);

    /* Restore curenv */
    curenv = prev_curenv;
}

/*
 * Allocates a new env with env_alloc, loads the named elf binary into it with
 * load_icode, and sets its env_type.
 * This function is ONLY called during kernel initialization, before running the
 * first user-mode environment.
 * The new env's parent ID is set to 0.
 */
void env_create(uint8_t *binary, enum env_type type)
{
    /* Allocate environment */
    struct env * e = 0;
    assert(env_alloc(&e, 0, type)==0);

    /* Setup env */
    e->env_type = type;

    /* Switch to user environment page directory */
    lcr3(PADDR(e->env_pgdir));

    /* Load code */
    load_icode(e, binary); //also setups env registers (such SP and IP)

    /* Now its runnable, mark it as such */
    if (sync_bool_compare_and_swap(&e->env_status, ENV_NOT_RUNNABLE, ENV_RUNNABLE) == 0)
        panic("Set runnable failed!");

    dprintf("Created env #%d at elf address %p\n", e - envs, binary);
}

/*
 * Frees env e and all memory it uses.
 */
void env_free(struct env *envp)
{
    /* Static so that we can enter env_free from kernel threads
     * from their own stack, without faulting because their stacks
     * are being freed as this method goes on. */
    static struct env *e;
    static pte_t *pt;
    static uint32_t pdeno, pteno;
    static physaddr_t pa;

    e = envp;

    /* Note the environment's demise. */
    cprintf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

    /* Clean vmas */
    vma_array_destroy(e);

    /* Flush all mapped pages in the user portion of the address space */
    static_assert(UTOP % PTSIZE == 0);
    for (pdeno = 0; pdeno < 1024; pdeno++) {

        /* Only look at mapped page tables */
        if (!(e->env_pgdir[pdeno] & PTE_P))
            continue;

        /* Only at user accessable tables */
        if (!(e->env_pgdir[pdeno] & PTE_BIT_USER))
            continue;

        /* Dont look above utop */
        if ((uint32_t)PGADDR(pdeno, 0, 0) >= UTOP)
            continue;

        /* Unmap huge */
        if (e->env_pgdir[pdeno] & PDE_BIT_HUGE) {
            page_info_t *huge = pa2page(PDE_GET_ADDRESS(e->env_pgdir[pdeno]));
            page_decref(huge);
            continue;
        }

        /* Unmap all PTEs in this page table */
        page_info_t *page_of_table = pa2page(PDE_GET_ADDRESS(e->env_pgdir[pdeno]));
        pte_t * table = page2kva(page_of_table);

        for(uint32_t i = 0; i<1024; i++) {
            pte_t pte = table[i];

            /* Only decref present user pages */
            if (pte & PTE_BIT_PRESENT) {
                if (pte & PTE_BIT_USER) {
                    page_info_t *usr_page = pa2page(PTE_GET_PHYS_ADDRESS(pte));
                    page_decref(usr_page);
                }
            }

        }

        /* Unmap the table itself */
        page_decref(page_of_table);

    }

    /* If freeing the current environment, switch to kern_pgdir
     * before freeing the page directory, just in case the page
     * gets reused. */
    if (e == curenv)
        lcr3(PADDR(kern_pgdir));

    /* Free the page directory */
    pa = PADDR(e->env_pgdir);
    e->env_pgdir = 0;
    page_decref(pa2page(pa));

    /* return the environment to the free list */
    e->env_status = ENV_FREE;
    e->env_link = env_free_list;
    env_free_list = e;

    /* Zero out env */
    memset(e, 0, sizeof(env_t));
}

/*
 * Frees environment e.
 * If e was the current env, then runs a new environment (and does not return
 * to the caller).
 */
void env_destroy(struct env *e)
{
    size_t i;
    lock_env();
    assert_lock_env();

    /* Mark all envs waiting for this env as runnable again */
    for(i = 0; i < NENV; i++) {
        env_t *env = &envs[i];
        if(env && env->env_status == ENV_WAITING && env->waiting_for == e->env_id) {
            env->env_status = ENV_RUNNABLE;
            env->waiting_for = 0;
        }
    }

    /* If e is currently running on other CPUs, we change its state to
     * ENV_DYING. A zombie environment will be freed the next time
     * it traps to the kernel. */
    if (e->env_status == ENV_RUNNING && curenv != e) {
        e->env_status = ENV_DYING;
        return;
    }

    env_free(e);

    if (curenv == e) {
        curenv = NULL;
    }

    unlock_env();
}

/*
 * Restores the register values in the trapframe with the 'iret' instruction.
 * This exits the kernel and starts executing some environment's code.
 *
 * This function does not return.
 */
void env_pop_tf(struct trapframe *tf)
{
    /* Record the CPU we are running on for user-space debugging */
    curenv->env_cpunum = cpunum();

    if (curenv->env_type == ENV_TYPE_KERNEL_ENV) {
        /* Our env is a kernel env */
        __asm __volatile(
            "mov %0, %%esp\n" /* tf */
            "subl $0xc, 0x3c(%%esp)\n" /* Reserve 12 more bytes on tf_esp */
            "mov 0x30(%%esp), %%ecx\n" /* tf_eip */
            "mov 0x3c(%%esp), %%edx\n" /* tf_esp */

            "mov %%ecx, (%%edx)\n" /* Push tf_eip on tf_esp */
            "mov 0x34(%%esp), %%ecx\n" /* tf_cs + padding */
            "mov %%ecx, 0x04(%%edx)\n" /* Push tf_cs on tf_esp */
            "mov 0x38(%%esp), %%ecx\n" /* tf_eflags */
            "mov %%ecx, 0x08(%%edx)\n" /* Push tf_cs on tf_esp */

            "popal\n" /* Reset registers to tf_regs */
            "mov 0x1c(%%esp), %%esp\n" /* tf_esp -> esp */
            "iret\n"
        :: "g" (tf) : "memory");
    } else if (curenv->env_type == ENV_TYPE_KERNEL_THREAD) {
        /* Our env is a kernel thread */
        __asm __volatile(
        "mov %0, %%esp\n"
            "popal\n"
            "\tpopl %%es\n"
            "\tpopl %%ds\n"
            "\taddl $0x8,%%esp\n" /* skip tf_trapno and tf_errcode */
            "ret\n"
        :: "g" (tf) : "memory");
    } else {
        __asm __volatile("movl %0,%%esp\n"
            "\tpopal\n"
            "\tpopl %%es\n"
            "\tpopl %%ds\n"
            "\taddl $0x8,%%esp\n" /* skip tf_trapno and tf_errcode */
            "\tiret"
        : : "g" (tf) : "memory");
        panic("iret failed");  /* mostly to placate the compiler */
    }

    panic("env_pop_tf() should not return");
}

/*
 * Context switch from curenv to env e.
 * Note: if this is the first call to env_run, curenv is NULL.
 *
 * This function does not return.
 */
void env_run(struct env *e)
{
    /*
     * Step 1: If this is a context switch (a new environment is running):
     *     1. Set the current environment (if any) back to
     *        ENV_RUNNABLE if it is ENV_RUNNING (think about
     *        what other states it can be in),
     *     2. Set 'curenv' to the new environment,
     *     3. Set its status to ENV_RUNNING,
     *     4. Update its 'env_runs' counter,
     *     5. Use lcr3() to switch to its address space.
     * Step 2: Use env_pop_tf() to restore the environment's
     *     registers and drop into user mode in the
     *     environment.
     *
     * Hint: This function loads the new environment's state from
     *  e->env_tf.  Go back through the code you wrote above
     *  and make sure you have set the relevant parts of
     *  e->env_tf to sensible values.
     */


    /* switch environment */
    if (curenv != e) {

        //set a running env back to runnable
//        if (curenv && curenv->env_status == ENV_RUNNING)
//            curenv->env_status = ENV_RUNNABLE;

        //switch curenv variable
        curenv = e;

        //update new curenv status
//        assert(curenv->env_status == ENV_RUNNABLE);
//        curenv->env_status = ENV_RUNNING;

        //inc runs
        curenv->env_runs++;

        //set memory environment
        lcr3(PADDR(curenv->env_pgdir)); //convert KVA to PA
    }

    //Check if everything is OK
    assert(curenv == e);
    assert(curenv->env_status == ENV_RUNNING);

    /* restore env registers */
    env_pop_tf(&e->env_tf);
}

