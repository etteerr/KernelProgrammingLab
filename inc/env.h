/* See COPYRIGHT for copyright information. */

#ifndef JOS_INC_ENV_H
#define JOS_INC_ENV_H

#include "../inc/trap.h"
#include "../inc/types.h"
#include "../inc/memlayout.h"


typedef int32_t envid_t;
struct vma_arr;
typedef struct vma_arr vma_arr_t;
/*
 * An environment ID 'envid_t' has three parts:
 *
 * +1+---------------21-----------------+--------10--------+
 * |0|          Uniqueifier             |   Environment    |
 * | |                                  |      Index       |
 * +------------------------------------+------------------+
 *                                       \--- ENVX(eid) --/
 *
 * The environment index ENVX(eid) equals the environment's offset in the
 * 'envs[]' array.  The uniqueifier distinguishes environments that were
 * created at different times, but share the same environment index.
 *
 * All real environments are greater than 0 (so the sign bit is zero).
 * envid_ts less than 0 signify errors.  The envid_t == 0 is special, and
 * stands for the current environment.
 */

#define LOG2NENV        10
#define NENV            (1 << LOG2NENV)
#define ENVX(envid)     ((envid) & (NENV - 1))

/* Values of env_status in struct env */
enum {
    ENV_FREE = 0,
    ENV_DYING,
    ENV_RUNNABLE,
    ENV_RUNNING,
    ENV_WAITING,
    ENV_WAITING_SWAP,
    ENV_NOT_RUNNABLE
};

/* Special environment types */
enum env_type {
    ENV_TYPE_USER = 0,
    ENV_TYPE_KERNEL_ENV,
    ENV_TYPE_KERNEL_THREAD,
};

typedef struct env {
    struct trapframe env_tf;    /* Saved registers */
    struct env *env_link;       /* Next free env */
    envid_t env_id;             /* Unique environment identifier */
    envid_t env_parent_id;      /* env_id of this env's parent */
    enum env_type env_type;     /* Indicates special system environments */
    unsigned env_status;        /* Status of the environment */
    uint32_t env_runs;          /* Number of times environment has run */
    int env_cpunum;             /* The CPU that the env is running on */
    uint32_t remain_cpu_time;
    envid_t waiting_for;

    /* Address space */
    pde_t *env_pgdir;           /* Kernel virtual address of page dir */
    vma_arr_t *vma_list;
} env_t;



#endif /* !JOS_INC_ENV_H */
