/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_TRAP_H
#define JOS_KERN_TRAP_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/trap.h>
#include <inc/mmu.h>

#define IA32_SYSENTER_CS 0x0174
#define IA32_SYSENTER_ESP 0x0175
#define IA32_SYSENTER_EIP 0x0176

/* The kernel's interrupt descriptor table */
extern struct gatedesc idt[];
extern struct pseudodesc idt_pd;

void trap_init(void);
void trap_prep_sysenter();
void trap_init_percpu(void);
void print_regs(struct pushregs *regs);
void print_trapframe(struct trapframe *tf);
void page_fault_handler(struct trapframe *);
void breakpoint_handler(struct trapframe *);
void backtrace(struct trapframe *);

void trap_divzero();
void trap_debug();
void trap_nmi();
void trap_break();
void trap_overflow();
void trap_bound();
void trap_illop();
void trap_device();
void trap_doublefault();
void trap_invtss();
void trap_segnp();
void trap_stack();
void trap_genprotfault();
void trap_pagefault();
void trap_floaterr();
void trap_aligncheck();
void trap_machcheck();
void trap_simderr();
void trap_syscall();
void trap_default();

/* Special trap to host the sysenter opcode's call */
void trap_sysenter();

#endif /* JOS_KERN_TRAP_H */
