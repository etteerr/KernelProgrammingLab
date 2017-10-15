/* 
 * File:   atomic_ops.h
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 *
 * Created on October 8, 2017, 3:26 PM
 */

#ifndef ATOMIC_OPS_H
#define ATOMIC_OPS_H

/* atomic add add_val and return the original value */
#define sync_fetch_and_add(p_val, add_val) __sync_fetch_and_add(p_val, add_val)
/* atomic sub sub_val and return the original value */
#define sync_fetch_and_sub(p_val, sub_val) __sync_fetch_and_add(p_val, sub_val)
/* atomic add add_val and return the new value */
#define sync_add_and_fetch(p_val, add_val) __sync_add_and_fetch(p_val, add_val)
/* atomic sub sub_val and return the new value */
#define sync_sub_and_fetch(p_val, add_val) __sync_sub_and_fetch(p_val, add_val)

/*
 * performs an atomic compare and swap. 
 * That is, if the current value of *ptr is oldval, 
 * then write newval into *ptr.
 * Returns value before swap
 */
#define sync_val_compare_and_swap(p_val, if_value, set_value) \
            __sync_val_compare_and_swap(p_val, if_value, set_value)

/*
 * performs an atomic compare and swap. 
 * That is, if the current value of *ptr is oldval, 
 * then write newval into *ptr.
 * Returns 1 on swap, 0 if no swap occured
 */
#define sync_bool_compare_and_swap(p_val, if_value, set_value) \
            __sync_bool_compare_and_swap(p_val, if_value, set_value)

/* Imlpies a full synchonization barrier */
#define sync_synchronize() __sync_synchronize()
#define sync_barrier() sync_synchronize()

#endif /* ATOMIC_OPS_H */
