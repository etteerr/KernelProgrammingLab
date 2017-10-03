#include "../inc/env.h"
#include "../inc/mmu.h"
#include "../inc/x86.h"
#include "../inc/trap.h"
#include "../inc/error.h"
#include "../inc/stdio.h"
#include "../inc/string.h"
#include "../inc/types.h"
#include "../inc/assert.h"
#include "../inc/memlayout.h"

#include "cpu.h"
#include "env.h"
#include "vma.h"
#include "pmap.h"
#include "trap.h"
#include "sched.h"
#include "kclock.h"
#include "picirq.h"
#include "console.h"
#include "monitor.h"
#include "syscall.h"
#include "spinlock.h"
#include "kdebug.h"

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
    if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16)
        return "Hardware Interrupt";
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

    SETGATE(idt[IRQ_OFFSET + IRQ_TIMER], 0, GD_KT, (uint32_t)&trap_irq_timer, 0);
    SETGATE(idt[IRQ_OFFSET + IRQ_KBD], 0, GD_KT, (uint32_t)&trap_irq_kbd, 0);
    SETGATE(idt[IRQ_OFFSET + 3], 0, GD_KT, (uint32_t)&trap_irq_3, 0);
    SETGATE(idt[IRQ_OFFSET + IRQ_SERIAL], 0, GD_KT, (uint32_t)&trap_irq_serial, 0);
    SETGATE(idt[IRQ_OFFSET + 5], 0, GD_KT, (uint32_t)&trap_irq_5, 0);
    SETGATE(idt[IRQ_OFFSET + 6], 0, GD_KT, (uint32_t)&trap_irq_6, 0);
    SETGATE(idt[IRQ_OFFSET + IRQ_SPURIOUS], 0, GD_KT, (uint32_t)&trap_irq_spur, 0);
    SETGATE(idt[IRQ_OFFSET + 8], 0, GD_KT, (uint32_t)&trap_irq_8, 0);
    SETGATE(idt[IRQ_OFFSET + 9], 0, GD_KT, (uint32_t)&trap_irq_9, 0);
    SETGATE(idt[IRQ_OFFSET + 10], 0, GD_KT, (uint32_t)&trap_irq_10, 0);
    SETGATE(idt[IRQ_OFFSET + 11], 0, GD_KT, (uint32_t)&trap_irq_11, 0);
    SETGATE(idt[IRQ_OFFSET + 12], 0, GD_KT, (uint32_t)&trap_irq_12, 0);
    SETGATE(idt[IRQ_OFFSET + 13], 0, GD_KT, (uint32_t)&trap_irq_13, 0);
    SETGATE(idt[IRQ_OFFSET + IRQ_IDE], 0, GD_KT, (uint32_t)&trap_irq_ide, 0);
    SETGATE(idt[IRQ_OFFSET + 15], 0, GD_KT, (uint32_t)&trap_irq_15, 0);
    SETGATE(idt[IRQ_OFFSET + IRQ_ERROR], 0, GD_KT, (uint32_t)&trap_irq_err, 0);

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
    cprintf("TRAP frame at %p from CPU %d\n", tf, cpunum());
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
        case IRQ_OFFSET + IRQ_SPURIOUS:
            /*
             * Handle spurious interrupts
             * The hardware sometimes raises these because of noise on the
             * IRQ line or other reasons. We don't care.
            */
            cprintf("Spurious interrupt on irq 7\n");
            print_trapframe(tf);
            return;
        /*
         * Handle clock interrupts. Don't forget to acknowledge the interrupt using
         * lapic_eoi() before calling the scheduler!
         * LAB 5: Your code here.
         */
        case IRQ_OFFSET + IRQ_TIMER:
            lapic_eoi();
            sched_yield();
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

    /* Halt the CPU if some other CPU has called panic(). */
    extern char *panicstr;
    if (panicstr)
        asm volatile("hlt");

    /* Re-acqurie the big kernel lock if we were halted in sched_yield(). */
    if (xchg(&thiscpu->cpu_status, CPU_STARTED) == CPU_HALTED)
        lock_kernel();

    /* Check that interrupts are disabled.
     * If this assertion fails, DO NOT be tempted to fix it by inserting a "cli"
     * in the interrupt path. */
    assert(!(read_eflags() & FL_IF));

    cprintf("Incoming TRAP frame at %p\n", tf);
    
    if ((tf->tf_cs & 3) == 3) {
        /* Trapped from user mode. */
        /* Acquire the big kernel lock before doing any serious kernel work.
         * LAB 5: Your code here. */

        assert(curenv);

        /* Garbage collect if current environment is a zombie. */
        if (curenv->env_status == ENV_DYING) {
            env_free(curenv);
            curenv = NULL;
            sched_yield();
        }

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

    /* If we made it to this point, then no other environment was scheduled, so
     * we should return to the current environment if doing so makes sense. */
    if (curenv && curenv->env_status == ENV_RUNNING)
        env_run(curenv);
    else
        sched_yield();
}

