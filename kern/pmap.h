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

/*
 * Gets physical page address (4096 alligned) from a page directory entry
 */
#define PDE_GET_PHYS_ADDRESS(A) A & 0xFFFFF000;
/*
 * Gets the present bit from a page directory entry
 */
#define PDE_GET_BIT_PRESENT(A) A & 0x1;
/*
 * Gets the (read) write permission bit
 */
#define PDE_GET_BIT_RW(A) A>>1 & 0x1;
/*
 * Get user permission bit (user allowed access if set)
 */
#define PDE_GET_BIT_USER(A) A>>2 & 0x1;
/*
 * Get the writethrough bit, if set write-back disabled (eg. no write cache)
 */
#define PDE_GET_BIT_WRITETHROUGH(A) A>>3 & 0x1;
/*
 * If set, page will not be cached
 */
#define PDE_GET_BIT_DISABLE_CACHE(A) A>>4 & 0x1;
/*
 * Bit is set if page has been written to
 * Bit can be cleared by OS, is not done by CPU
 */
#define PDE_GET_BIT_ACCESSED(A) A>>5 & 0x1;
/*
 * if set, entry is considered to be a 4MiB page
 */
#define PDE_GET_BIT_HUGE_PAGE(A) A>>6 & 0x1;

/**************
 * Me struct:
 *  Page table entry
 *  1024 * uint32_t
 *  Super easy mapping for paging
 */
typedef struct {
    pde_t entry[1024];
} pgtable;

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
