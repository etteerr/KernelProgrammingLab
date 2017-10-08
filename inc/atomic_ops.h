/* 
 * File:   atomic_ops.h
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 *
 * Created on October 8, 2017, 3:26 PM
 */

#ifndef ATOMIC_OPS_H
#define ATOMIC_OPS_H

#define sync_fetch_and_add(p_val, add_val) __sync_fetch_and_add(p_val, add_val)
#define sync_fetch_and_sub(p_val, sub_val) __sync_fetch_and_add(p_val, sub_val)
#define sync_add_and_fetch(p_val, add_val) __sync_add_and_fetch(p_val, add_val)
#define sync_sub_and_fetch(p_val, add_val) __sync_sub_and_fetch(p_val, add_val)

#define sync_val_compare_and_swap(p_val, if_value, set_value) \
            __sync_val_compare_and_swap(p_val, if_val, set_val)

#define sync_synchronize() __sync_synchronize()
#define sync_barrier() sync_synchronize()

#endif /* ATOMIC_OPS_H */
