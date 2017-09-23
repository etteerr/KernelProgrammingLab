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
#define VMA_KVA (0xF0000000 + (4<<20))

#define VMA_INVALID_POINTER 0xFFFF

/* VMA error codes */
enum {
    VMA_ERR_SUCCESS = 0,
};

enum {
    VMA_PERM_WRITE = 1,
    // 1 << 2 for next
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
    uint8_t highest_va_vma;
    vma_t vmas[VMA_ARRAY_SIZE];
} vma_arr_t;


/* VMA functions */
int vma_new(env_t *e, void *va, size_t len, int perm);
int vma_unmap(env_t *e, void *va, size_t len);
vma_t *vma_lookup(env_t *e, void *va);
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

#endif /* VMA_H */

