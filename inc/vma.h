//
// Created by Tom on 25/09/2017.
//

#ifndef KERNELPROGRAMMINGLAB_VMA_H
#define KERNELPROGRAMMINGLAB_VMA_H

/*  VMA helpers */
/* Anonymous VMAs are zero-initialized whereas binary VMAs
 * are filled-in from the ELF binary.
 */
#define VMA_ARRAY_SIZE 128
#define VMA_UVA 0xE0000000
/* map above static 4m kernel mapping */
//#define VMA_KVA (0xFFFFF000)

#define VMA_INVALID_POINTER (uint8_t)0xFFFF
#define VMA_INVALID_INDEX (uint8_t)0xFFFF

#define VMA_FLAG_POPULATE 0x1

/* VMA error codes */
enum {
    VMA_ERR_SUCCESS = 0,
    VMA_ERR_VMA_EXISTS = -1,
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

typedef union {
    uint8_t reg;
    struct {
        unsigned COW:1;
        unsigned :7;
    }bit;
} vma_flags_t;

typedef struct vma {
    int type;
    void *va;
    size_t len;
    int perm;
    uint8_t p_adj;
    uint8_t n_adj;
    vma_flags_t flags;
    void * backed_addr;
    uint32_t backsize;
    /* LAB 4: You may add more fields here, if required. */
} vma_t;

typedef struct vma_arr {
    uint8_t occupied;
    uint8_t lowest_va_vma;
//    uint8_t highest_va_vma;
    vma_t vmas[VMA_ARRAY_SIZE];
} vma_arr_t;


#endif //KERNELPROGRAMMINGLAB_VMA_H
