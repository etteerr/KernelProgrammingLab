/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/pmap.h>
#include <kern/kclock.h>
#include "../inc/memlayout.h"
#include "pmap.h"
#include "buddydef.h"

/* These variables are set by i386_detect_memory() */
size_t npages;                  /* Amount of physical memory (in pages) */
static size_t npages_basemem;   /* Amount of base memory (in pages) */

/* These variables are set in mem_init() */
struct page_info *pages;                 /* Physical page state array */
static struct page_info *page_free_list; /* Free list of physical pages */


/***************************************************************
 * Detect machine's physical memory setup.
 ***************************************************************/

static int nvram_read(int r)
{
    return mc146818_read(r) | (mc146818_read(r + 1) << 8);
}

static void i386_detect_memory(void)
{
    size_t npages_extmem;

    /* Use CMOS calls to measure available base & extended memory.
     * (CMOS calls return results in kilobytes.) */
    npages_basemem = (nvram_read(NVRAM_BASELO) * 1024) / PGSIZE;
    npages_extmem = (nvram_read(NVRAM_EXTLO) * 1024) / PGSIZE;

    /* Calculate the number of physical pages available in both base and
     * extended memory. */
    if (npages_extmem)
        npages = (EXTPHYSMEM / PGSIZE) + npages_extmem;
    else
        npages = npages_basemem;

    cprintf("Physical memory: %uK available, base = %uK, extended = %uK\n",
        npages * PGSIZE / 1024,
        npages_basemem * PGSIZE / 1024,
        npages_extmem * PGSIZE / 1024);
}


/***************************************************************
 * Set up memory mappings above UTOP.
 ***************************************************************/

static void check_page_free_list(bool only_low_memory);
static void check_page_alloc(void);

/* This simple physical memory allocator is used only while JOS is setting up
 * its virtual memory system.  page_alloc() is the real allocator.
 *
 * If n>0, allocates enough pages of contiguous physical memory to hold 'n'
 * bytes.  Doesn't initialize the memory.  Returns a kernel virtual address.
 *
 * If n==0, returns the address of the next free page without allocating
 * anything.
 *
 * If we're out of memory, boot_alloc should panic.
 * This function may ONLY be used during initialization, before the
 * page_free_list list has been set up. */
static void *boot_alloc(uint32_t n)
{
    static char *nextfree = 0;  /* virtual address of next byte of free memory */
    char *result;

    /* Initialize nextfree if this is the first time. 'end' is a magic symbol
     * automatically generated by the linker, which points to the end of the
     * kernel's bss segment: the first virtual address that the linker did *not*
     * assign to any kernel code or global variables. */
    if (!nextfree) {
        extern char end[];
        nextfree = ROUNDUP((char *) end, PGSIZE);
    }

    /* Allocate a chunk large enough to hold 'n' bytes, then update nextfree.
     * Make sure nextfree is kept aligned to a multiple of PGSIZE.
     *
     * LAB 1: Your code here.
     */
    //return pointer to freemem if n=0
    if (n==0)
        return (void*)nextfree;
    
    //Check if enough free memory exists
    // If we reached true OOM state, PANIC!
    uint32_t usage_cp, max;
    usage_cp = ((uint32_t)nextfree-KERNBASE) + n;
    max = npages * PGSIZE;
    cprintf("Kernel boot alloc:\n\tnew alloc: %uK\n\tcurrent Usage %uK\n\tmax Usage: %uK\n",n / 1024, usage_cp / 1024, max / 1024);
    if (usage_cp >= max)
        panic("Out of Memory PANIC: boot allocation failed.");
    
    //nextfree points to free memory, keep this value for return
    void* newAlloc = (void*) nextfree;
    nextfree += n; //increment next free by n
    nextfree = ROUNDUP((char*) nextfree, PGSIZE);
       
    /*
     * Before we check our max size, let us first discuss what this should be:
     * Currently we are in virtual memory and the variable end[] is places just after the kernel.
     * This means that our *end is somewhere here:
     * ---------------------------------------------------------------
     * | [vkernel_legacy]  [free]            [vkernel] [end]   |     | end of virtual memory (uint32 max)
     * ---------------------------------------------------------------
     *                                               true end  ^
     * NOTE: This is the end of virtual memory!!!, 261MB is not what we have! we have less! (66,5mb)
     * 
     * reference:
     * #Bootstrap GDT
        .p2align 2                                # force 4 byte alignment
        gdt:
          SEG_NULL              # null seg
          SEG(STA_X|STA_R, 0x0, 0xffffffff) # code seg
          SEG(STA_W, 0x0, 0xffffffff)           # data seg
     * 
     * Thanks Japan!
     * http://pekopeko11.sakura.ne.jp/unix_v6/xv6-book/en/_images/F2-2.png
     */
    
    return newAlloc;
}

