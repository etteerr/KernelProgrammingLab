/* 
 * File:   vma.h
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 *
 * Created on September 23, 2017, 3:00 PM
 */

#ifndef VMA_H
#define VMA_H

/* VMA functions */



/*  VMA helpers */
/* Anonymous VMAs are zero-initialized whereas binary VMAs
 * are filled-in from the ELF binary.
 */
#define VMA_ARRAY_SIZE 128

enum {
    VMA_PERM_WRITE = 1,
    // 1 << 2 for next
};
enum {
    VMA_UNUSED,
    VMA_ANON,
    VMA_BINARY,
};

struct vma {
    int type;
    void *va;
    size_t len;
    int perm;
    uint8_t p_adj;
    uint8_t n_adj;
    /* LAB 4: You may add more fields here, if required. */
};

struct vma_arr {
    uint8_t occupied;
    uint8_t lowest_va_vma;
    
    vma vmas[VMA_ARRAY_SIZE];
};

#endif /* VMA_H */

