#include "../inc/env.h"
#include "../inc/x86.h"
#include "../inc/stdio.h"
#include "../inc/assert.h"
#include "cpu.h"
#include "env.h"
#include "sched.h"
#include "pmap.h"
#include "monitor.h"
#include "spinlock.h"
#include "inc/atomic_ops.h"

static uint64_t last_ran = 0;

void sched_halt(void);

static unsigned long long int shared_sched_iter = 0;

/**
 * function handles increment and returns current iterator
 * @return iterator before atomic increment
 */
unsigned long long int shared_sched_get_next_iter() {
    return sync_fetch_and_add(&shared_sched_iter, (long long int)1);
}

/**
 * This is the counter part of shared_sched_yield_env
 * checks if env status is runnable
 *  if true, sets status to running and returns true
 *  if not, leaves status and returns false
 * @param env the enviroment (env_t) object
 * @return true on setting status to ENV_RUNNING from ENV_RUNNABLE
 */
int shared_sched_do_run(env_t * env) {
    return sync_bool_compare_and_swap(&env->env_status, ENV_RUNNABLE, ENV_RUNNING);
}
/**
 * This is the counter part of shared_sched_do_run
 * checks if env status is running
 *  if true, sets status to runnable and returns true
 *  if not, leaves status and returns false
 * @param env the enviroment (env_t) object
 * @return true on setting status to ENV_RUNNABLE from ENV_RUNNING
 */
int shared_sched_yield_env(env_t * env) {
    return sync_bool_compare_and_swap(&env->env_status, ENV_RUNNING, ENV_RUNNABLE);
}
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

    /**************************************
     * Multi-core rules                   *
     **************************************
     * 1. Never touch a env with ENV_RUNNING
     *    Enviroments with ENV_RUNNING are considered locked
     *    The owner is considered the core with the env as curenv
     * 2. Only change env status using shared_sched_do_run(...)
     *    shared_sched_do_run changes state to ENV_RUNNING if it is ENV_RUNNABLE
     * 3. Use global iterator, only via shared_sched_get_next_iter()
     ***************************************/


    env_t *cur = (env_t *)curenv; /* For IDE autocompletion, macro unfolding is b0rked */
    uint64_t since_last_yield = read_tsc() - last_ran;
    last_ran = read_tsc();

    /*
     * If we have a current enviroment
     * It is considered locked for us!
     */
    if(curenv) {
        curenv_i = (cur - envs);
        /* If current env has CPU time left in its slice, run it again */
        if(cur->env_status == ENV_RUNNING && (cur->remain_cpu_time > since_last_yield)) {
            cur->remain_cpu_time -= since_last_yield;
            dprintf("------------> Continue %d at %p (CPU %d) remaining time: %u\n",
                    curenv_i,
                    envs[curenv_i].env_tf.tf_eip,
                    thiscpu->cpu_id,
                    cur->remain_cpu_time
                    );
            env_run(cur);
        } else {
            cur->remain_cpu_time = MAX_TIME_SLICE;
            dprintf("------------> End of Timeslice %d at %p\n", curenv_i, envs[curenv_i].env_tf.tf_eip);
        }
    }

    /* Iterates over envs, starting at curenv's index, wrapping
     * around NENVS to 0, and from there up to curenv's index. */
    for(i = 0; i < NENV; i++) {
        env_i = shared_sched_get_next_iter() % NENV;
        idle = &envs[env_i];

        /* Try to acquire lock (tests if runnable) */
        if(idle && shared_sched_do_run(idle)) {
            /* Free enviroment from our grasp by setting ENV_RUNNING to ENV_RUNNABLE */
            /* Enviroment might be dying though... */
            if (curenv) {
                if (curenv->env_status == ENV_RUNNING)
                    if (shared_sched_yield_env(curenv)==0)
                        panic("Yielding failed! Must never happen!");
            }

            /* Run new curenv */
            dprintf("------------> running %d at %p (CPU %d).\n",
                    curenv_i,
                    envs[curenv_i].env_tf.tf_eip,
                    thiscpu->cpu_id
                    );
            assert(idle->env_tf.tf_eip);
            env_run(idle);
        }
    }

    /* If no eligible envs found above, we can continue running curenv if it is still marked as running */
    if(curenv && curenv->env_status == ENV_RUNNING) {
        dprintf("------------> continue %d at %p (CPU %d).\n",
                    curenv_i,
                    envs[curenv_i].env_tf.tf_eip,
                    thiscpu->cpu_id
                    );
        env_run(curenv);
    }

    /* sched_halt never returns */
    dprintf("CPU %d: Running sched_halt()\n", cpunum());
    sched_halt();

    /* Here to please the compiler, given sched_yield() is marked as non-returning */
    panic("sched_halt() should never return");
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
    dprintf("CPU %d: Checking for runnable enviroments...\n", cpunum());
    for (i = 0; i < NENV; i++) {
        if ((envs[i].env_status == ENV_RUNNABLE ||
             envs[i].env_status == ENV_RUNNING ||
             envs[i].env_status == ENV_WAITING ||
            envs[i].env_status == ENV_DYING))
            break;
    }
    if (i == NENV && cpunum() == 0) {
        dprintf("No runnable environments in the system!\n");
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
//    unlock_kernel();

    dprintf("CPU %d: Halting\n", cpunum());
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