void murder_env(env_t *env, uint32_t fault_va) {
    bool is_kernel = ((env->env_tf.tf_cs & 3) != 3);
    /* Handle kernel-mode page faults. */
    if (is_kernel) {
        cprintf("Kernel fault va %08x ip %08x\n", fault_va, env->env_tf.tf_eip);
        panic("Exiting due to kernel page fault");
    }

    cprintf("[%08x] user fault va %08x ip %08x\n",
            env->env_id, fault_va, env->env_tf.tf_eip);
    print_trapframe(&env->env_tf);
    env_destroy(env);
}

int trap_handle_cow(uint32_t fault_va){
    
//    vma_t* hit, pte_t** pte, pte_t pte_original, page_info_t* new_page;
    vma_t * hit = vma_lookup(curenv, (void*) fault_va, 0);
    
    //store original pte
    pte_t pte_original = *pgdir_walk(curenv->env_pgdir, (void*)fault_va, 0);
    
    if (!hit || ! pte_original) {
        cprintf("[COW] Invalid pte (%p) or vma (", pte_original);
        if (hit)
            vma_dump(hit);
        cprintf(")!\n");
        return -1;
    }
    
    if (hit->flags.bit.COW && !(pte_original & PDE_BIT_HUGE)) {
        cprintf("[COW] va %p original_pte %p\n", hit->va, pte_original);
        /* Reset pte entry */
        *pgdir_walk(curenv->env_pgdir, (void*)fault_va, 0) = 0;
        
        /* Extract original page address, copy this to our new page */
        page_info_t *cow_page = pa2page(PTE_GET_PHYS_ADDRESS(pte_original));
        page_info_t *new_page = page_alloc(ALLOC_ZERO);
        
        if (!new_page) {
                cprintf("[COW] Page allocation failed!\n");
                return -1;
            }
        /* Insert page with original permissions + write */
        
        if (page_insert(curenv->env_pgdir, new_page, (void*)(fault_va & 0xFFFFF000), 
                (pte_original & 0x1F) | PTE_BIT_RW))
        {
            cprintf("[COW] Page insertion failed!\n");
            page_decref(new_page);
            return -1;
        }
        
        void *src = (void*) page2kva(cow_page);
        void *dst = (void*) page2kva(new_page);
        memcpy(src,dst, PGSIZE);
        
        /* Make a final assertion, cow should only trigger on writes */
        assert(hit->perm & PTE_BIT_RW);
        assert(hit->perm & VMA_PERM_WRITE);
        
        /* Decrease page ref counter */
        page_decref(cow_page);
        
        return 0;
    }else
        if (hit->flags.bit.COW) {
            /* Hit on huge page */
            cprintf("[COW] [Huge] va %p original_pte %p\n", hit->va, pte_original);
            /* Reset pte entry */
            *pgdir_walk(curenv->env_pgdir, (void*)fault_va, 0) = 0;
            
            /* Make some assertions */
            assert(pte_original & PDE_BIT_HUGE); //Should always be true due to if statement
            assert(hit->perm & VMA_PERM_WRITE);
            assert(pte_original * PDE_BIT_RW);
            
            /* Now create 4M entry and handle cow */
            page_info_t *new_page = page_alloc(ALLOC_HUGE | ALLOC_ZERO);
            
            if (!new_page) {
                cprintf("[COW] [HUGE] Page allocation failed!\n");
                return -1;
            }
            
            page_info_t *cow_page = pa2page(PDE_GET_ADDRESS(pte_original));
            
            if (page_insert(curenv->env_pgdir, new_page, (void*) page2pa(new_page), 
                    PDE_BIT_PRESENT | PDE_BIT_RW | PDE_BIT_HUGE | PDE_BIT_USER
                    ))
            {
                cprintf("[COW] [HUGE] Page insertion failed!\n");
                page_decref(new_page);
                return -1;
            }
            
            void *src = (void*) page2kva(cow_page);
            void *dst = (void*) page2kva(new_page);
            
            memcpy(src, dst, PGSIZE*1024);
            
            /* Decrease page ref counter */
            page_decref(cow_page);
            
            return 0;
        }
    
    return -1;
}