/*
 * Set up a two-level page table:
 *    kern_pgdir is its linear (virtual) address of the rnpages_basememoot
 *
 * This function only sets up the kernel part of the address space (ie.
 * addresses >= UTOP).  The user part of the address space will be setup later.
 *
 * From UTOP to ULIM, the user is allowed to read but not write.
 * Above ULIM the user cannot read or write.
 */
void mem_init(void)
{
    uint32_t cr0;
    size_t n;

    /* Find out how much memory the machine has (npages & npages_basemem). */
    i386_detect_memory();

    /* Remove this line when you're ready to test this function. */
//    panic("mem_init: This function is not finished\n");

    /*********************************************************************
     * Allocate an array of npages 'struct page_info's and store it in 'pages'.
     * The kernel uses this array to keep track of physical pages: for each
     * physical page, there is a corresponding struct page_info in this array.
     * 'npages' is the number of physical pages in memory.  Your code goes here.
     */
    //npages of boot_alloc required for paging
    //struct page_info *pages;                 /* Physical page state array */
    cprintf("Allocating %u pages.\n", npages);
    pages = boot_alloc(sizeof(struct page_info)*npages); //This panics if Out of Memory

    /*********************************************************************
     * Now that we've allocated the initial kernel data structures, we set
     * up the list of free physical pages. Once we've done so, all further
     * memory management will go through the page_* functions. In particular, we
     * can now map memory using boot_map_region or page_insert.
     */
    page_init();

    check_page_free_list(1);
    check_page_alloc();

    /* ... lab 2 will set up page tables here ... */
}

/***************************************************************
 * Tracking of physical pages.
 * The 'pages' array has one 'struct page_info' entry per physical page.
 * Pages are reference counted, and free pages are kept on a linked list.
 ***************************************************************/

/*
 * Initialize page structure and memory free list.
 * After this is done, NEVER use boot_alloc again.  ONLY use the page
 * allocator functions below to allocate and deallocate physical
 * memory via the page_free_list.
 */
void page_init(void)
{
    /*
     * The example code here marks all physical pages as free.
     * However this is not truly the case.  What memory is free?
     *  1) Mark physical page 0 as in use.
     *     This way we preserve the real-mode IDT and BIOS structures in case we
     *     ever need them.  (Currently we don't, but...)
     *  2) The rest of base memory, [PGSIZE, npages_basemem * PGSIZE) is free.
     *  3) Then comes the IO hole [IOPHYSMEM, EXTPHYSMEM), which must never be
     *     allocated.
     *  4) Then extended memory [EXTPHYSMEM, ...).
     *     Some of it is in use, some is free. Where is the kernel in physical
     *     memory?  Which pages are already in use for page tables and other
     *     data structures?
     *
     * Change the code to reflect this.
     * NB: DO NOT actually touch the physical memory corresponding to free
     *     pages! */
    size_t i;
    bool is_free;
    physaddr_t page_addr;
    char *nextfree = boot_alloc((uint32_t)0);
    uint32_t cf = 0; //free pages counter

    register rpage_control pc0;
    pc0.RPC = 0;

    for (i = 0; i < npages; i++) {
        page_addr = page2pa(&pages[i]);   
        
        //List states of page
        pc0.reg.kernelPage = page_addr >= 0x10000 && page_addr < (uint32_t) nextfree - KERNBASE;  //Kernel allocated space
        pc0.reg.IOhole = (page_addr >= IOPHYSMEM && page_addr < EXTPHYSMEM); //IO hole
        pc0.reg.bios = !i;
        
        //debug print states
//        cprintf("Page %u: K %u, IO %u, bios %u\n", i, pc0.reg.kernelPage, pc0.reg.IOhole, pc0.reg.bios);
        
        //is free if
        //          not kernel              not iohole
        is_free = !pc0.reg.kernelPage && !pc0.reg.IOhole && !pc0.reg.bios;
        
        cf += is_free; //just a statistic counter
        
        pages[i].c0.RPC = pc0.RPC;
        pages[i].pp_ref = !is_free;
        pages[i].pp_link = is_free ? page_free_list : NULL;
        page_free_list = is_free ? &pages[i] : page_free_list;
    }
    
    cprintf("%u free pages. (%uK)\n", cf, (cf*PGSIZE) / 1024);
}

