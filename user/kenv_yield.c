/* Example kernel env. */

#include "../inc/lib.h"

void umain(int argc, char **argv)
{
    cprintf("Hello from kernel env! Should be able to read from kernel space: %llx\n", *(unsigned*)0xf0100000);
    sys_yield();
    cprintf("Yielding is fine\n");
    
    while(1) {
        sys_yield();
    }
}

