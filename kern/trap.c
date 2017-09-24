#include "../inc/env.h"
#include "../inc/mmu.h"
#include "../inc/x86.h"
#include "../inc/stdio.h"
#include "../inc/trap.h"
#include "../inc/types.h"
#include "../inc/assert.h"
#include "../inc/memlayout.h"

#include "env.h"
#include "vma.h"
#include "pmap.h"
#include "trap.h"
#include "monitor.h"
#include "syscall.h"
#include "../inc/error.h"

static struct taskstate ts;

/*
 * For debugging, so print_trapframe can distinguish between printing a saved
 * trapframe and printing the current trapframe and print some additional
 * information in the latter case.
 */
static struct trapframe *last_tf;

/*
 * Interrupt descriptor table.  (Must be built at run time because shifted
 * function addresses can't be represented in relocation records.)
 */
struct gatedesc idt[256] = { { 0 } };
struct pseudodesc idt_pd = {
    sizeof(idt) - 1, (uint32_t) idt
};


static const char *trapname(int trapno)
{
    static const char * const excnames[] = {
        "Divide error",
        "Debug",
        "Non-Maskable Interrupt",
        "Breakpoint",
        "Overflow",
        "BOUND Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack Fault",
        "General Protection",
        "Page Fault",
        "(unknown trap)",
        "x87 FPU Floating-Point Error",
        "Alignment Check",
        "Machine-Check",
        "SIMD Floating-Point Exception"
    };

    if (trapno < sizeof(excnames)/sizeof(excnames[0]))
        return excnames[trapno];
    if (trapno == T_SYSCALL)
        return "System call";
    return "(unknown trap)";
}

void trap_init(void)
{
    extern struct segdesc gdt[];

    /* LAB 3: Your code here. */
    SETGATE(idt[T_DIVIDE], 0, GD_KT, (uint32_t)&trap_divzero, 0);
    SETGATE(idt[T_DEBUG], 0, GD_KT, (uint32_t)&trap_debug, 0);
    SETGATE(idt[T_NMI], 0, GD_KT, (uint32_t)&trap_nmi, 3);
    SETGATE(idt[T_BRKPT], 0, GD_KT, (uint32_t)&trap_break, 3);
    SETGATE(idt[T_OFLOW], 0, GD_KT, (uint32_t)&trap_overflow, 0);
    SETGATE(idt[T_BOUND], 0, GD_KT, (uint32_t)&trap_bound, 0);
    SETGATE(idt[T_ILLOP], 0, GD_KT, (uint32_t)&trap_illop, 0);
    SETGATE(idt[T_DEVICE], 0, GD_KT, (uint32_t)&trap_device, 0);
    SETGATE(idt[T_DBLFLT], 0, GD_KT, (uint32_t)&trap_doublefault, 0);
    SETGATE(idt[T_TSS], 0, GD_KT, (uint32_t)&trap_invtss, 0);
    SETGATE(idt[T_SEGNP], 0, GD_KT, (uint32_t)&trap_segnp, 0);
    SETGATE(idt[T_STACK], 0, GD_KT, (uint32_t)&trap_stack, 0);
    SETGATE(idt[T_GPFLT], 0, GD_KT, (uint32_t)&trap_genprotfault, 0);
    SETGATE(idt[T_PGFLT], 0, GD_KT, (uint32_t)&trap_pagefault, 0);
    SETGATE(idt[T_FPERR], 0, GD_KT, (uint32_t)&trap_floaterr, 0);
    SETGATE(idt[T_ALIGN], 0, GD_KT, (uint32_t)&trap_aligncheck, 0);
    SETGATE(idt[T_MCHK], 0, GD_KT, (uint32_t)&trap_machcheck, 0);
    SETGATE(idt[T_SIMDERR], 0, GD_KT, (uint32_t)&trap_simderr, 0);
    SETGATE(idt[T_SYSCALL], 0, GD_KT, (uint32_t)&trap_syscall, 3);
    SETGATE(idt[T_DEFAULT], 0, GD_KT, (uint32_t)&trap_default, 0);

    /* Per-CPU setup */
    trap_init_percpu();
#ifdef BONUS_LAB3
    trap_prep_sysenter();
#endif
}

void trap_prep_sysenter() {
    asm volatile("wrmsr"::"c"(IA32_SYSENTER_CS), "d"(0), "a"(GD_KT));
    asm volatile("wrmsr"::"c"(IA32_SYSENTER_ESP), "d"(0), "a"((void *)KSTACKTOP));
    asm volatile("wrmsr"::"c"(IA32_SYSENTER_EIP), "d"(0), "a"(&trap_sysenter));
}

