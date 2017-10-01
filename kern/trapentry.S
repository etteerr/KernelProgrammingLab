/* See COPYRIGHT for copyright information. */

#include "../inc/mmu.h"
#include "../inc/trap.h"
#include "../inc/memlayout.h"



###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)                                                 \
    .globl name;            /* define global symbol for 'name' */              \
    .type name, @function;  /* symbol type is function */                      \
    .align 2;               /* align function definition */                    \
    name:                   /* function starts here */                         \
    pushl $(num);                                                              \
    jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)                                            \
    .globl name;                                                               \
    .type name, @function;                                                     \
    .align 2;                                                                  \
    name:                                                                      \
    pushl $0;                                                                  \
    pushl $(num);                                                              \
    jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
TRAPHANDLER_NOEC(trap_divzero, T_DIVIDE)
TRAPHANDLER_NOEC(trap_debug, T_DEBUG)
TRAPHANDLER_NOEC(trap_nmi, T_NMI)
TRAPHANDLER_NOEC(trap_break, T_BRKPT)
TRAPHANDLER_NOEC(trap_overflow, T_OFLOW)
TRAPHANDLER_NOEC(trap_bound, T_BOUND)
TRAPHANDLER_NOEC(trap_illop, T_ILLOP)
TRAPHANDLER_NOEC(trap_device, T_DEVICE)
TRAPHANDLER(trap_doublefault, T_DBLFLT)
TRAPHANDLER(trap_invtss, T_TSS)
TRAPHANDLER(trap_segnp, T_SEGNP)
TRAPHANDLER(trap_stack, T_STACK)
TRAPHANDLER(trap_genprotfault, T_GPFLT)
TRAPHANDLER(trap_pagefault, T_PGFLT)
TRAPHANDLER(trap_floaterr, T_FPERR)
TRAPHANDLER(trap_aligncheck, T_ALIGN)
TRAPHANDLER(trap_machcheck, T_MCHK)
TRAPHANDLER(trap_simderr, T_SIMDERR)
TRAPHANDLER_NOEC(trap_syscall, T_SYSCALL)
TRAPHANDLER(trap_default, T_DEFAULT)


TRAPHANDLER_NOEC(trap_irq_timer, (IRQ_OFFSET + IRQ_TIMER))
TRAPHANDLER_NOEC(trap_irq_kbd, (IRQ_OFFSET + IRQ_KBD))
TRAPHANDLER_NOEC(trap_irq_3, (IRQ_OFFSET + 3))
TRAPHANDLER_NOEC(trap_irq_serial, (IRQ_OFFSET + IRQ_SERIAL))
TRAPHANDLER_NOEC(trap_irq_5, (IRQ_OFFSET + 5))
TRAPHANDLER_NOEC(trap_irq_6, (IRQ_OFFSET + 6))
TRAPHANDLER_NOEC(trap_irq_spur, (IRQ_OFFSET + IRQ_SPURIOUS))
TRAPHANDLER_NOEC(trap_irq_8, (IRQ_OFFSET + 8))
TRAPHANDLER_NOEC(trap_irq_9, (IRQ_OFFSET + 9))
TRAPHANDLER_NOEC(trap_irq_10, (IRQ_OFFSET + 10))
TRAPHANDLER_NOEC(trap_irq_11, (IRQ_OFFSET + 11))
TRAPHANDLER_NOEC(trap_irq_12, (IRQ_OFFSET + 12))
TRAPHANDLER_NOEC(trap_irq_13, (IRQ_OFFSET + 13))
TRAPHANDLER_NOEC(trap_irq_ide, (IRQ_OFFSET + IRQ_IDE))
TRAPHANDLER_NOEC(trap_irq_15, (IRQ_OFFSET + 15))
TRAPHANDLER_NOEC(trap_irq_err, (IRQ_OFFSET + IRQ_ERROR))

#define IRQ_OFFSET  32  /* IRQ 0 corresponds to int IRQ_OFFSET */

/* Hardware IRQ numbers. We receive these as (IRQ_OFFSET+IRQ_WHATEVER) */
#define IRQ_TIMER        0
#define IRQ_KBD          1
#define IRQ_SERIAL       4
#define IRQ_SPURIOUS     7
#define IRQ_IDE         14
#define IRQ_ERROR       19


/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
    /* Set DS and ES to GD_KD */
    /* NOTE: AX, DX, CX, BX, DI, SI reserved! */
    push %eax //save eax
    mov $GD_KD, %ax 
    mov %ax, %ds
    mov %ax, %es
    pop %eax //restore eax
    /* Push all registers in struct pushregs to stack */
    push %ds
    push %es
    pushal //E-ACDB-X  SP BP SI DI
    /* Push pointer to stack as argument to trap() */
    pushl %esp
    call trap
spin: //We should not come here
    jmp spin
