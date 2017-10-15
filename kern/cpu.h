
#ifndef JOS_INC_CPU_H
#define JOS_INC_CPU_H

/* Maximum number of CPUs */
#define NCPU  8

#ifndef __ASSEMBLER__

#include <inc/types.h>
#include <inc/memlayout.h>
#include <inc/mmu.h>
#include <inc/env.h>



/* Values of status in struct cpuinfo */
enum {
    CPU_UNUSED = 0,
    CPU_STARTED,
    CPU_HALTED,
};

/* Per-CPU state */
struct cpuinfo {
    uint8_t cpu_id;                /* Local APIC ID; index into cpus[] below */
    volatile unsigned cpu_status;  /* The status of the CPU */
    struct env *cpu_env;           /* The currently-running environment. */
    struct taskstate cpu_ts;       /* Used by x86 to find stack for interrupt */
};

/* Initialized in mpconfig.c */
extern struct cpuinfo cpus[NCPU];
extern int ncpu;                   /* Total number of CPUs in the system */
extern struct cpuinfo *bootcpu;    /* The boot-strap processor (BSP) */
extern physaddr_t lapicaddr;       /* Physical MMIO address of the local APIC */

/* Per-CPU kernel stacks
 * CPU 0 is stored in the high memory part (esp = &percpu_kstacks[NCPU][LSTKSIZE])
 * CPU 1 is than stored as one part lower: (esp = &percpu_kstacks[NCPU - 1][LSTKSIZE])
 */
extern unsigned char percpu_kstacks[NCPU][KSTKSIZE];

int cpunum(void);
#define thiscpu (&cpus[cpunum()])

void mp_init(void);
void lapic_init(void);
void lapic_startap(uint8_t apicid, uint32_t addr);
void lapic_eoi(void);
void lapic_ipi(int vector);

#endif //assembler

#endif