/* Initialize and load the per-CPU TSS and IDT. */
void trap_init_percpu(void)
{
    /* Setup a TSS so that we get the right stack when we trap to the kernel. */
    ts.ts_esp0 = KSTACKTOP;
    ts.ts_ss0 = GD_KD;

    /* Initialize the TSS slot of the gdt. */
    gdt[GD_TSS0 >> 3] = SEG16(STS_T32A, (uint32_t) (&ts),
                    sizeof(struct taskstate), 0);
    gdt[GD_TSS0 >> 3].sd_s = 0;

    /* Load the TSS selector (like other segment selectors, the bottom three
     * bits are special; we leave them 0). */
    ltr(GD_TSS0);

    /* Load the IDT. */
    lidt(&idt_pd);
}

void print_trapframe(struct trapframe *tf)
{
    cprintf("TRAP frame at %p\n", tf);
    print_regs(&tf->tf_regs);
    cprintf("  es   0x----%04x\n", tf->tf_es);
    cprintf("  ds   0x----%04x\n", tf->tf_ds);
    cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
    /* If this trap was a page fault that just happened (so %cr2 is meaningful),
     * print the faulting linear address. */
    if (tf == last_tf && tf->tf_trapno == T_PGFLT)
        cprintf("  cr2  0x%08x\n", rcr2());
    cprintf("  err  0x%08x", tf->tf_err);
    /* For page faults, print decoded fault error code:
     * U/K=fault occurred in user/kernel mode
     * W/R=a write/read caused the fault
     * PR=a protection violation caused the fault (NP=page not present). */
    if (tf->tf_trapno == T_PGFLT)
        cprintf(" [%s, %s, %s]\n",
            tf->tf_err & 4 ? "user" : "kernel",
            tf->tf_err & 2 ? "write" : "read",
            tf->tf_err & 1 ? "protection" : "not-present");
    else
        cprintf("\n");
    cprintf("  eip  0x%08x\n", tf->tf_eip);
    cprintf("  cs   0x----%04x\n", tf->tf_cs);
    cprintf("  flag 0x%08x\n", tf->tf_eflags);
    if ((tf->tf_cs & 3) != 0) {
        cprintf("  esp  0x%08x\n", tf->tf_esp);
        cprintf("  ss   0x----%04x\n", tf->tf_ss);
    }
}

void print_regs(struct pushregs *regs)
{
    cprintf("  edi  0x%08x\n", regs->reg_edi);
    cprintf("  esi  0x%08x\n", regs->reg_esi);
    cprintf("  ebp  0x%08x\n", regs->reg_ebp);
    cprintf("  oesp 0x%08x\n", regs->reg_oesp);
    cprintf("  ebx  0x%08x\n", regs->reg_ebx);
    cprintf("  edx  0x%08x\n", regs->reg_edx);
    cprintf("  ecx  0x%08x\n", regs->reg_ecx);
    cprintf("  eax  0x%08x\n", regs->reg_eax);
}

static void trap_dispatch(struct trapframe *tf)
{
    /* *** Handle system call interupts by passing to syscall ***
     * Generic system call: passes system call number in AX,
     * up to five parameters in DX, CX, BX, DI, SI.
     * Interrupt kernel with T_SYSCALL.
    */
    uint32_t a1,a2,a3,a4,a5,callnum,ret;
    switch(tf->tf_trapno) {
        case T_SYSCALL:
            //setup params
            callnum = tf->tf_regs.reg_eax;
            a1 = tf->tf_regs.reg_edx;
            a2 = tf->tf_regs.reg_ecx;
            a3 = tf->tf_regs.reg_ebx;
            a4 = tf->tf_regs.reg_edi;
            a5 = tf->tf_regs.reg_esi;
            //Do systemcall
            ret = syscall(callnum, a1,a2,a3,a4,a5);

            //Set the user env. eax
            tf->tf_regs.reg_eax = ret;
            break;
        case T_PGFLT:
            page_fault_handler(tf);
            break;
        case T_BRKPT:
            breakpoint_handler(tf);
            break;
        default:
            /* Unexpected trap: The user process or the kernel has a bug. */
            print_trapframe(tf);
            if (tf->tf_cs == GD_KT)
                panic("unhandled trap in kernel");
            else {
                env_destroy(curenv);
            }
            break;
    }
}

