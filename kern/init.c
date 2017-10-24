/* See COPYRIGHT for copyright information. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <kern/vma.h>
#include <kern/monitor.h>
#include <kern/console.h>
#include <kern/pmap.h>
#include <kern/kclock.h>
#include <kern/env.h>
#include <kern/trap.h>
#include <kern/sched.h>
#include <kern/picirq.h>
#include <kern/cpu.h>
#include <kern/spinlock.h>
#include <kern/ide.h>

static void boot_aps(void);

#include "vma.h"
#include "swappy.h"
#include "kswapd.h"
#include "kernel_threads.h"
#include "../inc/stdio.h"


void i386_init(void)
{
    extern char edata[], end[];

    /* Before doing anything else, complete the ELF loading process.
     * Clear the uninitialized global data (BSS) section of our program.
     * This ensures that all static/global variables start out zero. */
    memset(edata, 0, end - edata);

    /* Initialize the console.
     * Can't call cprintf until after we do this! */
    cons_init();

    /* Lab 1 and 2 memory management initialization functions. */
    mem_init();

    /* Assertions */
    assert(sizeof(struct vma_arr)<= PGSIZE);

    /* Lab 3 user environment initialization functions. */
    env_init();
    trap_init();

    /* Lab 5 and 6 multiprocessor initialization functions */
    mp_init();
    lapic_init();

    /* Lab 5 multitasking initialization functions */
    pic_init();

    ide_init();
    
    /* Initialize swappy (global space) */
    swappy_init();
    
    /* Test swap */
    swappy_unit_test_case();
    
    /* Starting non-boot CPUs */
    dprintf("Bootcpu: Starting aps...\n");
    boot_aps();
    dprintf("Bootcpu: Starting aps... done!\n");

    /* Start essential kernel services */
    swappy_start_service();
    kswapd_start_service();

#if defined(TEST)
    /* Don't touch -- used by grading script! */
    ENV_CREATE(TEST, ENV_TYPE_USER);
#else
    /* Touch all you want. */
    ENV_CREATE(user_yield, ENV_TYPE_USER);
//    ENV_CREATE(user_cowforktest, ENV_TYPE_USER);
    kern_thread_create(test_thread);
#endif
    
    /* Schedule and run the first user environment! */
    sched_yield();
}

/*
 * While boot_aps is booting a given CPU, it communicates the per-core
 * stack pointer that should be loaded by mpentry.S to that CPU in
 * this variable.
 */
void *mpentry_kstack;

/*
 * Start the non-boot (AP) processors.
 */
static void boot_aps(void)
{
    extern unsigned char mpentry_start[], mpentry_end[];
    void *code;
    struct cpuinfo *c;

    /* Write entry code to unused memory at MPENTRY_PADDR */
    code = KADDR(MPENTRY_PADDR);
    memmove(code, mpentry_start, mpentry_end - mpentry_start);

    /* Boot each AP one at a time */
    for (c = cpus; c < cpus + ncpu; c++) {
        if (c == cpus + cpunum())  /* We've started already. */
            continue;

        /* Tell mpentry.S what stack to use */
        mpentry_kstack = percpu_kstacks[c - cpus] + KSTKSIZE;
        dprintf("Stack: %p\n", mpentry_kstack);
        /* Start the CPU at mpentry_start */
        lapic_startap(c->cpu_id, PADDR(code));
        /* Wait for the CPU to finish some basic setup in mp_main() */
        while(c->cpu_status != CPU_STARTED)
            ;
    }
}

/*
 * Setup code for APs.
 */
void mp_main(void)
{
    /* We are in high EIP now, safe to switch to kern_pgdir */
    lcr3(PADDR(kern_pgdir));
    cprintf("SMP: CPU %d starting\n", cpunum());

    cprintf("Init lapic (cpu %d)\n", cpunum());
    lapic_init();
    cprintf("Init env (cpu %d)\n", cpunum());
    env_init_percpu();
    cprintf("Init traps (cpu %d)\n", cpunum());
    trap_init_percpu();
    cprintf("set status to CPU_STARTED (cpu %d)\n", cpunum());
    xchg(&thiscpu->cpu_status, CPU_STARTED); /* tell boot_aps() we're up */

    /*
     * Now that we have finished some basic setup, call sched_yield()
     * to start running processes on this CPU.  But make sure that
     * only one CPU can enter the scheduler at a time!
     *
     * Note Tom: our scheduler uses atomic cooperative iteration of the envs,
     * So multiple cores can enter it concurrently safely.
     */
    cprintf("CPU %d startup done, waiting for cpu0 to complete booting\n", cpunum());
    sched_yield();

    /* Remove this after you initialize per-CPU trap information */
    for (;;);
}

/*
 * Variable panicstr contains argument to first call to panic; used as flag
 * to indicate that the kernel has already called panic.
 */
const char *panicstr;

/*
 * Panic is called on unresolvable fatal errors.
 * It prints "panic: mesg", and then enters the kernel monitor.
 */
void _panic(const char *file, int line, const char *fmt,...)
{
    va_list ap;

    if (panicstr)
        goto dead;
    panicstr = fmt;

    /* Be extra sure that the machine is in as reasonable state */
    __asm __volatile("cli; cld");

    va_start(ap, fmt);
    cprintf(KBSD KBLK"kernel panic on CPU %d at %s:%d: ", cpunum(), file, line);
    vcprintf(fmt, ap);
    cprintf("\n"KNRM);
    va_end(ap);

dead:
    /* break into the kernel monitor */
    while (1)
        monitor(NULL);
}

/* Like panic, but don't. */
void _warn(const char *file, int line, const char *fmt,...)
{
    va_list ap;

    va_start(ap, fmt);
    cprintf("kernel warning at %s:%d: ", file, line);
    vcprintf(fmt, ap);
    cprintf("\n");
    va_end(ap);
}