int trap_handle_backed_memory(uint32_t fault_va){
    vma_t * vma = vma_lookup(curenv, (void*)fault_va, 0);
    if (vma->backed_addr && vma->len) {
        /* Our vma is backed! */
        cprintf("[filebacked memory] Backing memory address %p\n", fault_va);
        
        page_info_t * page = page_alloc(ALLOC_ZERO);
        if (!page) {
            cprintf("[filebacked memory] Page allocation failed!\n");
            return -1;
        }
        
        int perm = PTE_BIT_USER | PTE_BIT_PRESENT;
        perm |= vma->perm & VMA_PERM_WRITE ? PTE_BIT_RW : 0;
        
        if (page_insert(curenv->env_pgdir, page, (void*)(fault_va & 0xFFFFF000), perm)) {
            cprintf("[filebacked memory] Page insertion failed!\n");
            page_decref(page);
            return -1;
        }
        /* Set the inter vma offset of the file backing
         * Take into account the intitial offset provided as non-allignment of the vma
         * example:
         *  vma_new(addr=0x0200020)
         *  start addres will be 0x0200020
         *  offset will be 0x20
         *  as filebacking starts from requested vma addr
         */
        uint32_t page_va = fault_va & 0xFFFFF000;
        
        /* Set copy parameters */
        uint32_t src = (uint32_t)vma->backed_addr;
        uint32_t dst = (uint32_t)page2kva(page);
        uint32_t cpy_len = PGSIZE;
        
        /* determine if dst_base is within page backed offset (null zone) */
        /* page alligned -> [null zone | requested vma | null zone ] */
        /* This is the case when it is the first page in the vma */
        if ((page_va == (uint32_t)vma->va) && vma->backed_start_offset) {
            /* We have a backed offset and this is the first page */
            dst += vma->backed_start_offset;
            assert(vma->backed_start_offset <= PGSIZE);
            cpy_len -= vma->backed_start_offset;
        }
        
        /* Check if we do not exceed our backed length */
        uint32_t ivma_offset = fault_va - (uint32_t)vma->va;
        if (ivma_offset > vma->backsize) {
            uint32_t overflow = ivma_offset - vma->backsize;
            if (overflow >= cpy_len)
                return 0; //nothing to copy, we are in null region
            
            cpy_len -= overflow;
        }
        
        /* Set correct src and dst offset in pages */
        uint32_t pages_offset = fault_va - (uint32_t)vma->va;
        pages_offset /= PGSIZE;
        dst += pages_offset * PGSIZE;
        src += pages_offset * PGSIZE;
        
        /* Do memcpy */
        memcpy((void*) dst, (void*) src, cpy_len);
        
        return 0;
    }
    
    return -1;
}

/**
 * Determine the type of page fault
 * @param fault_va
 * @param is_kernel
 * @return 
 */
