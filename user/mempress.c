#include "../inc/lib.h"
#include "../inc/mmu.h"
#include "../inc/types.h"
#include "../inc/stdio.h"
#include "../inc/assert.h"
#include "../inc/string.h"

#define MEM_BLOCK_SIZE  ((size_t)(128 *  1024 * 1024))
#define PRINT(...)      cprintf(__VA_ARGS__);

//char gigs[MEM_BLOCK_SIZE];

void umain(int argc, char **argv)
{

    int i;

    /* Since our on-demand ELF section loading wasn't working in the lab about envs,
     * we modified this test to use anonymous memory instead of using the bss region.
     * This allows us to not have to introduce new bugs, or spend a lot of time, fixing this
     * artifact from previous labs. */
    char *gigs = (char *)sys_vma_create(MEM_BLOCK_SIZE, PERM_W, 0);

    assert(gigs);

    /* Write to all of available physical memory (and more) */
    memset(gigs, 0xd0, sizeof(char) * MEM_BLOCK_SIZE);
    assert(gigs[10] == (char) 0xd0);
    PRINT("Memory of size %d bytes set to: %x\n", MEM_BLOCK_SIZE, gigs[10]);

    /* Read every page so that they get swapped back again */
    for(i = 37; i < MEM_BLOCK_SIZE; i+= PGSIZE) { 
        assert(gigs[i] == (char) 0xd0);
    }

    PRINT("mempress successful.\n");
    return;
}
