/* implement fork from user space */

#include <inc/string.h>
#include <inc/lib.h>

envid_t fork(void)
{
    /* LAB 5: Your code here. */
//    panic("fork not implemented");

    /* TODO: this return is just so we can test user/spin.c, remove after implementing fork() */
    return (envid_t)0;
}
