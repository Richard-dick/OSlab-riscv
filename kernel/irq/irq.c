#include <os/irq.h>
#include <os/time.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/kernel.h>
#include <plic.h>
#include <os/mm.h>
#include <printk.h>
#include <assert.h>
#include <screen.h>

handler_t irq_table[IRQC_COUNT];
handler_t exc_table[EXCC_COUNT];

void interrupt_helper(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task3] & [p2-task4] interrupt handler.
    // call corresponding handler by the value of `scause`
    // scause: 第63位为1则说明是中断，否则为系统调用
    handler_t *table = (scause >> 63) ? irq_table : exc_table; 
    uint64_t exc_code = scause & ~(1UL << 63);
    table[exc_code](regs, stval, scause);
}

void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p2-task4] clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule
    check_sleeping();
    // check_net_send();
    // check_net_recv();
    bios_set_timer(get_ticks() + TIMER_INTERVAL);

    do_scheduler();
}

void handle_irq_ext(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    // TODO: [p5-task4] external interrupt handler.
    // Note: plic_claim and plic_complete will be helpful ...

    // !1: claim获得PLIC的中断信号
    uint32_t plic_id = plic_claim();

    // printk("plic id::%d\n", plic_claim);
    // handle_other(regs, stval, scause);

    // 匹配外设信息, 并进行相应的中断处理
    if(plic_id == PLIC_E1000_PYNQ_IRQ)
    {
        net_handle_irq();
    }

    // 结束中断处理
    plic_complete(plic_id);
}

void init_exception()
{
    /* TODO: [p2-task3] initialize exc_table */
    /* NOTE: handle_syscall, handle_other, etc.*/
    for(int i = 0; i != EXCC_COUNT; ++i)
        exc_table[i] = handle_other;
        
    exc_table[EXCC_SYSCALL] = handle_syscall;
    exc_table[EXCC_INST_PAGE_FAULT] = handle_inst_pagefault;
    exc_table[EXCC_LOAD_PAGE_FAULT] = handle_load_page_fault;
    exc_table[EXCC_STORE_PAGE_FAULT] = handle_store_page_fault;

    /* TODO: [p2-task4] initialize irq_table */
    /* NOTE: handle_int, handle_other, etc.*/
    for (int i = 0; i != IRQC_COUNT; ++i)
        irq_table[i] = handle_irq_timer;
    irq_table[IRQC_S_EXT] = handle_irq_ext;
    /* TODO: [p2-task3] set up the entrypoint of exceptions */
    setup_exception();
}

void handle_other(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    char* reg_name[] = {
        "zero "," ra  "," sp  "," gp  "," tp  ",
        " t0  "," t1  "," t2  ","s0/fp"," s1  ",
        " a0  "," a1  "," a2  "," a3  "," a4  ",
        " a5  "," a6  "," a7  "," s2  "," s3  ",
        " s4  "," s5  "," s6  "," s7  "," s8  ",
        " s9  "," s10 "," s11 "," t3  "," t4  ",
        " t5  "," t6  "
    };
    for (int i = 0; i < 32; i += 3) {
        for (int j = 0; j < 3 && i + j < 32; ++j) {
            printk("%s : %016lx ",reg_name[i+j], regs->regs[i+j]);
        }
        printk("\n\r");
    }
    printk("sstatus: 0x%lx sbadaddr: 0x%lx scause: %lu\n\r",
           regs->sstatus, regs->sbadaddr, regs->scause);
    printk("sepc: 0x%lx\n\r", regs->sepc);
    printk("tval: 0x%lx cause: 0x%lx\n", stval, scause);
    assert(0);
}

void handle_load_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    PTE *pte;
    pcb_t *current_running = core_running[get_current_cpu_id()];
    uintptr_t kva, pgdir = current_running->pgdir;
    pte = pte_checker(stval, pgdir);
    if(pte == 0){// ! 说明无映射
        kva = alloc_page_helper(stval, current_running, SWAPABLE);
    }else{//* 说明要么是SD卡里, 要么是权限错误
        if(inSD(*pte)){
            if(current_running->pagetable.pagenum == current_running->pagetable.upbound){
                kva = do_swap(current_running);
            }
            swap_to_mem(pte, stval, kva);
        }else{
            assert(get_attribute(*pte, _PAGE_ACCESSED) == 0);
            set_attribute(pte, _PAGE_ACCESSED);
        }
        local_flush_tlb_all();
    }
}

void handle_store_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause)
{
    PTE *pte;
    pcb_t *current_running = core_running[get_current_cpu_id()];
    uintptr_t kva, pgdir = current_running->pgdir;
    pte = pte_checker(stval, pgdir);
    if(pte == 0){// ! 说明无映射
        kva = alloc_page_helper(stval, current_running, SWAPABLE);
    }else{//* 说明要么是SD卡里, 要么是权限错误
        if(inSD(*pte)){
            if(current_running->pagetable.pagenum == current_running->pagetable.upbound){
                kva = do_swap(current_running);
            }
            swap_to_mem(pte, stval, kva);
        }else{
            if(isWrite(*pte)){
                // ! 说明是可写, 那就是其他权限错误
                assert(get_attribute(*pte, _PAGE_ACCESSED) == 0);
                set_attribute(pte, _PAGE_ACCESSED | _PAGE_DIRTY);
            }else{// 是copy_on页
                list_head *pagehead = &current_running->pagetable.pagehead;
                list_node_t *page_list = pagehead->next;
                page_info_t* tmp;
                kva = pa2kva(get_pa(*pte));
                while(page_list != pagehead){
                    tmp = list_entry(page_list, page_info_t, list);
                    if(tmp->kva == kva){
                        break;// 此时只能比较内容了, 内容一致, 就是映射到同一页上
                    }
                    page_list = page_list->next;
                }
                assert(tmp->status == COPY_ON_WRITE);
                if(tmp->read_member == 1)
                {
                    tmp->read_member = 0;
                    set_attribute(pte, _PAGE_WRITE);
                    tmp->status = SWAPABLE;
                }else{
                    tmp->read_member--;
                    clean_attribute(pte, _PAGE_PRESENT); // 使其失效
                    page_info_t* new = allocPagetable(current_running, SWAPABLE);
                    build_page_helper(stval,current_running, SWAPABLE, kva2pa(new->kva));// 重新映射
                    memcpy(kva, tmp->kva, PAGE_SIZE);
                }
            }
        }
        local_flush_tlb_all();
    }
}


void handle_inst_pagefault(regs_context_t *regs, uint64_t stval, uint64_t cause){
    pcb_t *current_running = core_running[get_current_cpu_id()];
    PTE *pte = pte_checker(stval, current_running->pgdir);
    if(pte != 0){
        set_attribute(pte, _PAGE_ACCESSED | _PAGE_DIRTY);
        local_flush_tlb_all();
    }
    else
        handle_other(regs,stval,cause);
}