/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   buddydef.h
 * Author: Erwin Diepgrond <e.j.diepgrond@gmail.com>
 *
 * Created on September 7, 2017, 2:18 PM
 */

#ifndef BUDDYDEF_H
#define BUDDYDEF_H
#include "pmap.h"


//#define BUDDY 7 //Defines max order of buddies

//get buddy by physical adress
#define BUDDYPHY(A,ORDER) (typeof(A))( (uint32_t) A ^ 1 << ORDER) 
//Get buddy by page_info *
#define BUDDY_GET_BUDDY_PAGE(A,ORDER) pa2page(BUDDYPHY(page2pa(A),ORDER))

//Get master/slave by physical memory
#define BUDDYMASTERPHY(a,b) (typeof(a)) ( (uint32_t)a & (uint32_t)b )
#define BUDDYSLAVEPHY(a,b) (typeof(a)) ( (uint32_t)a | (uint32_t)b )

//Get master/slave by page_info *
#define BUDDY_GET_SLAVE(a,b) pa2page(BUDDYSLAVEPHY(page2pa(a), page2pa(b)))
#define BUDDY_GET_MASTER(a,b) pa2page(BUDDYMASTERPHY(page2pa(a), page2pa(b)))

#endif /* BUDDYDEF_H */