#ifdef BUDDY
static uint32_t* buddy_count;

void* buddy_merge(struct page_info* a) {
    struct page_info *b = BUDDY_GET_BUDDY_PAGE(a,a->c0.reg.buddy_order);
    
    //a must be free
    assert(a->pp_ref==0);
    
    //Before merge, buddies must have the same order
    //We can only recurse to b, not a (eg O(B) < O(A))
    if (a->c0.reg.buddy_order > b->c0.reg.buddy_order)
        b = buddy_merge(b);
    
    assert(a->c0.reg.buddy_order < b->c0.reg.buddy_order); //this may not exist
    
    
    //If b has merged, merge A and B
    
}

void* buddy_split(struct page_info* a) {
    struct page_info *b = BUDDY_GET_BUDDY_PAGE(a,a->c0.reg.buddy_order);
    
    //Buddies must always have the same order
    assert(b->c0.reg.buddy_order == a->c0.reg.buddy_order);
    
    //buddy order must not be 0
    assert(a->c0.reg.buddy_order != 0);
    
    //Both pages must be free
    if (a->pp_ref !=0 || b->pp_ref !=0)
        panic("Invalid split: not all pages free!");
    
    //decrement buddy order
    b->c0.reg.buddy_order = a->c0.reg.buddy_order--;
    
    //TODO: Add b to free list
    //return b to free list as it is now no longer a buddy
    
    //return slave buddy
    return BUDDY_GET_SLAVE(a,b);
}


inline uint32_t buddy_isfree(struct page_info * a) {
    return !(BUDDY_GET_BUDDY_PAGE(a,a->c0.reg.buddy_order))->pp_ref;
}

void buddy_init() {
    //Allocate buddy_count
    buddy_count = boot_alloc(sizeof(uint32_t)*BUDDY);
    
    //set zero
    memset(buddy_count, 0, sizeof(uint32_t)*BUDDY);
    
    //Make buddies of all free pages
    
}
#endif

void prepare_page(struct page_info *page, int alloc_flags) {
    page_free_list = page->pp_link;
    cprintf("free_list: %p\n", page_free_list);

    page->pp_ref = 0;
    page->pp_link = NULL;

    if(alloc_flags & ALLOC_ZERO) {
        memset(page2kva(page), 0, PGSIZE);
    }
}

/*
 * Traverses naively over all pages to find a consecutive block of the given
 * amount of pages.
 */
struct page_info *alloc_consecutive_pages(uint16_t amount, int alloc_flags) {
    size_t i;
    uint16_t hits = 0;
    uint32_t start, end;
    struct page_info *page_hit = NULL, *current, *last_free;
    for(i = 0; i < npages; i++) {
        if(hits >= amount) {
            break;
        }

        if(pages[i].pp_link) {
            hits++;
            page_hit = &pages[i];
        } else {
            hits = 0;
        }
    }

    if(!hits || hits < amount) {
        return NULL;
    }

    if(!page_hit) {
        panic("No page found, but hitcount is correct");
    }

    end = (uint32_t) page2pa(page_hit);
    start = (uint32_t) (end - amount * PGSIZE);

    last_free = page_free_list;
    for(current = page_free_list; current;) {
        if(page2pa(current) >= start && page2pa(current) <= end) {
            /* Reserve page */
            if(page_free_list == current) {
                page_free_list = current->pp_link;
                last_free = page_free_list;

                /* Unlink current page, and move on with its child */
                current->pp_link = NULL;
                current = last_free;
            } else {
                /* Link parent page to current page's child */
                last_free->pp_link = current->pp_link;

                /* Unlink current page, and move on with its child */
                current->pp_link = NULL;
                current = last_free->pp_link;
            }
        } else {
            /* Move on with current page's child */
            current = current->pp_link;
        }
    }

    return pa2page((physaddr_t)start);
}

