#ifndef JOS_INC_SPINLOCK_H
#define JOS_INC_SPINLOCK_H

#include <inc/types.h>

/* Comment this to disable spinlock debugging */
#define DEBUG_SPINLOCK

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
#ifdef DEBUG_SPINLOCK
#define lock_kernel() lock_kernel_(__FILE__, __LINE__)
#define unlock_kernel() unlock_kernel_(__FILE__, __LINE__)
#endif

#ifdef lock_kernel
static inline void lock_kernel_(const char * file, const int line) {
    cprintf("[kern lock] Kernel lock at %s:%d\n", file, line);
    spin_lock(&kernel_lock);
}
#else
static inline void lock_kernel(void)
{
    spin_lock(&kernel_lock);
}
#endif

#ifdef unlock_kernel
static inline void unlock_kernel_(const char * file, const int line)
{
    cprintf("[kern lock] Kernel unlock at %s:%d\n", file, line);
    spin_unlock(&kernel_lock);
    asm volatile("pause");
}
#else
static inline void unlock_kernel(void)
{
    spin_unlock(&kernel_lock);

    /*
     * Normally we wouldn't need to do this, but QEMU only runs one CPU at a
     * time and has a long time-slice.  Without the pause, this CPU is likely to
     * reacquire the lock before another CPU has even been given a chance to
     * acquire it.
     */
    asm volatile("pause");
}
#endif

#endif
