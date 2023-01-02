/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *         The kernel's entry, where most of the initialization work is done.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions:
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

#include <common.h>
#include <asm.h>
#include <asm/unistd.h>
#include <os/loader.h>
#include <os/irq.h>
#include <os/sched.h>
#include <os/lock.h>
#include <os/fs.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/mm.h>
#include <os/smp.h>
// #include <os/net.h>
#include <os/time.h>
#include <os/ioremap.h>
#include <sys/syscall.h>
#include <screen.h>
// #include <e1000.h>
#include <printk.h>
#include <assert.h>
#include <type.h>
#include <csr.h>

extern void ret_from_exception();

// Task info array
task_info_t tasks[TASK_MAXNUM];
LIST_HEAD(ready_queue);

void cancel_preboot_map(){
    // 转成虚拟地址, 开始操作
    PTE * first_pgdir = (PTE *)pa2kva((uintptr_t)PGDIR_PA);
    
    uint64_t pa = 0x50000000lu;
    uint64_t va = pa & VA_MASK;
    uint64_t vpn2 = GetVPN2(va);
    // 通过一级页表的偏移, 找到一级表项, 
    PTE* first_pte = (PTE*)(&first_pgdir[vpn2]);
    // 然后需要从一级表项中得到二级表地址
    PTE *second_pgdir = (PTE*)pa2kva(get_pa(*first_pte));

    clear_pgdir((uintptr_t)second_pgdir);
    *first_pte = 0;
}

static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
    jmptab[SD_WRITE]        = (long (*)())sd_write;
    jmptab[QEMU_LOGGING]    = (long (*)())qemu_logging;
    jmptab[SET_TIMER]       = (long (*)())set_timer;
    jmptab[READ_FDT]        = (long (*)())read_fdt;
    jmptab[MOVE_CURSOR]     = (long (*)())screen_move_cursor;
    jmptab[PRINT]           = (long (*)())printk;
    jmptab[YIELD]           = (long (*)())do_scheduler;
    jmptab[MUTEX_INIT]      = (long (*)())do_mutex_lock_init;
    jmptab[MUTEX_ACQ]       = (long (*)())do_mutex_lock_acquire;
    jmptab[MUTEX_RELEASE]   = (long (*)())do_mutex_lock_release;
}

static void init_task_info(void)
{
    // TODO: Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
    void* rd_addr = (void*)TASK_NUM_LOC;
    // 开始读入image中appinfo的地址
    uint8_t tasknum;
    tasknum = *(uint8_t*)rd_addr;
    ++rd_addr;
    uint32_t task_info_base = 0;
    task_info_base = *(uint32_t*)rd_addr;

    //接下来把相应的image段读到第一个任务的地址处。
    uint32_t begin_block = task_info_base >> 9;
    uint32_t begin_offset = task_info_base - (begin_block << 9);
    uint32_t end_block = (task_info_base + tasknum * TASK_INFO_SIZE) >> 9;
    uint32_t end_offset = (task_info_base + tasknum * TASK_INFO_SIZE) - (end_block << 9);
    uint32_t block_num;

    block_num = end_block - begin_block + (end_offset != 0);

    bios_sdread(kva2pa(0xfffffc0058000000), block_num, begin_block);
    memcpy((uint8_t*)tasks, (uint8_t*)pa2kva(0x58000000ul + begin_offset), tasknum*TASK_INFO_SIZE);
    
}

