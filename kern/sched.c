#include "../inc/env.h"
#include "../inc/x86.h"
#include "../inc/stdio.h"
#include "../inc/assert.h"
#include "cpu.h"
#include "env.h"
#include "pmap.h"
#include "monitor.h"
#include "spinlock.h"

void sched_halt(void);

/*
 * Choose a user environment to run and run it.
 */
void sched_yield(void)
{
    struct env *idle;
    int curenv_i = 0, env_i, i;

    /*
     * Implement simple round-robin scheduling.
     *
     * Search through 'envs' for an ENV_RUNNABLE environment in
     * circular fashion starting just after the env this CPU was
     * last running.  Switch to the first such environment found.
     *
     * If no envs are runnable, but the environment previously
     * running on this CPU is still ENV_RUNNING, it's okay to
     * choose that environment.
     *
     * Never choose an environment that's currently running (on
     * another CPU, if we had, ie., env_status == ENV_RUNNING). 
     * If there are
     * no runnable environments, simply drop through to the code
     * below to halt the cpu.
     */

    lock_kernel();

    env_t *cur = (env_t *)curenv;

    if(curenv) {
        curenv_i = (cur - envs);
    }

    /* Iterates over envs, starting at curenv's index, wrapping
     * around NENVS to 0, and from there up to curenv's index. */
    for(i = 0; i < NENV; i++) {
        env_i = (curenv_i + i) % NENV;
        idle = &envs[env_i];

        if(idle && idle->env_status == ENV_RUNNABLE) {
            unlock_kernel();
            return env_run(idle);
        }
    }

    /* If no eligible envs found above, we can continue running curenv if it is still marked as running */
    if(curenv && curenv->env_status == ENV_RUNNING) {
        unlock_kernel();
        return env_run(curenv);
    }

    /* sched_halt never returns */
    sched_halt();
}

/*
 * Halt this CPU when there is nothing to do. Wait until the timer interrupt
 * wakes it up. This function never returns.
 */
void sched_halt(void)
{
    int i;

    /* For debugging and testing purposes, if there are no runnable
     * environments in the system, then drop into the kernel monitor. */
    for (i = 0; i < NENV; i++) {
        if ((envs[i].env_status == ENV_RUNNABLE ||
             envs[i].env_status == ENV_RUNNING ||
             envs[i].env_status == ENV_DYING))
            break;
    }
    if (i == NENV) {
        cprintf("No runnable environments in the system!\n");
        while (1)
            monitor(NULL);
    }

    /* Mark that no environment is running on this CPU */
    curenv = NULL;
    lcr3(PADDR(kern_pgdir));

    /* Mark that this CPU is in the HALT state, so that when
     * timer interupts come in, we know we should re-acquire the
     * big kernel lock */
    xchg(&thiscpu->cpu_status, CPU_HALTED);

    /* Release the big kernel lock as if we were "leaving" the kernel */
    unlock_kernel();

    /* Reset stack pointer, enable interrupts and then halt. */
    asm volatile (
        "movl $0, %%ebp\n"
        "movl %0, %%esp\n"
        "pushl $0\n"
        "pushl $0\n"
        "sti\n"
        "hlt\n"
    : : "a" (thiscpu->cpu_ts.ts_esp0));
}

