#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <os/mm.h>
#include <os/loader.h>
#include <os/string.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>

#include <os/task.h>

#define RR
extern void ret_from_exception();

pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack_master = INIT_MASTER_KERNEL_STACK + PAGE_SIZE- sizeof(switchto_context_t)- sizeof(regs_context_t);
pcb_t pid0_pcb_master = {
    .pid = 0,
    .pgdir = 0xffffffc051000000,
    .kernel_sp = (ptr_t)pid0_stack_master,
    .user_sp = (ptr_t)pid0_stack_master
};
const ptr_t pid0_stack_slave = INIT_SLAVE_KERNEL_STACK + PAGE_SIZE- sizeof(switchto_context_t) - sizeof(regs_context_t);
pcb_t pid0_pcb_slave = {
    .pid = 0,
    .pgdir = 0xffffffc051000000,
    .kernel_sp = (ptr_t)pid0_stack_slave,
    .user_sp = (ptr_t)pid0_stack_slave
};

extern task_info_t tasks[TASK_MAXNUM];
extern struct list_node ready_queue;
// extern list_head core_queue[CORE_NUM];
LIST_HEAD(sleep_queue);

/* current running task PCB */
// pcb_t * volatile current_running;
pcb_t * volatile core_running[CORE_NUM];

/* global process id */
pid_t process_id = 1;

void do_scheduler(void)
{
    // TODO: [p2-task3] Check sleep queue to wake up PCBs
    uint64_t cpu_id = get_current_cpu_id();
    pcb_t *current_running = core_running[cpu_id];
    pcb_t * last_running = current_running;
    check_sleeping();

    // TODO: [p5-task3] Check send/recv queue to unblock PCBs





    if(last_running->status == TASK_RUNNING && last_running->pid != 0)
    {// 加入非初始进程
        current_running->status = TASK_READY;
        add_queue(&ready_queue, current_running);
    }

    if(list_is_empty(&ready_queue)){
        // 无奈选择原始的pcb轮转
        current_running = cpu_id ? &pid0_pcb_slave : &pid0_pcb_master;
    }else{
        list_node_t *next_list = ready_queue.next;
        pcb_t * next_running;
        while(next_list != &ready_queue.next){
            next_running = list_entry(next_list, pcb_t, list);
            if(next_running->mask & GETMASK(cpu_id))
            {// * 说明是可以运行
                del_queue(next_list);
                break;
            }// ! 由于只需要找到一个，所以不存在导致NULL的情况
            next_list = next_list->next;
        }
        // ! 这不应该发生，非空必然能找到一个，但未必能找到自己的。。。
        if(next_list == &ready_queue){
            next_running = cpu_id ? &pid0_pcb_slave : &pid0_pcb_master;
        }

        current_running = next_running;
    }

    current_running->status = TASK_RUNNING;
    core_running[cpu_id] = current_running;
    // screen_move_cursor(current_running->cursor_x, current_running->cursor_y);
    if(last_running->pgdir != current_running->pgdir){
        set_satp(SATP_MODE_SV39, current_running->pid, (kva2pa(current_running->pgdir) >> NORMAL_PAGE_SHIFT));
        local_flush_tlb_all();
    }
    switch_to(last_running, current_running);
}

void do_sleep(uint32_t sleep_time)
{
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    // 2. set the wake up time for the blocked task
    // 3. reschedule because the current_running is blocked.
    pcb_t *current_running = core_running[get_current_cpu_id()];
    current_running->status = TASK_BLOCKED;

    current_running->wakeup_time = sleep_time + get_timer();
    add_queue(&sleep_queue, current_running);

    do_scheduler();
}

void do_block(list_node_t *pcb_list, list_head *queue)
{
    // TODO: [p2-task2] block the pcb task into the block queue
    pcb_t *temp = list_entry(pcb_list, pcb_t, list);
    //assert(temp == current_running);
    temp->status = TASK_BLOCKED;
    add_queue(queue, temp);
    do_scheduler();
}

void do_unblock(list_head *head)
{
    // TODO: [p2-task2] unblock the `pcb` from the block queue
    pcb_t * unblocked_task = get_head(head);
    unblocked_task->status = TASK_READY;
    add_queue(&ready_queue, unblocked_task);
}