/*
 * Allocates a physical page.
 * If (alloc_flags & ALLOC_ZERO), fills the entire
 * returned physical page with '\0' bytes.  Does NOT increment the reference
 * count of the page - the caller must do these if necessary (either explicitly
 * or via page_insert).
 * If (alloc_flags & ALLOC_PREMAPPED), returns a physical page from the
 * initial pool of mapped pages.
 *
 * Be sure to set the pp_link field of the allocated page to NULL so
 * page_free can check for double-free bugs.
 *
 * Returns NULL if out of free memory.
 *
 * Hint: use page2kva and memset
 *
 * 4MB huge pages:
 * Come back later to extend this function to support 4MB huge page allocation.
 * If (alloc_flags & ALLOC_HUGE), returns a huge physical page of 4MB size.
 */
struct page_info *page_alloc(int alloc_flags)
{
    struct page_info *page;

    if(!page_free_list) {
        return NULL;
    }

    /* TODO: find out what to do for ALLOC_PREMAPPED */

    if(alloc_flags & ALLOC_HUGE) {
        page = alloc_consecutive_pages((uint16_t) HUGE_PAGE_AMOUNT, alloc_flags);
        page->c0.reg.huge = 1;
        return page;
    }

    /* Pop the top page from the free list */
    page = page_free_list;
    prepare_page(page, alloc_flags);

    return page;
}

/*
 * Return a page to the free list.
 * (This function should only be called when pp->pp_ref reaches 0.)
 */
void page_free(struct page_info *pp)
{
    /* Fill this function in
     * Hint: You may want to panic if pp->pp_ref is nonzero or
     * pp->pp_link is not NULL. */
    uint32_t amount = 1, i;

    if(pp->pp_ref || pp->pp_link) {
        panic("Page contained free list reference, or had nonzero refcount during free()");
    }

    if(pp->c0.reg.huge) {
        amount = HUGE_PAGE_AMOUNT;
    }

    for(i = 0; i < amount; i++){
        pp->pp_link = page_free_list;
        page_free_list = pp;

        /* Move to next page. Used if i > 1 */
        pp = (struct page_info*)(page2pa(pp) + PGSIZE);
    }
}

/*
 * Decrement the reference count on a page,
 * freeing it if there are no more refs.
 */
void page_decref(struct page_info* pp)
{
    if (--pp->pp_ref == 0)
        page_free(pp);
}


/***************************************************************
 * Checking functions.
 ***************************************************************/

/*
 * Check that the pages on the page_free_list are reasonable.
 */
static void check_page_free_list(bool only_low_memory)
{
    struct page_info *pp;
    unsigned pdx_limit = only_low_memory ? 1 : NPDENTRIES;
    int nfree_basemem = 0, nfree_extmem = 0;
    char *first_free_page;

    if (!page_free_list)
        panic("'page_free_list' is a null pointer!");

    if (only_low_memory) {
        /* Move pages with lower addresses first in the free list, since
         * entry_pgdir does not map all pages. */
        struct page_info *pp1, *pp2;
        struct page_info **tp[2] = { &pp1, &pp2 };
        for (pp = page_free_list; pp; pp = pp->pp_link) {
            int pagetype = PDX(page2pa(pp)) >= pdx_limit;
            *tp[pagetype] = pp;
            tp[pagetype] = &pp->pp_link;
        }
        *tp[1] = 0;
        *tp[0] = pp2;
        page_free_list = pp1;
    }

    /* if there's a page that shouldn't be on the free list,
     * try to make sure it eventually causes trouble. */
    for (pp = page_free_list; pp; pp = pp->pp_link)
        if (PDX(page2pa(pp)) < pdx_limit)
            memset(page2kva(pp), 0x97, 128);

    first_free_page = (char *) boot_alloc(0);
    for (pp = page_free_list; pp; pp = pp->pp_link) {
        /* check that we didn't corrupt the free list itself */
        assert(pp >= pages);
        assert(pp < pages + npages);
        assert(((char *) pp - (char *) pages) % sizeof(*pp) == 0);

        /* check a few pages that shouldn't be on the free list */
        assert(page2pa(pp) != 0);
        assert(page2pa(pp) != IOPHYSMEM);
        assert(page2pa(pp) != EXTPHYSMEM - PGSIZE);
        assert(page2pa(pp) != EXTPHYSMEM);
        assert(page2pa(pp) < EXTPHYSMEM || (char *) page2kva(pp) >= first_free_page);

        if (page2pa(pp) < EXTPHYSMEM)
            ++nfree_basemem;
        else
            ++nfree_extmem;
    }

    assert(nfree_basemem > 0);
    assert(nfree_extmem > 0);
}