static void init_pcb_stack(
    ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point,
    pcb_t *pcb)
{

     /* TODO: [p2-task3] initialization of registers on kernel stack
      * HINT: sp, ra, sepc, sstatus
      * NOTE: To run the task in user mode, you should set corresponding bits
      *     of sstatus(SPP, SPIE, etc.).
      */
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));
    // 首先初始化32个寄存器
    for(int i = 0; i < 32; ++i){
       pt_regs->regs[i] = 0;
    }
    if(pcb->mode == USER)
    {
        pt_regs->regs[1] = entry_point;     //ra
        pt_regs->regs[2] = user_stack;      //sp
        //pt_regs->regs[3] = ??;      //gp
        pt_regs->regs[4] = (reg_t)pcb;      //tp
        
        pt_regs->sepc  = entry_point;
        pt_regs->sstatus = (SR_SPIE | SR_SUM) & ~SR_SPP;
    }

    /* TODO: [p2-task1] set sp to simulate just returning from switch_to
     * NOTE: you should prepare a stack, and push some values to
     * simulate a callee-saved context.
     */
    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    
    pcb->kernel_sp = pt_switchto;
    pcb->user_sp = user_stack;
    if( pcb->mode == USER)
    {
        pt_switchto->regs[0] = ret_from_exception;         //ra寄存器
    }else{
        pt_switchto->regs[0] = entry_point;
    }
    
    pt_switchto->regs[1] = (reg_t)pt_switchto;  //sp寄存器
    
}

static void init_pcb(void)
{
    /* TODO: [p2-task1] load needed tasks and init their corresponding PCB */
    uint64_t entry_addr;
    page_info_t* temp_page;

    // 先初始化页表, 这是最重要的

    pcb[0].pagetable.upbound = MAX_PAGETABLE_NUM;
    pcb[0].pagetable.pagenum = 0;
    init_list_head(&pcb[0].pagetable.pagehead);

    temp_page = allocPagetable(pcb, UNSWAPABLE);
    pcb[0].pgdir = temp_page->kva;
    clear_pgdir(pcb[0].pgdir);
    share_pgtable(pcb[0].pgdir, pa2kva(PGDIR_PA));

    // 分配kva, 内核已经是经过map的; 但user还没有经过map
    temp_page = allocPagetable(pcb, UNSWAPABLE);
    pcb[0].kernel_sp = temp_page->kva+ PAGE_SIZE;
    pcb[0].user_sp = USER_STACK_ADDR;
    pcb[0].kernel_stack_base = pcb[0].kernel_sp;
    pcb[0].user_stack_base = pcb[0].user_sp;

    // 得到用户栈在内核的虚地址, 可能需要填入参数
    alloc_page_helper(pcb[0].user_sp - PAGE_SIZE, pcb, UNSWAPABLE/*pcb[0].pgdir*/);

    entry_addr = load_task_img("shell", pcb);

    pcb[0].list.next = NULL;
    pcb[0].list.prev = NULL;
    init_list_head(&pcb[0].wait_list);
    pcb[0].pid = process_id++;
    pcb[0].childnum = 0;
    pcb[0].status = TASK_READY;
    pcb[0].cursor_x = 0;
    pcb[0].cursor_y = 0;
    pcb[0].wakeup_time = 0;
    pcb[0].mode = USER;
    pcb[0].mask = 0x3;

    init_pcb_stack(pcb[0].kernel_sp, pcb[0].user_sp, entry_addr, &pcb[0]);
    add_queue(&ready_queue, pcb);

    for(int i = 1 ; i < NUM_MAX_TASK; ++i){
        pcb[i].pid = -1;
        pcb[i].status = TASK_AVAILABLE;
    }    
}