// shell bash
pid_t do_exec(char *name, int argc, char *argv[])
{
    pcb_t *new_pcb;
    page_info_t* temp_page;
    int i;
    for(i = 1; i < NUM_MAX_TASK; ++i) // pcb[0]不考虑，固定为shell
    {
        if(pcb[i].status == TASK_EXITED || pcb[i].status == TASK_AVAILABLE)
        {
            new_pcb = &pcb[i];
            break;
        }
    }
    

    if(i == NUM_MAX_TASK) assert(0); // pcb不够，失败了。
    new_pcb->status = TASK_READY;
    new_pcb->pagetable.upbound = MAX_PAGETABLE_NUM;
    new_pcb->pagetable.pagenum = 0;
    init_list_head(&new_pcb->pagetable.pagehead);
    // 重建所有地址
    temp_page = allocPagetable(new_pcb, UNSWAPABLE);
    new_pcb->pgdir = temp_page->kva;
    clear_pgdir(new_pcb->pgdir);
    share_pgtable(new_pcb->pgdir, pa2kva(PGDIR_PA));
    temp_page = allocPagetable(new_pcb, UNSWAPABLE);
    new_pcb->kernel_sp = temp_page->kva+ PAGE_SIZE;
    new_pcb->user_sp = USER_STACK_ADDR;
    new_pcb->kernel_stack_base = new_pcb->kernel_sp;
    new_pcb->user_stack_base = new_pcb->user_sp;

    uint64_t kva_stack_addr = alloc_page_helper(new_pcb->user_sp - PAGE_SIZE, new_pcb, UNSWAPABLE) + PAGE_SIZE;
    uint64_t entry_addr = load_task_img(name, new_pcb);

    new_pcb->list.next = NULL;
    new_pcb->list.prev = NULL;
    new_pcb->wait_list.next = &new_pcb->wait_list;
    new_pcb->wait_list.prev = &new_pcb->wait_list;

    new_pcb->pid = process_id++;
    new_pcb->childnum = 0;
    new_pcb->cursor_x = 0;
    new_pcb->cursor_y = 0;
    new_pcb->wakeup_time = 0;

    new_pcb->mode = USER;
    pcb_t *current_running = core_running[get_current_cpu_id()];
    new_pcb->mask = current_running->mask;

    regs_context_t *pt_regs =
        (regs_context_t *)(new_pcb->kernel_sp - sizeof(regs_context_t));
    // 首先初始化32个寄存器
    for(i = 0; i < 32; ++i){
       pt_regs->regs[i] = 0;
    }
    pt_regs->regs[1] = entry_addr;     //ra
    
    pt_regs->regs[4] = (reg_t)new_pcb;//tp
    
    pt_regs->sepc  = entry_addr;
    pt_regs->sstatus = 0x00000020;

    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    
    new_pcb->kernel_sp = pt_switchto;

    pt_switchto->regs[0] = ret_from_exception;         //ra寄存器
    
    pt_switchto->regs[1] = (reg_t)pt_switchto;  //sp寄存器

    /*
        ? 到目前为止，pcb创建成功，下一步是传参
        ! 采取两个, kva和va两种方式, 齐头并进, 地址采用va, 内容采用kva
    */
    char *kva_argc_addr, *va_argc_addr;
    uint64_t * kva_argv_base, *va_argv_base;
    // 确定两个地址，
    va_argv_base = new_pcb->user_sp - ((argc + 1) << 3);
    va_argc_addr = (char *)va_argv_base;
    kva_argv_base = kva_stack_addr - ((argc + 1) << 3);
    kva_argc_addr = (char *)kva_argv_base;
    // 即可确定a0和a1寄存器
    pt_regs->regs[10] = argc;
    pt_regs->regs[11] = va_argv_base; // ! 需要地址做值, 传入va

    // 两个参数的初始化完成
    for(i = 0 ; i != argc; ++i)
    {
        int len = strlen(argv[i]) + 1;
        // ! 同步变化
        va_argc_addr -= len; kva_argc_addr -= len;
        // ! 修改地址内容, 用kva
        strncpy(kva_argc_addr, argv[i], len);
        *kva_argv_base = va_argc_addr; // argv地址上的内容为argc_addr
        ++va_argv_base;// 开始准备下一个argv
        ++kva_argv_base;
    }// 结束后argv_base再补一个八位的0；argc_addr就是最后的栈指针，记得128对齐

    *kva_argv_base = 0;
    // 高地址变成低地址，这可也太容易了
    va_argc_addr = ((uint64_t)va_argc_addr >> 7) << 7;
    new_pcb->user_sp = va_argc_addr;
    pt_regs->regs[2] = new_pcb->user_sp;      //sp

    add_queue(&ready_queue, new_pcb);

    return new_pcb->pid;
}

