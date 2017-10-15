#include "inc/memlayout.h"
#include "kernel_threads.h"
#include "cpu.h"
.text

.align 4

## KERNEL ENTRY POINT FOR THREADS ##
.globl      _kernel_thread_start
_kernel_thread_start:
    // ESP is +0x34 from *tf
    // EDX contains entry point
    addl $0x8, %esp
    // We are now at esp with %esp
    mov %esp, %eax //save our tf + 68 (0x3c)
    pop %esp
    sub $0x3c, %eax //subtract 64 to get *tf
    //x80_86 calling convention
    pushl %eax //save *tf at the beginning of the stack (never changes)
    pushl %ebp //save original base pointer of the kernel
    mov %esp, %ebp
    //Push arguments
    pushl %eax //push *tf
    mov %edx, %eax
    call *%eax
    //Caller returns! OMG
    //Lets, in this case, say the enviroment has ended
    //Good idea for quick launch and forget threads
    //Call env_destroy, *tf is on the stack
    pop %ebp //store *tf to base pointer
    /* Set kernel stack pointer */
    call   cpunum //only eax used (cpunum: a)
    mov $NCPU, %ebx //ncpu: b
    mov $0x9000, %ecx //kstsize: c
    mov $(KSTACKTOP), %edx //Address of stackbase: d
    sub %eax, %ebx //b: NCPU - cpunum
    imul %ecx, %ebx //b*c = offset from base
    lea (%edx, %ebx, 1), %eax // d + b*1
    mov %eax, %esp //Set kernel sp
    //Now cal env_destroy
    push %ebp
    call env_destroy
    //Now call sched_yield and never come back
    push $0x0 //Calling convention (return address)
    call sched_yield

.globl _kern_thread_yield
_kern_thread_yield:
    //REturn address => edx
    pop %edx
    //env_t * tf => eax
    pop %eax
    //sub $4 to stack (for some reason... its 2:28PM)
    sub $4, %esp
    //save basepointer on stack
    push %ebp
    //Now save stack pointer to ecx
    mov %esp, %ecx
######## NO MORE STACK OPERATIONS ############
    //Save to tf
    mov %eax, 0x1c(%eax) //Save tf to eax 
    mov %edx, 0x18(%eax) //save return to ecx
    mov %ecx, 0x3c(%eax) //save esp to tf esp
    mov $reentry, %ecx
    mov %ecx, 0x30(%eax) //save eip to eip (reentry)
    /* Set kernel stack pointer */
    call   cpunum //only eax used (cpunum: a)
    mov $NCPU, %ebx //ncpu: b
    mov $0x9000, %ecx //kstsize: c
    mov $(KSTACKTOP), %edx //Address of stackbase: d
    sub %eax, %ebx //b: NCPU - cpunum
    imul %ecx, %ebx //b*c = offset from base
    lea (%edx, %ebx, 1), %eax // d + b*1
    mov %eax, %esp //Set kernel sp
    /* Call sched_yield as the kernel stack is initialized */
    push $0x0 //Calling convention (return address)
    call sched_yield
reentry:
    //Now we have been resumed
    // eax contains tf
    // ecx contains return value
    //REstore pointer
    mov 0x3c(%eax), %edx
    mov %edx, %esp
######### STACK OPERATIONS RESTORED ################
    //restore basepointer (bp - 4 => ebp
    pop %ebp
    //push return value on stack
    pushl %ecx
    ret
    
    