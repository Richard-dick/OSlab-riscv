/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                                   Memory Management
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */
#ifndef MM_H
#define MM_H

#include <type.h>
#include <os/list.h>
#include <pgtable.h>
#include <os/sched.h>

#define MAP_KERNEL 1
#define MAP_USER 2
#define MEM_SIZE 32
#define PAGE_SIZE 4096 // 4K
#define INIT_MASTER_KERNEL_STACK 0xffffffc052000000
#define INIT_SLAVE_KERNEL_STACK (INIT_MASTER_KERNEL_STACK+PAGE_SIZE)
#define FREEMEM_KERNEL (INIT_SLAVE_KERNEL_STACK+(PAGE_SIZE<<1))


/* Rounding; only works for n = power of two */
#define ROUND(a, n)     (((((uint64_t)(a))+(n)-1)) & ~((n)-1))
#define ROUNDDOWN(a, n) (((uint64_t)(a)) & ~((n)-1))

// ! 从va得到三类vpn
typedef enum {
    UNUSED,
    SWAPABLE,
    UNSWAPABLE,
    COPY_ON_WRITE,
    NOT_IN_MEM,
} page_status_t;

typedef struct PageNode{
    page_status_t status;
    int read_member;
    list_node_t list;
    uint64_t kva;
    // ! 这个只是记录虚拟地址, 包括内核和用户, 还有换入SD卡后的pte项
    uint64_t uva; 
    PTE* pte;
}page_info_t;

// 共享内存, 16张就差不多了, 你还要我怎样?
#define NUM_MAX_SHM 16
typedef struct share_page{
    uint16_t valid;
    uint16_t key;
    uint32_t num;
    uintptr_t pa;
}shm_t;
shm_t shm_page_table[NUM_MAX_SHM];

#define PAGE_NUM 300//160 // 16PCB*10PAGE

#define VPN_MASK ((1ul << PPN_BITS) - 1)
#define GetVPN2(va) ((va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS)) & VPN_MASK)
#define GetVPN1(va) ((va >> (NORMAL_PAGE_SHIFT + PPN_BITS)) & VPN_MASK)
#define GetVPN0(va) ((va >> NORMAL_PAGE_SHIFT) & VPN_MASK)

#define PageValid(pte) (pte & _PAGE_PRESENT)
#define GetPFN(pa) (pa >> NORMAL_PAGE_SHIFT)
#define GetPPN(pa) (GetPFN(pa) << _PAGE_PFN_SHIFT)

// 定义第9位为SD卡位, 如果为0
#define _PAGE_INSD (1 << 9)     // ! 自我定义, 忽略reserve
#define inSD(pte) ((pte) & _PAGE_INSD)
#define isWrite(pte) ((pte) & _PAGE_WRITE)
#define isValid(pte) ((pte) & _PAGE_PRESENT)
extern page_info_t page_manager[PAGE_NUM];
extern page_info_t * allocPagetable(pcb_t*, page_status_t/*, uint64_t*/);
extern page_info_t * get_swap_page(list_head *head);
page_info_t* get_first_page(list_head* head);
extern list_head free_list;

extern ptr_t allocPage(int numPage);
// TODO [P4-task1] */
void freePage(page_info_t*);

// #define S_CORE
// NOTE: only need for S-core to alloc 2MB large page
#ifdef S_CORE
#define LARGE_PAGE_FREEMEM 0xffffffc056000000
#define USER_STACK_ADDR 0x400000
extern ptr_t allocLargePage(int numPage);
#else
// NOTE: A/C-core
#define USER_STACK_ADDR 0xf00010000
#endif

// TODO [P4-task1] */
extern void* kmalloc(size_t size);
extern void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir);
extern uintptr_t alloc_page_helper(uintptr_t va, pcb_t*, page_status_t/*uintptr_t pgdir*/);
extern void build_page_helper(uintptr_t va, pcb_t*, page_status_t, uintptr_t pa);

extern PTE* pte_checker(uintptr_t va, uintptr_t pgdir);
extern uint64_t swap_to_sd(page_info_t*, pcb_t*);
extern void swap_to_mem(PTE* pte, uint64_t, uint64_t);
extern void init_pagetable();
extern uint32_t get_sector();
extern PTE* get_pte(page_info_t* page, pcb_t*);
extern uint64_t do_swap(pcb_t*);
extern uint64_t do_snapshot(uintptr_t va);
extern uint64_t do_getpa(uintptr_t va);

// TODO [P4-task4]: shm_page_get/dt */
uintptr_t shm_page_get(int key);
void shm_page_dt(uintptr_t addr);



#endif /* MM_H */