void do_exit(void)
{
    pcb_t *current_running = core_running[get_current_cpu_id()];
    pcb_t *to_exit_pcb = current_running;
    // release wait_list
    while(!list_is_empty(&(to_exit_pcb->wait_list))){
        pcb_t *wait_pcb;
        wait_pcb = list_entry(to_exit_pcb->wait_list.prev, pcb_t, list);
        if(wait_pcb->status != TASK_EXITED){
            do_unblock(&(to_exit_pcb->wait_list));
        }
    }

    // ! 释放页表
    // list_head * page = &to_exit_pcb->pagetable.pagehead;
    // page_info_t* to_free_page;
    // while(!list_is_empty(page)){
    //     to_free_page = get_first_page(page);// 内存里的释放掉, 磁盘里的不管了, 直接加入到page_manager里去, 因为他们没有内存地址
    //     if(to_free_page->status == NOT_IN_MEM){// 此时先不清理内存, 等到了alloc时, 从freelist里来的需要清理一下即可
    //         to_free_page->uva = 0;
    //         to_free_page->status = UNUSED;
    //         del_queue(&to_free_page->list);
    //     }else{
    //         freePage(to_free_page);
    //     }
    // }
    del_queue(&(to_exit_pcb->list));
    to_exit_pcb->status = TASK_EXITED;

    do_scheduler();
}

int do_kill(pid_t pid)
{
    //disable_preempt(); // 防止被时间中断掉，造成blocked情况的出错。
    int i, res = 1;
    for(i = 0; i != NUM_MAX_TASK; ++i){
        if(pcb[i].pid == pid)
            break;
    }
    if(i == NUM_MAX_TASK) return 0;

    pcb_t *to_kill_pcb = pcb + i;

    while(!list_is_empty(&(to_kill_pcb->wait_list))){
        pcb_t *wait_pcb;
        wait_pcb = list_entry(to_kill_pcb->wait_list.next, pcb_t, list);
        if(wait_pcb->status != TASK_EXITED){
            do_unblock(&(to_kill_pcb->wait_list));
        }
    }
    to_kill_pcb->pid = 0;

    // 释放锁
    release_pcb_lock(pid);
    // list_head * page = &to_kill_pcb->pagetable.pagehead;
    // page_info_t* to_free_page;
    // while(!list_is_empty(page)){
    //     to_free_page = get_first_page(page);// 内存里的释放掉, 磁盘里的不管了, 直接加入到page_manager里去, 因为他们没有内存地址
    //     if(to_free_page->status == NOT_IN_MEM){// 此时先不清理内存, 等到了alloc时, 从freelist里来的需要清理一下即可
    //         to_free_page->uva = 0;
    //         to_free_page->status = UNUSED;
    //         del_queue(&to_free_page->list);
    //     }else{
    //         freePage(to_free_page);
    //     }
    // }

    switch (to_kill_pcb->status)
    {
    case TASK_BLOCKED: // 肯定有锁，从队列中删除，然后修改成EXITED，还要把锁id调0；
    // 但也可能是sleep_queue里面的，核心就是判断是lock里面的，还是do_sleep里的。所以另外拆出来一个sleep态算了。
        del_queue(&(to_kill_pcb->list));
        to_kill_pcb->status = TASK_EXITED;
        break;

    case TASK_RUNNING: // 可能有锁，也需要从队列中删除，这个最为复杂。多把锁，我杀了你。
        to_kill_pcb->status = TASK_EXITED;
        do_scheduler();
        break;

    case TASK_READY:
        del_queue(&(to_kill_pcb->list));
        to_kill_pcb->status = TASK_EXITED;
        break;

    case TASK_EXITED: // good，但到底算不算成功呢？
        res = 0;
        break;

    default:
        res = 0;
        // assert(0);
        break;
    }
    //enable_preempt();
    
    return res;
}

int do_waitpid(pid_t pid)
{
    int i;
    for(i = 0; i != NUM_MAX_TASK; ++i){
        if(pcb[i].pid == pid)
            break;
    }
    if(i == NUM_MAX_TASK) return 0;

    pcb_t *wait_pcb = pcb + i;

    if(wait_pcb->status != TASK_EXITED && wait_pcb->status != TASK_AVAILABLE)
    {
        pcb_t *current_running = core_running[get_current_cpu_id()];
        current_running->status = TASK_BLOCKED;
        add_queue(&(wait_pcb->wait_list), current_running);
    }
        // do_block(&(current_running->list), &(wait_pcb->wait_list));
    do_scheduler();
    return pid;
}

