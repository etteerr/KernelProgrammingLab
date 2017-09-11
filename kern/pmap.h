/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_PMAP_H
#define JOS_KERN_PMAP_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/memlayout.h>
#include <inc/assert.h>

extern char bootstacktop[], bootstack[];

extern struct page_info *pages;
extern size_t npages;

//The 4MiB mapping at boot
extern pde_t *kern_pgdir;

/** 
 * INVLPG—Invalidate TLB Entries
 * The INVLPG instruction is a privileged instruction. 
 * When the processor is running in protected mode, 
 * the CPL must be 0 to execute this instruction.
 */
#define INVALIDATE_TLB(M) invlpg(M);

/*
 * States if page A and B are the same page
 * This helps to check if we are in the page directory or not
 */
#define SAME_PAGE_4K(A,B) (((uint32_t)A & 0xFFFFF000) == ((uint32_t)B & 0xFFFFF000))

/*
 * Defines for Page Directory entries
 */
#define PDE_PHYS_ADDRESS        12 //Of a  pgtable
#define PDE_BIT_PRESENT         0
#define PDE_BIT_RW              1
#define PDE_BIT_USER            2
#define PDE_BIT_WRITETHROUGH    3
#define PDE_BIT_DISABLECACHE    4
#define PDE_BIT_ACCESSED        5
#define PDE_BIT_HUGE            7

/*
 * Defines for page table entries
 */
#define PTE_PHYS_ADDRESS        12 //Of physical page
#define PTE_BIT_PRESENT         0
#define PTE_BIT_RW              1
#define PTE_BIT_USER            2
#define PTE_BIT_WRITETHROUGH    3
#define PTE_BIT_DISABLECACHE    4
#define PTE_BIT_ACCESSED        5
#define PTE_BIT_DIRTY           6
#define PTE_BIT_GLOBAL          8
/*
 * Gets physical page address (4096 alligned) from a page directory entry
 * This address thus is the beginning of a pg table
 * 
 * Note: This can be used to read 4M addresses. 
 * Note that bit 21 to 12 are reserved and must not be written to, 
 * hence they must be 4M alligned as well!
 */
#define PDE_GET_PHYS_ADDRESS(A) (A & 0xFFFFF000)
/*
 * Gets physical page address (4096 alligned) from a page directory entry
 * This address thus is the physical page
 */
#define PTE_GET_PHYS_ADDRESS(A) (A & 0xFFFFF000)
/*
 * Gets the present bit from a page directory entry
 * 
 * When P is not set, the processor ignores the rest of the entry 
 * and you can use all remaining 31 bits for extra information, 
 * like recording where the page has ended up in swap space.
 * ~ OSDEV.wiki
 */
#define PDE_GET_BIT_PRESENT(A) (uint32_t)A & 0x1
/*
 * Gets the (read) write permission bit
 */
#define PDE_GET_BIT_RW(A) ((uint32_t)A>>PDE_BIT_RW) & 0x1
/*
 * Get user permission bit (user allowed access if set)
 */
#define PDE_GET_BIT_USER(A) ((uint32_t)A>>PDE_BIT_USER) & 0x1
/*
 * Get the writethrough bit, if set write-back disabled (eg. no write cache)
 */
#define PDE_GET_BIT_WRITETHROUGH(A) ((uint32_t)A>>PDE_BIT_WRITETHROUGH) & 0x1
/*
 * If set, page will not be cached
 */
#define PDE_GET_BIT_DISABLE_CACHE(A) ((uint32_t)A>>PDE_BIT_DISABLECACHE) & 0x1
/*
 * Bit is set if page has been written to
 * Bit can be cleared by OS, is not done by CPU
 */
#define PDE_GET_BIT_ACCESSED(A) ((uint32_t)A>>PDE_BIT_ACCESSED) & 0x1
/*
 * if set, entry is considered to be a 4MiB page
 */
#define PDE_GET_BIT_HUGE_PAGE(A) ((uint32_t)A>>PDE_BIT_HUGE) & 0x1


/*
 * if set, Physical page information is assumed to be present
 */
#define PTE_GET_BIT_PRESENT(A)      ((uint32_t)A>>PTE_BIT_PRESENT) & 0x1
/*
 * If set, write access is granted to page
 */
