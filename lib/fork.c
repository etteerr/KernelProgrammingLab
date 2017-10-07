/* implement fork from user space */

#include <inc/string.h>
#include <inc/lib.h>

envid_t fork(void)
{
    /* LAB 5: Your code here. */
    uint32_t res = sys_fork();
    if (res) 
        return res;
    
    uint32_t envid = sys_getenvid();
    thisenv = &envs[ENVX(envid)];
    return 0;
}