void do_process_show()
{
    printk("[PRCESS TABLE]\n");
    int i;
    int run_core = 0;
    for(i = 0; i < NUM_MAX_TASK; ++i){
        // run_core = get_current_cpu_id();
        switch(pcb[i].status){
            case TASK_RUNNING:
                if(pcb[i].pid == core_running[0]->pid) run_core = 0;
                else run_core = 1;
                printk("[%d] PID : %d STATUS : %s MASK : 0x%d on Core %d\n", i, pcb[i].pid, "RUNNING", pcb[i].mask, run_core);
                break;
            case TASK_BLOCKED:
                printk("[%d] PID : %d STATUS : %s MASK : 0x%d\n", i, pcb[i].pid, "BLOCKED", pcb[i].mask);
                break;
            case TASK_EXITED:
                //printk("[%d] PID : %d STATUS : %s MASK : 0x%d\n", i, pcb[i].pid, "EXITED", pcb[i].mask);
                break;
            case TASK_READY:
                printk("[%d] PID : %d STATUS : %s MASK : 0x%d\n", i, pcb[i].pid, "READY", pcb[i].mask);
                break;
            case TASK_AVAILABLE:
                //printk("[%d] PID : %d STATUS : %s MASK : 0x%d\n", i, pcb[i].pid, "AVAILABLE", pcb[i].mask);
                break;
            default:
                break;
        }
    }
}

pid_t do_getpid()
{
    pcb_t *current_running = core_running[get_current_cpu_id()];
    return current_running->pid;
}


void add_queue(list_head* head, pcb_t* node)
{
    _list_add_tail(&(node->list), head);
}

pcb_t* get_head(list_head* head)
{
    // if(list_is_empty(head)) return NULL;
    list_head * list = head->next;
    pcb_t* tmp = list_entry(list, pcb_t, list);
    del_queue(list);
    return tmp;
}

void del_queue(list_node_t * node)
{
    list_del(node);
}

void do_taskset_exec(char *name, int argc, char *argv[], int mask)
{
    pcb_t *new_pcb;
    int i;
    for(i = 1; i < NUM_MAX_TASK; ++i) // pcb[0]不考虑，固定为shell
    {
        if(pcb[i].status == TASK_EXITED){ // 已经退出，则复用
            new_pcb = &pcb[i];
            new_pcb->kernel_sp = new_pcb->kernel_stack_base;
            new_pcb->user_sp = new_pcb->user_stack_base;
            break;
        }else if(pcb[i].status == TASK_AVAILABLE){ // 可以使用
            new_pcb = &pcb[i];
            new_pcb->kernel_sp = allocPage(1) + PAGE_SIZE;
            new_pcb->user_sp = allocPage(1) + PAGE_SIZE;
            new_pcb->kernel_stack_base = new_pcb->kernel_sp;
            new_pcb->user_stack_base = new_pcb->user_sp;
            break;
        }
    }

    if(i == NUM_MAX_TASK) assert(0); // pcb不够，失败了。
    new_pcb->status = TASK_READY;

    uint64_t entry_addr = load_task_img(name, new_pcb->pgdir);

    new_pcb->list.next = NULL;
    new_pcb->list.prev = NULL;
    new_pcb->wait_list.next = &new_pcb->wait_list;
    new_pcb->wait_list.prev = &new_pcb->wait_list;

    new_pcb->pid = process_id++;
    new_pcb->cursor_x = 0;
    new_pcb->cursor_y = 0;
    new_pcb->wakeup_time = 0;

    new_pcb->mode = USER;
    pcb_t *current_running = core_running[get_current_cpu_id()];
    new_pcb->mask = mask;

    regs_context_t *pt_regs =
        (regs_context_t *)(new_pcb->kernel_sp - sizeof(regs_context_t));
    // 首先初始化32个寄存器
    for(i = 0; i < 32; ++i){
       pt_regs->regs[i] = 0;
    }
    pt_regs->regs[1] = entry_addr;     //ra
    
    pt_regs->regs[4] = (reg_t)new_pcb;//tp
    
    pt_regs->sepc  = entry_addr;
    pt_regs->sstatus = 0x00000020;

    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    
    new_pcb->kernel_sp = pt_switchto;

    pt_switchto->regs[0] = ret_from_exception;         //ra寄存器
    
    pt_switchto->regs[1] = (reg_t)pt_switchto;  //sp寄存器

    /*
    到目前为止，pcb创建成功，下一步是传参，emmm，其实应该放在栈初始化之前。
    */

    char *argc_addr;
    uint64_t * argv_base;
    // 确定两个地址，
    argv_base = new_pcb->user_sp - ((argc + 1) << 3);
    argc_addr = (char *)argv_base;
    // 即可确定a0和a1寄存器
    pt_regs->regs[10] = argc;
    pt_regs->regs[11] = argv_base;

    // 两个参数的初始化完成
    for(i = 0 ; i != argc; ++i)
    {
        int len = strlen(argv[i]) + 1;
        argc_addr = argc_addr - len;
        strncpy(argc_addr, argv[i], len);
        *argv_base = argc_addr; // argv地址上的内容为argc_addr
        ++argv_base;// 开始准备下一个argv
    }// 结束后argv_base再补一个八位的0；argc_addr就是最后的栈指针，记得128对齐

    *argv_base = 0;
    // 高地址变成低地址，这可也太容易了
    argc_addr = ((uint64_t)argc_addr >> 7) << 7;
    new_pcb->user_sp = argc_addr;
    pt_regs->regs[2] = new_pcb->user_sp;      //sp

    add_queue(&ready_queue, new_pcb);
}