#define PTE_GET_BIT_RW(A)           ((uint32_t)A>>PTE_BIT_RW) & 0x1
/*
 * If set, user may access page
 */
#define PTE_GET_BIT_USER(A)         ((uint32_t)A>>PTE_BIT_USER) & 0x1
/*
 * If set, write-through enabled (write-back disabled)
 */
#define PTE_GET_BIT_WRITETHROUGH(A) ((uint32_t)A>>PTE_BIT_WRITETHROUGH) & 0x1
/*
 * If set, page will not be cached
 */
#define PTE_GET_BIT_DISABLECACHE(A) ((uint32_t)A>>PTE_BIT_DISABLECACHE) & 0x1
/*
 * Set by cpu. if set, page is accessed. OS must clear if OS needs this.
 */
#define PTE_GET_BIT_ACCESSED(A)     ((uint32_t)A>>PTE_BIT_ACCESSED) & 0x1
/*
 * (Is set by cpu?) Must be unset by OS?
 * "If the Dirty flag ('D') is set, then the page has been written to. This flag is not updated by the CPU, and once set will not unset itself." ~ OSDEV.wiki
 */
#define PTE_GET_BIT_DIRTY(A)        ((uint32_t)A>>PTE_BIT_DIRTY) & 0x1
/*
 * "The Global, or 'G' above, flag, if set, 
 * prevents the TLB from updating the address in its cache if CR3 is reset. 
 * Note, that the page global enable bit in CR4 must be set to enable this feature."
 * ~ OSDEV.wiki
 */
#define PTE_GET_BIT_GLOBAL(A)       ((uint32_t)A>>PTE_BIT_GLOBAL) & 0x1


#define VA_GET_PDE_INDEX(A) ((uint32_t) A >> 22) & 0x3FF //10bit mask
#define VA_GET_PTE_INDEX(A) ((uint32_t) A >> 12) & 0x3FF

/* This macro takes a kernel virtual address -- an address that points above
 * KERNBASE, where the machine's maximum 256MB of physical memory is mapped --
 * and returns the corresponding physical address.  It panics if you pass it a
 * non-kernel virtual address.
 */
#define PADDR(kva) _paddr(__FILE__, __LINE__, kva)
#define HUGE_PAGE_AMOUNT 1024

static inline physaddr_t _paddr(const char *file, int line, void *kva)
{
    if ((uint32_t)kva < KERNBASE)
        _panic(file, line, "PADDR called with invalid kva %08lx", kva);
    return (physaddr_t)kva - KERNBASE;
}

/* This macro takes a physical address and returns the corresponding kernel
 * virtual address.  It panics if you pass an invalid physical address. */
#define KADDR(pa) _kaddr(__FILE__, __LINE__, pa)

static inline void *_kaddr(const char *file, int line, physaddr_t pa)
{
    if (PGNUM(pa) >= npages)
        _panic(file, line, "KADDR called with invalid pa %08lx", pa);
    return (void *)(pa + KERNBASE);
}


enum {
    /* For page_alloc, zero the returned physical page. */
    ALLOC_ZERO = 1<<0,
    ALLOC_HUGE = 1<<1,
    ALLOC_PREMAPPED = 1<<2,
};

enum {
    /* For pgdir_walk, tells whether to create normal page or huge page */
    CREATE_NORMAL = 1<<0,
    CREATE_HUGE   = 1<<1,
};

void mem_init(void);

void page_init(void);
struct page_info *page_alloc(int alloc_flags);
void page_free(struct page_info *pp);
int page_insert(pde_t *pgdir, struct page_info *pp, void *va, int perm);
void page_remove(pde_t *pgdir, void *va);
struct page_info *page_lookup(pde_t *pgdir, void *va, pte_t **pte_store);
void page_decref(struct page_info *pp);

void tlb_invalidate(pde_t *pgdir, void *va);

static inline physaddr_t page2pa(struct page_info *pp)
{
    return (pp - pages) << PGSHIFT;
}

static inline struct page_info *pa2page(physaddr_t pa)
{
    if (PGNUM(pa) >= npages)
        panic("pa2page called with invalid pa");
    return &pages[PGNUM(pa)];
}

static inline void *page2kva(struct page_info *pp)
{
    return KADDR(page2pa(pp));
}

#endif /* !JOS_KERN_PMAP_H */
