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
    PRINT("Allocating memory as VMA\n");
    char *gigs = (char *)sys_vma_create(MEM_BLOCK_SIZE, PERM_W, 0);
    PRINT("Range allocated: %p to %p\n", gigs, gigs + MEM_BLOCK_SIZE);

    assert(gigs);

    /* Write to all of available physical memory (and more) */
    PRINT("Memsetting to 0xd0...\n");
    //memset(gigs, 0xd0, sizeof(char) * MEM_BLOCK_SIZE);
    for(uint32_t i = 0; i<sizeof(char) * MEM_BLOCK_SIZE; i+=sizeof(uint32_t)) {
        *((uint32_t*)(gigs+i)) = 0xd0d0d0d0;
        if (i%(1<<20)==0) {
            cprintf("User memset %d of %d MiB\n", i>>20, (sizeof(char) * MEM_BLOCK_SIZE)>>20);
            cprintf("Current address %p\n", gigs+i);
        }

    }
    cprintf("%p\n", gigs[10]);
    assert(gigs[10] == (char) 0xd0);
    PRINT("Memory of size %d bytes set to: %x\n", MEM_BLOCK_SIZE, gigs[10]);

    /* Read every page so that they get swapped back again */
    for(i = 37; i < MEM_BLOCK_SIZE; i+= PGSIZE) { 
        assert(gigs[i] == (char) 0xd0);
    }

    PRINT("mempress successful.\n");
    return;
}
