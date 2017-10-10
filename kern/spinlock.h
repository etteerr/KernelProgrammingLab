#ifndef JOS_INC_SPINLOCK_H
#define JOS_INC_SPINLOCK_H

#include "../inc/types.h"
#include "../inc/assert.h"
#include "../inc/stdio.h"
#include "cpu.h"

/* Comment this to disable spinlock debugging */
#define DEBUG_SPINLOCK

/* Disable big kernel lock
 *
 * LAB 6: Comment out the following macro definition
 *        when you are ready to move to fine-grained locking.
 */
//#define USE_BIG_KERNEL_LOCK 1

/* Mutual exclusion lock. */
struct spinlock {
    unsigned locked;       /* Is the lock held? */

#ifdef DEBUG_SPINLOCK
    /* For debugging: */
    char *name;            /* Name of lock. */
    struct cpuinfo *cpu;   /* The CPU holding the lock. */
    uintptr_t pcs[10];     /* The call stack (an array of program counters) */
    /* that locked the lock. */
#endif
};

void __spin_initlock(struct spinlock *lk, char *name);

void spin_lock(struct spinlock *lk);

void spin_unlock(struct spinlock *lk);

#define spin_initlock(lock)   __spin_initlock(lock, #lock)

extern struct spinlock kernel_lock;

/*
 * 🔒
 * Kernel lock and unlock debug functions
 */
static inline void lock_kernel_(const char *file, const int line) {
#ifdef DEBUG_SPINLOCK
    dprintf("Kernel lock at %s:%d\n", cpunum(), file, line);
#endif
    spin_lock(&kernel_lock);
#ifdef DEBUG_SPINLOCK
    dprintf("Kernel lock done at %s:%d\n", cpunum(), file, line);
#endif
}

static inline void unlock_kernel_(const char *file, const int line) {
#ifdef DEBUG_SPINLOCK
    dprintf("Kernel unlock at %s:%d\n", cpunum(), file, line);
#endif
    spin_unlock(&kernel_lock);
    asm volatile("pause");
#ifdef DEBUG_SPINLOCK
    dprintf("Kernel unlock done at %s:%d\n", cpunum(), file, line);
#endif
}

#define lock(slock) lock_(slock, __FILE__, __LINE__, __func__)
#define unlock(slock) unlock_(slock, __FILE__, __LINE__, __func__)

static inline void lock_(struct spinlock *slock, const char * file, const int line, const char * func) {
#ifdef USE_BIG_KERNEL_LOCK
    lock_kernel_(file, line);
#else /* USE_BIG_KERNEL_LOCK */
#ifdef DEBUG_SPINLOCK
    dprintf("%s lock at %s:%d (%s)\n", cpunum(), slock->name, file, line, func);
#endif
    spin_lock(slock);
#ifdef DEBUG_SPINLOCK
    dprintf("%s lock done at %s:%d (%s)\n", cpunum(), slock->name, file, line, func);
#endif
#endif /* USE_BIG_KERNEL_LOCK */
}   

static inline void unlock_(struct spinlock *slock, const char * file, const int line, const char * func) {
#ifdef USE_BIG_KERNEL_LOCK
    unlock_kernel_(file, line);
#else /* USE_BIG_KERNEL_LOCK */
#ifdef DEBUG_SPINLOCK
    dprintf("%s unlock at %s:%d (%s)\n", cpunum(), slock->name, file, line, func);
#endif
    spin_unlock(slock);
#ifdef DEBUG_SPINLOCK
    dprintf("%s unlock done at %s:%d (%s)\n", cpunum(), slock->name, file, line, func);
#endif
#endif /* USE_BIG_KERNEL_LOCK */
    asm volatile("pause");
}

/*
 * 🔒
 * Kernel lock and unlock debug functions
 */
#ifdef DEBUG_SPINLOCK
/* Activate dprintf statement */
#ifdef DEBUGPRINT
#undef DEBUGPRINT
#endif
#define DEBUGPRINT 1
#endif

extern struct spinlock pagealloc_lock;
extern struct spinlock env_lock;
extern struct spinlock console_lock;

#define lock_pagealloc() lock(&pagealloc_lock)
#define unlock_pagealloc() unlock(&pagealloc_lock)
#define lock_env() lock(&env_lock)
#define unlock_env() unlock(&env_lock)
#define lock_kernel() lock(&kernel_lock)
#define unlock_kernel() unlock(&kernel_lock)

/* Can't do our fancy debug logging on console lock, it would loop. So, using normal spin_lock(). */
static inline void lock_console(void) { spin_lock(&console_lock); }
static inline void unlock_console(void) { spin_unlock(&console_lock); asm volatile("pause"); }

#ifdef DEBUG_SPINLOCK
static __always_inline void assert_lock_env(void)
{
    assert(env_lock.locked && env_lock.cpu == thiscpu);
}
#else /* DEBUG_SPINLOCK */
static inline void assert_lock_env(void) { }
#endif /* DEBUG_SPINLOCK */

#endif
