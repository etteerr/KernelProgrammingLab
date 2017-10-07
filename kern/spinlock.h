#ifndef JOS_INC_SPINLOCK_H
#define JOS_INC_SPINLOCK_H

#include <inc/types.h>
#include <inc/assert.h>
#include <kern/cpu.h>

/* Comment this to disable spinlock debugging */
#define DEBUG_SPINLOCK
/* Disable big kernel lock
 *
 * LAB 6: Comment out the following macro definition
 *        when you are ready to move to fine-grained locking.
 */
#define USE_BIG_KERNEL_LOCK 1

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

#ifdef USE_BIG_KERNEL_LOCK

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

static inline void lock_pagealloc(void) { }
static inline void unlock_pagealloc(void) { }
static inline void lock_env(void) { }
static inline void unlock_env(void) { }
static inline void lock_console(void) { }
static inline void unlock_console(void) { }

static inline void assert_lock_env(void) { }

#else /* USE_BIG_KERNEL_LOCK */

/*
 * 🔒
 * Kernel lock and unlock debug functions
 */
#ifdef DEBUG_SPINLOCK
#define lock_pagealloc() _lock_pagealloc(__FILE__, __LINE__)
#define unlock_pagealloc() _unlock_pagealloc(__FILE__, __LINE__)
#define lock_env() _lock_env(__FILE__, __LINE__)
#define unlock_env() _unlock_env(__FILE__, __LINE__)
#define lock_console() _lock_console(__FILE__, __LINE__)
#define unlock_console() _unlock_console(__FILE__, __LINE__)
#define lock_kernel() _lock_kernel(__FILE__, __LINE__)
#define unlock_kernel() _unlock_kernel(__FILE__, __LINE__)

/* Activate dprintf statement */
#ifdef DEBUGPRINT
#undef DEBUGPRINT
#endif
#define DEBUGPRINT 1

/* Copy paste code */
extern struct spinlock pagealloc_lock;
extern struct spinlock env_lock;
extern struct spinlock console_lock;

/* Copy paste & edit with print code*/
static inline void _lock_pagealloc(const char * file, const int line) { dprintf("lock at %s:%d\n", file, line); spin_lock(&pagealloc_lock); }
static inline void _unlock_pagealloc(const char * file, const int line) { dprintf("unlock at %s:%d\n", file, line); spin_unlock(&pagealloc_lock); asm volatile("pause"); }
static inline void _lock_env(const char * file, const int line) { dprintf("lock at %s:%d\n", file, line); spin_lock(&env_lock); }
static inline void _unlock_env(const char * file, const int line) { dprintf("unlock at %s:%d\n", file, line); spin_unlock(&env_lock); asm volatile("pause"); }
static inline void _lock_console(const char * file, const int line) { spin_lock(&console_lock); }
static inline void _unlock_console(const char * file, const int line) { spin_unlock(&console_lock); asm volatile("pause"); }
static inline void _lock_kernel(const char * file, const int line) { }
static inline void _unlock_kernel(const char * file, const int line) { }

/* End of me debug, original code follows */
#else

extern struct spinlock pagealloc_lock;
extern struct spinlock env_lock;
extern struct spinlock console_lock;

static inline void lock_pagealloc(void) { spin_lock(&pagealloc_lock); }
static inline void unlock_pagealloc(void) { spin_unlock(&pagealloc_lock); asm volatile("pause"); }
static inline void lock_env(void) { spin_lock(&env_lock); }
static inline void unlock_env(void) { spin_unlock(&env_lock); asm volatile("pause"); }
static inline void lock_console(void) { spin_lock(&console_lock); }
static inline void unlock_console(void) { spin_unlock(&console_lock); asm volatile("pause"); }
static inline void lock_kernel(void) { }
static inline void unlock_kernel(void) { }

#endif

#ifdef DEBUG_SPINLOCK
static __always_inline void assert_lock_env(void)
{
    assert(env_lock.locked && env_lock.cpu == thiscpu);
}
#else /* DEBUG_SPINLOCK */
static inline void assert_lock_env(void) { }
#endif /* DEBUG_SPINLOCK */
#endif /* USE_BIG_KERNEL_LOCK */

#endif
