/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   kernel_threads.h
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 *
 * Created on October 10, 2017, 11:55 PM
 */

#ifndef KERNEL_THREADS_H
#define KERNEL_THREADS_H
#include "inc/env.h"

#define KERNEL_THREAD_STACK_TOP 0x10000000

extern void _kernel_thread_start();
extern void _kern_thread_yield(env_t * tf);

#define kern_thread_yield(tf) _kern_thread_yield(tf);

void kern_thread_create(void* entry);

void test_thread(env_t * tf);

#endif /* KERNEL_THREADS_H */