void do_taskset_chg(pid_t pid, int mask)
{
    int i;
    for(i = 0; i != NUM_MAX_TASK; ++i){
        if(pcb[i].pid == pid)
            break;
    }
    if(i == NUM_MAX_TASK) assert(0);

    pcb_t *to_chg_pcb = pcb + i;
    to_chg_pcb->mask = mask;

}

void do_thread_create(pthread_t *thread, void (*start_routine)(void*), void *arg)
{ 
    uint64_t entry_addr = start_routine;
    pcb_t* father_pcb = core_running[get_current_cpu_id()];
    father_pcb->childnum++;
    pcb_t *new_pcb;
    page_info_t* temp_page;
    int i;
    for(i = 1; i < NUM_MAX_TASK; ++i)
    {
        if(pcb[i].status == TASK_EXITED || pcb[i].status == TASK_AVAILABLE)
        {
            new_pcb = &pcb[i];
            break;
        }
    }
    if(i == NUM_MAX_TASK) assert(0); // pcb不够，失败了。

    new_pcb->status = TASK_READY;
    new_pcb->pagetable.upbound = MAX_PAGETABLE_NUM;
    new_pcb->pagetable.pagenum = 0;
    init_list_head(&new_pcb->pagetable.pagehead);
    // 重建所有地址
    new_pcb->pgdir = father_pcb->pgdir;
    share_pgtable(new_pcb->pgdir, father_pcb->pgdir);
    temp_page = allocPagetable(new_pcb, UNSWAPABLE);
    new_pcb->kernel_sp = temp_page->kva + PAGE_SIZE;
    new_pcb->user_sp = USER_STACK_ADDR + father_pcb->childnum * PAGE_SIZE;
    new_pcb->kernel_stack_base = new_pcb->kernel_sp;
    new_pcb->user_stack_base = new_pcb->user_sp;

    alloc_page_helper(new_pcb->user_sp - PAGE_SIZE, new_pcb, UNSWAPABLE) + PAGE_SIZE;

    new_pcb->list.next = NULL;
    new_pcb->list.prev = NULL;
    init_list_head(&new_pcb->wait_list);

    new_pcb->pid = father_pcb->pid;
    new_pcb->cursor_x = 0;
    new_pcb->cursor_y = 0;
    new_pcb->wakeup_time = 0;

    new_pcb->mode = THREAD;
    pcb_t *current_running = core_running[get_current_cpu_id()];
    new_pcb->mask = current_running->mask;

    regs_context_t *pt_regs =
        (regs_context_t *)(new_pcb->kernel_sp - sizeof(regs_context_t));
    // 首先初始化32个寄存器
    for(i = 0; i < 32; ++i){
       pt_regs->regs[i] = 0;
    }
    pt_regs->regs[1] = entry_addr;     //ra
    
    pt_regs->regs[4] = (reg_t)new_pcb;//tp
    
    pt_regs->sepc  = entry_addr;
    pt_regs->sstatus = 0x00000020;

    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    
    new_pcb->kernel_sp = pt_switchto;

    pt_switchto->regs[0] = ret_from_exception;         //ra寄存器
    
    pt_switchto->regs[1] = (reg_t)pt_switchto;  //sp寄存器

    /*
        ? 到目前为止，pcb创建成功，下一步是传参
        * 用寄存器即可
    */
    // 即可确定a0寄存器
    pt_regs->regs[10] = arg;

    add_queue(&ready_queue, new_pcb);
    *thread = new_pcb->pid;

    // return new_pcb->pid;
}