static void init_syscall(void)
{
    // TODO: [p2-task3] initialize system call table.
    // initialize with NULL
    for(int i = 0; i != NUM_SYSCALLS; ++i){
        syscall[i] = NULL;
    }
    // initialize special syscall:
    syscall[SYSCALL_EXEC]           = (long (*)())do_exec;              //0
    syscall[SYSCALL_EXIT]           = (long (*)())do_exit;              //1
    syscall[SYSCALL_SLEEP]          = (long (*)())do_sleep;             //2
    syscall[SYSCALL_KILL]           = (long (*)())do_kill;              //3
    syscall[SYSCALL_WAITPID]        = (long (*)())do_waitpid;           //4
    syscall[SYSCALL_PS]             = (long (*)())do_process_show;      //5
    syscall[SYSCALL_GETPID]         = (long (*)())do_getpid;            //6
    syscall[SYSCALL_YIELD]          = (long (*)())do_scheduler;         //7
    syscall[SYSCALL_WRITE]          = (long (*)())screen_write;         //20
    syscall[SYSCALL_READCH]         = (long (*)())bios_getchar;         //21
    syscall[SYSCALL_CURSOR]         = (long (*)())screen_move_cursor;   //22
    syscall[SYSCALL_REFLUSH]        = (long (*)())screen_reflush;       //23
    syscall[SYSCALL_CLEAR]          = (long (*)())screen_clear;         //24
    syscall[SYSCALL_GET_TIMEBASE]   = (long (*)())get_time_base;        //30
    syscall[SYSCALL_GET_TICK]       = (long (*)())get_ticks;            //31

    // syscall[SYSCALL_WRITECH]        = (long (*)())bios_putchar;         //33
    // syscall[SYSCALL_GET_ID]         = (long (*)())do_get_process_id;    //34
    syscall[SYSCALL_LOCK_INIT]      = (long (*)())do_mutex_lock_init;   //40
    syscall[SYSCALL_LOCK_ACQ]       = (long (*)())do_mutex_lock_acquire;//41
    syscall[SYSCALL_LOCK_RELEASE]   = (long (*)())do_mutex_lock_release;//42
    syscall[SYSCALL_SHOW_TASK]      = (long (*)())NULL;//43
    syscall[SYSCALL_BARR_INIT]      = (long (*)())do_barrier_init;      //44
    syscall[SYSCALL_BARR_WAIT]      = (long (*)())do_barrier_wait;      //45
    syscall[SYSCALL_BARR_DESTROY]   = (long (*)())do_barrier_destroy;   //46
    syscall[SYSCALL_COND_INIT]      = (long (*)())do_condition_init;    //47
    syscall[SYSCALL_COND_WAIT]      = (long (*)())do_condition_wait;    //48
    syscall[SYSCALL_COND_SIGNAL]    = (long (*)())do_condition_signal;  //49
    syscall[SYSCALL_COND_BROADCAST] = (long (*)())do_condition_broadcast;//50
    syscall[SYSCALL_COND_DESTROY]   = (long (*)())do_condition_destroy; //51
    syscall[SYSCALL_MBOX_OPEN]      = (long (*)())do_mbox_open;         //52
    syscall[SYSCALL_MBOX_CLOSE]     = (long (*)())do_mbox_close;        //53
    syscall[SYSCALL_MBOX_SEND]      = (long (*)())do_mbox_send;         //54
    syscall[SYSCALL_MBOX_RECV]      = (long (*)())do_mbox_recv;         //55
    syscall[SYSCALL_SHM_GET]        = (long (*)())shm_page_get;         //56
    syscall[SYSCALL_SHM_DT]         = (long (*)())shm_page_dt;          //57
    // syscall[SYSCALL_NET_SEND]       = (long (*)())do_net_send;          //63
    // syscall[SYSCALL_NET_RECV]       = (long (*)())do_net_recv;          //64
    syscall[SYSCALL_FS_MKFS]        = (long (*)())do_mkfs;              // 65
    syscall[SYSCALL_FS_STATFS]      = (long (*)())do_statfs;            // 66
    syscall[SYSCALL_FS_CD]          = (long (*)())do_cd;                // 67
    syscall[SYSCALL_FS_MKDIR]       = (long (*)())do_mkdir;             // 68
    syscall[SYSCALL_FS_RMDIR]       = (long (*)())do_rmdir;             // 69
    syscall[SYSCALL_FS_LS]          = (long (*)())do_ls;                // 70
    syscall[SYSCALL_FS_TOUCH]       = (long (*)())do_touch;             // 71
    syscall[SYSCALL_FS_CAT]         = (long (*)())do_cat;               // 72
    syscall[SYSCALL_FS_FOPEN]       = (long (*)())do_fopen;             // 73
    syscall[SYSCALL_FS_FCLOSE]      = (long (*)())do_fclose;            // 74
    syscall[SYSCALL_FS_FREAD]       = (long (*)())do_fread;             // 75
    syscall[SYSCALL_FS_FWRITE]      = (long (*)())do_fwrite;            // 76
    syscall[SYSCALL_FS_LN]          = (long (*)())do_ln;                // 77
    syscall[SYSCALL_FS_RM]          = (long (*)())do_rm;                // 78
    syscall[SYSCALL_FS_LSEEK]       = (long (*)())do_lseek;             // 79

    syscall[SYSCALL_SET_PID]        = (long (*)())do_taskset_chg;       // 80
    syscall[SYS_TASKSET]            = (long (*)())do_taskset_exec;      // 81
    syscall[SYSCALL_PTHREAD_CREATE] = (long (*)())do_thread_create;     // 82
    syscall[SYS_SANPSHOT]           = (long (*)())do_snapshot;          // 83
    syscall[SYS_GETPA]              = (long (*)())do_getpa;             // 84
    
}

