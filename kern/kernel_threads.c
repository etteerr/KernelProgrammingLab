/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   kernel_threads.c
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 * 
 * Created on October 10, 2017, 11:55 PM
 */

#include "kernel_threads.h"
#include "pmap.h"
#include "inc/atomic_ops.h"
#include "vma.h"
#include "inc/x86.h"

uint32_t kern_get_percpu_stack_pointer() {
    return (uint32_t)&percpu_kstacks[8-cpunum()][KSTKSIZE];
}

int kern_thread_create(void* entry) {
        /* Allocate environment */
    struct env * e = 0;
    if (env_alloc(&e, 0, ENV_TYPE_KERNEL_THREAD)!=0) {
        dprintf(KRED "Failed to allocate new env!\n");
        return -1;
    }

    /* Setup env */
    e->env_type = ENV_TYPE_KERNEL_THREAD;

    /* Switch to user environment page directory */
    lcr3(PADDR(e->env_pgdir));

    /* set start */
    e->env_tf.tf_eip = (uint32_t)_kernel_thread_start;
    e->env_tf.tf_esp = (uint32_t)KERNEL_THREAD_STACK_TOP;
    e->env_tf.tf_regs.reg_edx = (uint32_t)entry;
    
    /* Map some stack region */
    vma_new(e, (void*)KERNEL_THREAD_STACK_TOP-0x08000000, 0x08000000, VMA_PERM_WRITE | VMA_PERM_READ, VMA_ANON);
    
    /* page alloc stack */
    page_info_t *pp = page_alloc(ALLOC_ZERO);
    if (!pp)
        panic("Page alloc failed!");
    
    /* Page insert */
    page_insert(e->env_pgdir, pp,(void*) KERNEL_THREAD_STACK_TOP-PGSIZE, PTE_BIT_RW);
    
    /* Now its runnable, mark it as such */
    if (sync_bool_compare_and_swap(&e->env_status, ENV_NOT_RUNNABLE, ENV_RUNNABLE) == 0)
        panic("Set runnable failed!");

    return 0;
}


void test_thread(env_t * tf) {
    /* ESP is now our TF struct past EIP */
    cprintf("Hello, its me! tf:%p\n", tf);
    
    kern_thread_yield(tf);
    
    cprintf("Hello, its me again! tf:%p\n", tf);
    
    /* Never ever return! */
    int j = 0;
    while(j<100) {
        for(int i=0; i<100; i++) {
            asm volatile("nop");
        }
        kern_thread_yield(tf);
        j++;
    }
}