/*
 * Check the physical page allocator (page_alloc(), page_free(),
 * and page_init()).
 */
static void check_page_alloc(void)
{
    struct page_info *pp, *pp0, *pp1, *pp2;
    struct page_info *php0, *php1, *php2;
    int nfree, total_free;
    struct page_info *fl;
    char *c;
    int i;

    if (!pages)
        panic("'pages' is a null pointer!");

    /* check number of free pages */
    for (pp = page_free_list, nfree = 0; pp; pp = pp->pp_link)
        ++nfree;
    total_free = nfree;

    /* should be able to allocate three pages */
    pp0 = pp1 = pp2 = 0;
    assert((pp0 = page_alloc(0)));
    assert((pp1 = page_alloc(0)));
    assert((pp2 = page_alloc(0)));

    assert(pp0);
    assert(pp1 && pp1 != pp0);
    assert(pp2 && pp2 != pp1 && pp2 != pp0);
    assert(page2pa(pp0) < npages*PGSIZE);
    assert(page2pa(pp1) < npages*PGSIZE);
    assert(page2pa(pp2) < npages*PGSIZE);

    /* temporarily steal the rest of the free pages.
     *
     * Lab 1 Bonus:
     * For the bonus, if you go for a different design for the page allocator,
     * then do update here suitably to simulate a no-free-memory situation */
    fl = page_free_list;
    page_free_list = 0;

    /* should be no free memory */
    assert(!page_alloc(0));

    /* free and re-allocate? */
    page_free(pp0);
    page_free(pp1);
    page_free(pp2);
    pp0 = pp1 = pp2 = 0;
    assert((pp0 = page_alloc(0)));
    assert((pp1 = page_alloc(0)));
    assert((pp2 = page_alloc(0)));
    assert(pp0);
    assert(pp1 && pp1 != pp0);
    assert(pp2 && pp2 != pp1 && pp2 != pp0);
    assert(!page_alloc(0));

    /* test flags */
    memset(page2kva(pp0), 1, PGSIZE);
    page_free(pp0);
    assert((pp = page_alloc(ALLOC_ZERO)));
    assert(pp && pp0 == pp);
    c = page2kva(pp);
    for (i = 0; i < PGSIZE; i++)
        assert(c[i] == 0);

    /* give free list back */
    page_free_list = fl;

    /* free the pages we took */
    page_free(pp0);
    page_free(pp1);
    page_free(pp2);

    /* number of free pages should be the same */
    for (pp = page_free_list; pp; pp = pp->pp_link)
        --nfree;
    assert(nfree == 0);

    cprintf("[4K] check_page_alloc() succeeded!\n");
   
    /* test allocation of huge page */
    pp0 = pp1 = php0 = 0;
    assert((pp0 = page_alloc(0)));
    assert((php0 = page_alloc(ALLOC_HUGE)));
    assert((pp1 = page_alloc(0)));
    assert(pp0);
    assert(php0 && php0 != pp0);
    assert(pp1 && pp1 != php0 && pp1 != pp0);
    assert(0 == (page2pa(php0) % 1024*PGSIZE));
    if (page2pa(pp1) > page2pa(php0)) {
        assert(page2pa(pp1) - page2pa(php0) >= 1024*PGSIZE);
    }

    /* free and reallocate 2 huge pages */
    page_free(php0);
    page_free(pp0);
    page_free(pp1);
    php0 = php1 = pp0 = pp1 = 0;
    assert((php0 = page_alloc(ALLOC_HUGE)));
    assert((php1 = page_alloc(ALLOC_HUGE)));

    /* Is the inter-huge-page difference right? */
    if (page2pa(php1) > page2pa(php0)) {
        assert(page2pa(php1) - page2pa(php0) >= 1024*PGSIZE);
    } else {
        assert(page2pa(php0) - page2pa(php1) >= 1024*PGSIZE);
    }

    /* free the huge pages we took */
    page_free(php0);
    page_free(php1);

    /* number of free pages should be the same */
    nfree = total_free;
    for (pp = page_free_list; pp; pp = pp->pp_link)
        --nfree;
    assert(nfree == 0);

    cprintf("[4M] check_page_alloc() succeeded!\n");
}