int determine_pagefault(uint32_t fault_va, bool is_kernel){
    env_t * e = curenv;
    
    /* Determine if this is the kernel */
    if (is_kernel)
        return PAGEFAULT_TYPE_KERNEL;
    
    /* Determine if it is user accessable*/
    if(!is_kernel && (fault_va < USTABDATA || fault_va >= UTOP))
        return PAGEFAULT_TYPE_OUTSIDE_USER_RANGE;
    
    /* Determine if the vma exists and is used */
    vma_t * vma = vma_lookup(e, (void*)fault_va, 0);
    if (!vma)
        return PAGEFAULT_TYPE_NO_VMA;
    
    if (vma->type == VMA_UNUSED)
        return PAGEFAULT_TYPE_UNUSED_VMA;
    
    /* Determine if the pte entry and vma specify a condition */
    pte_t * pte = pgdir_walk(e->env_pgdir, (void*)fault_va, 0);
    if (pte && (*pte & PTE_BIT_PRESENT)) {
        /* Condition page present */
        if (!(*pte & PTE_BIT_RW) && vma->perm & VMA_PERM_WRITE)
            return PAGEFAULT_TYPE_COW;
        else
            return PAGEFAULT_TYPE_INVALID_PERMISSION;
    }else{
        /* Condition page not present */
        if (!pte && vma->backed_addr)
            return PAGEFAULT_TYPE_FILEBACKED;
        
        return PAGEFAULT_TYPE_NO_PTE;
    }
    
    return PAGEFAULT_TYPE_NONE;
}

void handle_pf_pte(uint32_t fault_va){
    page_info_t * pp = page_alloc(ALLOC_ZERO);
    if (!pp) {
        cprintf("[PAGEFAULT] Dynamic allocation for %p failed.\n", fault_va);
        murder_env(curenv, fault_va);
    }
    vma_t * vma = vma_lookup(curenv, (void*)fault_va, 0);
    int perm = PTE_BIT_PRESENT | PTE_BIT_USER;
    perm |= vma->perm & VMA_PERM_WRITE ? PTE_BIT_RW : 0;
    int res = page_insert(curenv->env_pgdir, pp, (void*)(fault_va & 0xFFFFF000), perm);
    if (res) {
        cprintf("[PAGEFAULT] Failed to map page table: page allocation failed\n");
        page_decref(pp);
        murder_env(curenv, fault_va);
    }
}

void page_fault_handler(struct trapframe *tf)
{
    uint32_t fault_va, cs;
    bool is_kernel = ((tf->tf_cs & 3) != 3);

    /* Read processor's CR2 register to find the faulting address */
    fault_va = rcr2();

    if(!curenv) {
        panic("No curenv set");
    }

    /* Determine type of pagefault */
    int pf_type = determine_pagefault(fault_va, is_kernel);
    
    /* Handle all pagefaults */
    
    switch (pf_type) {
        case PAGEFAULT_TYPE_KERNEL:
            murder_env(curenv, fault_va);
            break;
        case PAGEFAULT_TYPE_OUTSIDE_USER_RANGE:
            cprintf("[PAGEFAULT] Page outside user accessable range.\n");
            murder_env(curenv, fault_va);
            break;
        case PAGEFAULT_TYPE_INVALID_PERMISSION:
            cprintf("[PAGEFAULT] Page permissions insufficient.\n");
            murder_env(curenv, fault_va);
            break;
        case PAGEFAULT_TYPE_NO_PTE:
            cprintf("[PAGEFAULT] No page entry exists at %p.\n", fault_va);
            handle_pf_pte(fault_va);
            break;
        case PAGEFAULT_TYPE_NO_VMA:
            cprintf("[PAGEFAULT] Va outside VMA ranges.\n");
            murder_env(curenv, fault_va);
            break;
        case PAGEFAULT_TYPE_UNUSED_VMA:
            cprintf("[PAGEFAULT] VA inside unused VMA range.\n");
            murder_env(curenv, fault_va);
            break;
        case PAGEFAULT_TYPE_COW:
            if (trap_handle_cow(fault_va)) {
                cprintf("[PAGEFAULT] COW failed.\n");
                murder_env(curenv, fault_va);
            }
            break;
        case PAGEFAULT_TYPE_FILEBACKED:
            if (trap_handle_backed_memory(fault_va)) {
                cprintf("[PAGEFAULT] file backing failed.\n");
                murder_env(curenv, fault_va);
            }
            break;
        case PAGEFAULT_TYPE_NONE:
            cprintf("[PAGEFAULT] No pagefault!\n");
        default:
            panic("Unhandled pagefault type %d", pf_type);
    }
        
    
    /* If we've reached this point, the memory fault should have been addressed properly */
    cprintf("Page fault at (%#08x) should be fixed\n", fault_va);
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