void trap(struct trapframe *tf)
{
    /* The environment may have set DF and some versions of GCC rely on DF being
     * clear. */
    asm volatile("cld" ::: "cc");

    /* Check that interrupts are disabled.
     * If this assertion fails, DO NOT be tempted to fix it by inserting a "cli"
     * in the interrupt path. */
    assert(!(read_eflags() & FL_IF));

    cprintf("Incoming TRAP frame at %p\n", tf);

    if ((tf->tf_cs & 3) == 3) {
        /* Trapped from user mode. */
        assert(curenv);

        /* Copy trap frame (which is currently on the stack) into
         * 'curenv->env_tf', so that running the environment will restart at the
         * trap point. */
        curenv->env_tf = *tf;
        /* The trapframe on the stack should be ignored from here on. */
        tf = &curenv->env_tf;
    }

    /* Record that tf is the last real trapframe so print_trapframe can print
     * some additional information. */
    last_tf = tf;

    /* Dispatch based on what type of trap occurred */
    trap_dispatch(tf);

    /* Return to the current environment, which should be running. */
    assert(curenv && curenv->env_status == ENV_RUNNING);
    env_run(curenv);
}

void murder_env(env_t *env, uint32_t fault_va) {
    cprintf("[%08x] user fault va %08x ip %08x\n",
            env->env_id, fault_va, env->env_tf.tf_eip);
    print_trapframe(&env->env_tf);
    env_destroy(env);
}

void page_fault_handler(struct trapframe *tf)
{
    uint32_t fault_va, cs;

    /* Read processor's CR2 register to find the faulting address */
    fault_va = rcr2();

    /* Handle kernel-mode page faults. */
    if ((tf->tf_cs & 3) != 3) {
        cprintf("Kernel fault va %08x ip %08x\n", fault_va, tf->tf_eip);
        panic("Exiting due to kernel page fault");
    }

    /* We've already handled kernel-mode exceptions, so if we get here, the page
     * fault happened in user mode. */

    /* If user is requesting an address outside its addressable range, kill it */
    if(fault_va < USTABDATA || fault_va >= UTOP) {
        cprintf("Virtual address outside of user addressable range\n");
        return murder_env(curenv, fault_va);
    }

    /* Check if user env has a VMA for given address */
    vma_t *hit = vma_lookup(curenv, (void *)fault_va, PGSIZE);
    if(!hit) {
        cprintf("Virtual address does not have VMA mapping\n");
        return murder_env(curenv, fault_va);
    }

    /* Only allow dynamic allocation of pre-mapped VMA regions */
    if(hit->type == VMA_UNUSED) {
        cprintf("Virtual address in unused VMA\n");
        return murder_env(curenv, fault_va);
    }

    int permissions = PTE_BIT_PRESENT | PTE_BIT_USER;
    permissions |= (hit->perm & VMA_PERM_WRITE) ? PTE_BIT_RW : 0;
    page_info_t *page = page_alloc(ALLOC_ZERO);
    if(!page) {
        cprintf("Unable to allocate dynamically requested memory\n");
        return murder_env(curenv, fault_va);
    }

    int result = page_insert(curenv->env_pgdir, page, (void *)fault_va, permissions);
    if(result == -E_NO_MEM) {
        cprintf("Unable to allocate page table entry\n");
        return murder_env(curenv, fault_va);
    }

    /* If we've reached this point, the memory fault should have been addressed properly */
}

void breakpoint_handler(struct trapframe *tf) {
    monitor(tf);
}

void trap_sysenter() {    
    /* Prepre all variables*/
    uint32_t p_esp;
    
    /* int num, int check, uint32_t a1, uint32_t a2,
        uint32_t a3, uint32_t a4, uint32_t a5
     * */
    asm volatile(
    "mov (%%ebp), %%eax\n" //read userspace stack pointer
//    "mov %%eax, %%esp\n"   //Make it our stackpointer
//    "mov %%esp, %%ebp\n"   //and base pointer
    "mov %%eax, %0\n"
    : "=m" (p_esp)
    :
    :  "eax"//Do not specify ebp & esp as we do not want them saved (ASM hack)
    );
    
    struct _caller_stack {
        uint32_t basepointer;
        uint32_t syscall_return;
        uint32_t padd[10];
        uint32_t returnaddr;
        uint32_t padd1;
        int num;
        int check;
        uint32_t a1;
        uint32_t a2;
        uint32_t a3; 
        uint32_t a4; 
        uint32_t a5;
    };
    
     struct _caller_stack* caller_stack = (struct _caller_stack*) p_esp;
     
     uint32_t a1,a2,a3,a4,a5,callnum,ret;
     //setup params
    callnum = caller_stack->num;
    a1 = caller_stack->a1;
    a2 = caller_stack->a2;
    a3 = caller_stack->a3;
    a4 = caller_stack->a4;
    a5 = caller_stack->a5;
    //Do systemcall
    ret = syscall(callnum, a1,a2,a3,a4,a5);
    asm volatile("push %eax");
    
    asm volatile("mov %0, %%edx\n":: "r" (caller_stack->syscall_return));
    asm volatile("mov %0, %%ecx\n":: "r" (caller_stack->basepointer));
    asm volatile("pop %eax");
    asm volatile(
    "sysexit\n"
    : "=a" (ret)::
    );
}