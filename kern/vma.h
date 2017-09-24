/* 
 * File:   vma.h
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 *
 * Created on September 23, 2017, 3:00 PM
 */

#ifndef VMA_H
#define VMA_H


#include <kern/env.h>

/*  VMA helpers */
/* Anonymous VMAs are zero-initialized whereas binary VMAs
 * are filled-in from the ELF binary.
 */
#define VMA_ARRAY_SIZE 128
#define VMA_UVA 0xE0000000
/* map above static 4m kernel mapping */
#define VMA_KVA (0xFFFFF000)

#define VMA_INVALID_POINTER (uint8_t)0xFFFF

/* VMA error codes */
enum {
    VMA_ERR_SUCCESS = 0,
};

enum {
    VMA_PERM_READ = 1,
    VMA_PERM_WRITE = 1 << 1,
    VMA_PERM_EXEC = 1 << 2
};
enum {
    VMA_UNUSED,
    VMA_ANON,
    VMA_BINARY,
};

typedef struct vma {
    int type;
    void *va;
    size_t len;
    int perm;
    uint8_t p_adj;
    uint8_t n_adj;
    /* LAB 4: You may add more fields here, if required. */
} vma_t;

typedef struct vma_arr {
    uint8_t occupied;
    uint8_t lowest_va_vma;
//    uint8_t highest_va_vma;
    vma_t vmas[VMA_ARRAY_SIZE];
} vma_arr_t;


/* VMA functions */
/**
 * Asserts if vma is empty.
 * @param vma
 * @return 1 on true, 0 on false
 */
int vma_is_empty(vma_t * vma);
/**
 * Creates a new vma_entry in the vma table
 * @param e THe enviroment to modify
 * @param va The virtual address to map
 * @param len the VA range to map
 * @param perm THe requested VMA permissions
 * @param type The requested type
 * @return index to created vma, -1 on error
 */
int vma_new(env_t *e, void *va, size_t len, int perm, int type);
int vma_unmap(env_t *e, void *va, size_t len);
/**
 * Looks up a vma table which is the first to be found in the range of va to va+len
 * @param e
 * @param va
 * @param len
 * @return 0 on not found, valid pointer on success
 */
vma_t *vma_lookup(env_t *e, void *va, size_t len);
/**
 * Prints vma_list entries in sorted VA order (low to high)
 * @param e
 */
void vma_dump_all(env_t *e);
/**
 * vma_array_init:
 *  - allocates and maps a page (set to zero)
 *  - inits vma_arr_t metadata
 * asserts enviroment vma pointer is zero.
 * @param e target environment
 */
void vma_array_init(env_t *e);

/**
 * vma_array_destroy:
 *  - destroys all current allocated vma's
 *  - free's page
 * if environment pointer is zero, returns without doing anything.
 * @param e
 */
void vma_array_destroy(env_t *e);

/**
 * VMA relative return values
 */
enum {
    VMA_RELATIVE_BEFORE_NADJ = -2,
    VMA_RELATIVE_BEFORE_ADJ = -1,
    VMA_RELATIVE_OVERLAP = 0,
    VMA_RELATIVE_AFTER_ADJ = 1,
    VMA_RELATIVE_AFTER_NADJ = 2,
};
/**
 * checks if vma1 and vma2 overlap or which one comes first and if they are adjacent
 *  Return parameter in perspective of vma1 (eg: comes before means vma1 comes before vma2)
 * @param vma1
 * @param vma2
 * @return A VMA_RELATIVE_* value
 */
int vma_get_relative(vma_t * vma1, vma_t * vma2);

#endif /* VMA_H */