int main(void)
{
    uint64_t cpu_id = get_current_cpu_id();
    
    if( cpu_id == 0)
    {
        smp_init();
        lock_kernel();
        // cancel_preboot_map();
        
        // Init jump table provided by kernel and bios(ΦωΦ)
        init_jmptab();

        // Init task information (〃'▽'〃)
        init_task_info();
        init_pagetable();
        core_running[0] = &pid0_pcb_master;
        core_running[1] = &pid0_pcb_slave;
    
        // Read CPU frequency (｡•ᴗ-)_
        time_base = bios_read_fdt(TIMEBASE);

        // // Read Flatten Device Tree (｡•ᴗ-)_
        // e1000 = (volatile uint8_t *)bios_read_fdt(EHTERNET_ADDR);
        // uint64_t plic_addr = bios_read_fdt(PLIC_ADDR);
        // uint32_t nr_irqs = (uint32_t)bios_read_fdt(NR_IRQS);
        // printk("> [INIT] e1000: 0x%lx, plic_addr: 0x%lx, nr_irqs: 0x%lx.\n", e1000, plic_addr, nr_irqs);

        // // IOremap
        // plic_addr = (uintptr_t)ioremap((uint64_t)plic_addr, 0x4000 * NORMAL_PAGE_SIZE);
        // e1000 = (uint8_t *)ioremap((uint64_t)e1000, 8 * NORMAL_PAGE_SIZE);
        // printk("> [INIT] IOremap initialization succeeded.\n");

        // Init Process Control Blocks |•'-'•) ✧
        init_pcb();
        printk("> [INIT] PCB initialization succeeded.\n");

        // Init lock mechanism o(´^｀)o
        init_locks();
        printk("> [INIT] Lock mechanism initialization succeeded.\n");

        // Init interrupt (^_^)
        init_exception();
        printk("> [INIT] Interrupt processing initialization succeeded.\n");

        // // TODO: [p5-task4] Init plic
        // plic_init(plic_addr, nr_irqs);
        // printk("> [INIT] PLIC initialized successfully. addr = 0x%lx, nr_irqs=0x%x\n", plic_addr, nr_irqs);

        // Init network device
        // e1000_init();
        // printk("> [INIT] E1000 device initialized successfully.\n");

        // Init system call table (0_0)
        init_syscall();
        printk("> [INIT] System call initialized successfully.\n");

        // Init screen (QAQ)
        init_screen();
        printk("> [INIT] SCREEN initialization succeeded.\n");

        // TODO: 初始化文件系统和文件描述符
        do_mkfs();
        // init_fdesc();

        init_barriers();
        init_conditions();
        init_mbox();
        
        disable_interrupt();
        wakeup_other_hart();
    }else{
        lock_kernel();
        cancel_preboot_map();
        time_base = bios_read_fdt(TIMEBASE);
        setup_exception();
    }
    cpu_id = get_current_cpu_id();
    // current_running = core_running[cpu_id];
    printk("> [core:%d] Successfully in core!\n", cpu_id);
    bios_set_timer(get_ticks() + time_base/1000); // 开各自的计时器中断
    unlock_kernel();

    enable_interrupt();
    setup_eie();
    

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {
        // If you do non-preemptive scheduling, it's used to surrender control
        //do_scheduler();

        // If you do preemptive scheduling, they're used to enable CSR_SIE and wfi
        //enable_preempt();
        asm volatile("wfi");
        // do_scheduler();
    }

    return 0;

}
