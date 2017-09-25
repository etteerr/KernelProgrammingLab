/* System call stubs. */

#include "../inc/env.h"
#include "../inc/lib.h"
#include "../inc/vma.h"
#include "../inc/mmu.h"
#include "../inc/types.h"
#include "../inc/syscall.h"
#include "../inc/assert.h"

static inline int32_t syscall(int num, int check, uint32_t a1, uint32_t a2,
        uint32_t a3, uint32_t a4, uint32_t a5)
{
    int32_t ret;
    
    /*
     * Generic system call: pass system call number in AX,
     * up to five parameters in DX, CX, BX, DI, SI.
     * Interrupt kernel with T_SYSCALL.
     *
     * The "volatile" tells the assembler not to optimize
     * this instruction away just because we don't use the
     * return value.
     *
     * The last clause tells the assembler that this can
     * potentially change the condition codes and arbitrary
     * memory locations.
     */
#ifdef BONUS_LAB3
    static uint32_t ebp = 0;
    asm volatile(
    "push %%ebp\n"
    "pop %%eax\n"
    "mov %%eax, %0\n"
    : "=m" (ebp)
    ::);
    
    asm volatile(
    "mov $1, %%EAX\n"
    "cpuid"
    : "=edx" (ret)
    :
    : "%eax", "%ebx", "%ecx"
    );

    /* Check if CPU supports sysenter opcode */
    if (ret & 1 << 11)  {
        asm volatile (
        "lea exitpoint, %%eax\n"
        "push %%eax\n"
        "push %%ebp\n"
        "mov %%esp, %%ebp\n"
        "sysenter\n"
        "exitpoint:\n"
        "mov %%esp, %%ebp\n"
        : "=a" (ret)
        : 
        : "cc", "memory"
        );
        
        asm volatile (
        "push %%eax\n"
        "mov %0, %%eax\n"
        "mov %%eax, %%ebp\n"
        "pop %%eax\n"
        ::"r" (ebp):);
        
    }else{
        asm volatile("int %1\n"
            : "=a" (ret)
            : "i" (T_SYSCALL),
              "a" (num),
              "d" (a1),
              "c" (a2),
              "b" (a3),
              "D" (a4),
              "S" (a5)
            : "cc", "memory");
    }
#else
    asm volatile("int %1\n"
            : "=a" (ret)
            : "i" (T_SYSCALL),
              "a" (num),
              "d" (a1),
              "c" (a2),
              "b" (a3),
              "D" (a4),
              "S" (a5)
            : "cc", "memory");
#endif

    if(check && ret > 0)
        panic("syscall %d returned %d (> 0)", num, ret);

    return ret;
}

void sys_cputs(const char *s, size_t len)
{
    syscall(SYS_cputs, 0, (uint32_t)s, len, 0, 0, 0);
}

int sys_cgetc(void)
{
    return syscall(SYS_cgetc, 0, 0, 0, 0, 0, 0);
}

int sys_env_destroy(envid_t envid)
{
    return syscall(SYS_env_destroy, 1, envid, 0, 0, 0, 0);
}

envid_t sys_getenvid(void)
{
     return syscall(SYS_getenvid, 0, 0, 0, 0, 0, 0);
}

void *sys_vma_create(size_t size, int perm, int flags)
{
    vma_t *vma = (vma_t *)syscall(SYS_vma_create, 0, size, perm, flags, 0, 0);
    void *addr;
    register char access;
    if(flags & VMA_FLAG_POPULATE) {
        /* Trigger pagefaults to alloc pages */
        for(addr = vma->va; addr < vma->va + size; size += PGSIZE) {
            access = *((char *) addr);
        }
    }

    return vma;
}

int sys_vma_destroy(void *va, size_t size)
{
    return syscall(SYS_vma_destroy, 0, (uint32_t) va, size, 0, 0, 0);
}
