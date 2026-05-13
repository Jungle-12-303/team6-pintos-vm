#ifndef THREADS_PTE_H
#define THREADS_PTE_H

#include "threads/vaddr.h"

/* Functions and macros for working with x86 hardware page tables.
 * See vaddr.h for more generic functions and macros for virtual addresses.
 *
 * Virtual addresses are structured as follows:
 *  63          48 47            39 38            30 29            21 20         12 11         0
 * +-------------+----------------+----------------+----------------+-------------+------------+
 * | Sign Extend |    Page-Map    | Page-Directory | Page-directory |  Page-Table |  Physical  |
 * |             | Level-4 Offset |    Pointer     |     Offset     |   Offset    |   Offset   |
 * +-------------+----------------+----------------+----------------+-------------+------------+
 *               |                |                |                |             |            |
 *               +------- 9 ------+------- 9 ------+------- 9 ------+----- 9 -----+---- 12 ----+
 *                                         Virtual Address
 */

#define PML4SHIFT 39UL /* PML4 인덱스 시작 비트 -SONNY- */
#define PDPESHIFT 30UL /* PDPE 인덱스 시작 비트*/
#define PDXSHIFT  21UL /* PD 인덱스 시작 비트 */
#define PTXSHIFT  12UL /* PT 인덱스 시작 비트 */

#define PML4(la)  ((((uint64_t) (la)) >> PML4SHIFT) & 0x1FF)   /* PML4 인덱스 */
#define PDPE(la) ((((uint64_t) (la)) >> PDPESHIFT) & 0x1FF)    /* PDPE 인덱스 */
#define PDX(la)  ((((uint64_t) (la)) >> PDXSHIFT) & 0x1FF)     /* PD 인덱스 */
#define PTX(la)  ((((uint64_t) (la)) >> PTXSHIFT) & 0x1FF)     /* PT 인덱스 */
#define PTE_ADDR(pte) ((uint64_t) (pte) & ~0xFFF)              /* 프레임 주소 뽑아내기 */
                                                               /* pte = 0x12345007 */
                                                    /* PTE_ADDR(PTE) == 0x12345000 */

/* 중요한 플래그들은 아래에 나열되어 있다.
   PDE 또는 PTE가 "present" 상태가 아닐 때는, 다른 플래그들은 무시된다.
   0으로 초기화된 PDE 또는 PTE는 "not present"로 해석되며, 이는 문제가 없다. */
#define PTE_FLAGS 0x00000000000000fffUL    /* Flag bits. */
#define PTE_ADDR_MASK  0xffffffffffffff000UL /* Address bits. */
#define PTE_AVL   0x00000e00             /* Bits available for OS use. */
#define PTE_P 0x1                        /* 1=present, 0=not present. */
#define PTE_W 0x2                        /* 1=read/write, 0=read-only. */
#define PTE_U 0x4                        /* 1=user/kernel, 0=kernel only. */
#define PTE_A 0x20                       /* 1=accessed, 0=not acccessed. */
#define PTE_D 0x40                       /* 1=dirty, 0=not dirty (PTEs only). */

#endif /* threads/pte.h */


/* 
PTE_FLAGS 활용법
flags값 뽑아내기 -> flags = pte & PTE_FLAGS -> 0x12345007에서 0x007 뽑아냄

PTE_ADDR_MASK 활용법
프레임 주소(물리주소) 부분 뽑아내기 -> addr = pte & PTE_ADDR_MASK -> 0x12345007에서 0x12345000 뽑아냄

PTE_AVL
flags 상위 3비트 -> 우리가 커스텀 할 수 있는 영역

각종 플레그값 뽑아내는 방법
pte & (뽑아내려는 플레그) -> p = pte & PTE_P

-SONNY- */
