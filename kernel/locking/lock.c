#include <os/lock.h>
#include <os/sched.h>
#include <os/list.h>
#include <os/string.h>
#include <atomic.h>

mutex_lock_t mlocks[LOCK_NUM];
barrier_t barriers[BARRIER_NUM];
condition_t conditions[CONDITION_NUM];
mailbox_t mboxs[MBOX_NUM];
uint32_t lock_cnt;

void init_locks(void)
{
    /* TODO: [p2-task2] initialize mlocks */
    int i;
    for(i = 0; i != LOCK_NUM; ++i)
    {
        init_list_head(&(mlocks[i].block_queue));
        mlocks[i].key = 0;
        mlocks[i].owner = -1;
        spin_lock_init(&(mlocks[i].lock));
    }
}

void spin_lock_init(spin_lock_t *lock)
{
    /* TODO: [p2-task2] initialize spin lock */
    lock->status = UNLOCKED;
}

int spin_lock_try_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] try to acquire spin lock */
    return lock->status == UNLOCKED;
}

void spin_lock_acquire(spin_lock_t *lock)
{
    /* TODO: [p2-task2] acquire spin lock */
    lock->status = LOCKED;
}

void spin_lock_release(spin_lock_t *lock)
{
    /* TODO: [p2-task2] release spin lock */
    lock->status = UNLOCKED;
}

int do_mutex_lock_init(int key)
{
    /* TODO: [p2-task2] initialize mutex lock */
    int i;
    for(i = 0; i != LOCK_NUM; ++i)
    {
        if(mlocks[i].key == key){
            return i;
        }else if(mlocks[i].owner == -1){
            break;
        }
    }
    if(i == LOCK_NUM) while (1) ;    

    mlocks[i].owner = 0;
    mlocks[i].key = key;
    mlocks[i].lock.status = UNLOCKED;
    return i;
}

void do_mutex_lock_acquire(int mlock_idx)
{
    pcb_t *current_running = core_running[get_current_cpu_id()];
    /* TODO: [p2-task2] acquire mutex lock */
    if(spin_lock_try_acquire( &(mlocks[mlock_idx].lock) )){
        spin_lock_acquire( &(mlocks[mlock_idx].lock) );
        mlocks[mlock_idx].owner = current_running->pid;
    }else{//若未获取成功，则直接阻塞
        do_block(&(current_running->list), &(mlocks[mlock_idx].block_queue));
    }
}

void do_mutex_lock_release(int mlock_idx)
{
    /* TODO: [p2-task2] release mutex lock */
    if(list_is_empty( &(mlocks[mlock_idx].block_queue))){
        spin_lock_release(&(mlocks[mlock_idx].lock));
        mlocks[mlock_idx].owner = -1;
    }else{
        list_head * list = mlocks[mlock_idx].block_queue.next;
        pcb_t *next_pcb = list_entry(list, pcb_t, list);
        mlocks[mlock_idx].owner = next_pcb->pid;
        do_unblock(&(mlocks[mlock_idx].block_queue));
    }
}


void init_barriers(void)
{
    int i;
    for(i = 0; i != BARRIER_NUM; ++i)
    {
        init_list_head(&(barriers[i].barrier_queue));
        barriers[i].key = -1;
        // barriers[i].bound -
        barriers[i].barrier_num = -1;
    }
}

int do_barrier_init(int key, int goal)
{
    int i;
    for(i = 0; i != BARRIER_NUM; ++i)
    {
        if(barriers[i].key == -1)
            break;
    }
    if(i == BARRIER_NUM) return -1;// no more barriers available

    barriers[i].key = key;
    barriers[i].barrier_num = goal;
    return i;
}

// 每次申请num--，最后释放的时候就++
void do_barrier_wait(int bar_idx)
{
    pcb_t *current_running = core_running[get_current_cpu_id()];
    if(barriers[bar_idx].barrier_num == 1){// 当最后一个wait时，把队列清空即可
        while(!list_is_empty(&(barriers[bar_idx].barrier_queue))){
            do_unblock(&(barriers[bar_idx].barrier_queue));
            barriers[bar_idx].barrier_num++;
        }// 执行完后，应该正常了
    }else{
        // 这里就是大于1的情况，也就是可以直接block的情况
        current_running->status = TASK_BLOCKED;
        add_queue(&(barriers[bar_idx].barrier_queue), current_running);
        barriers[bar_idx].barrier_num--;
    }
    do_scheduler();
}

// 需不需要考虑鲁棒性呢？暂时假设里面没有剩下的list吧
void do_barrier_destroy(int bar_idx)
{
    if(barriers[bar_idx].key == -1) return;
    
    // 还是加一点鲁棒性吧：如果非空，就直接终结掉
    // while(list_is_empty(&(barriers[bar_idx].barrier_queue)))
    // {
    //     pcb_t * tmp = get_head(&(barriers[bar_idx].barrier_queue));
    //     tmp->status = TASK_EXITED;
    //     do_kill(tmp->pid);
    // }

    barriers[bar_idx].key = -1;
    barriers[bar_idx].barrier_num = -1;

}


void init_conditions(void)
{
    for(int i = 0; i != CONDITION_NUM; ++i)
    {
        init_list_head(&(conditions[i].queue));
        conditions[i].cond_key = -1;
        conditions[i].lock_idx = -1;
        conditions[i].valid= FALSE;
    }
}

int do_condition_init(int key)
{
    int i;
    for(i = 0; i != CONDITION_NUM; ++i)
    {
        if(conditions[i].valid == FALSE)
            break;
    }
    if(i == CONDITION_NUM) return -1;// no more barriers available

    conditions[i].cond_key = key;
    conditions[i].valid = TRUE;
    return i;
}

void do_condition_wait(int cond_idx, int mutex_idx)
{
    pcb_t *current_running = core_running[get_current_cpu_id()];
    current_running->status = TASK_BLOCKED;
    conditions[cond_idx].lock_idx = mutex_idx;
    add_queue(&conditions[cond_idx].queue, current_running);

    do_mutex_lock_release(mutex_idx);

    do_scheduler();
}

void do_condition_signal(int cond_idx)
{
    pcb_t * signaled_pcb = get_head(&(conditions[cond_idx].queue));
    int lock_id = conditions[cond_idx].lock_idx;
    if(spin_lock_try_acquire( &(mlocks[lock_id].lock) )){
        spin_lock_acquire( &(mlocks[lock_id].lock) );
        mlocks[lock_id].owner = signaled_pcb->pid;
    }else{
        do_block(&(signaled_pcb->list), &(mlocks[lock_id].block_queue));
    }
}

void do_condition_broadcast(int cond_idx)
{
    list_head * block_queue = &(conditions[cond_idx].queue);
    int lock_id = conditions[cond_idx].lock_idx;
    // 将block_queue全部释放，加入锁队列或者ready队列
    while(!list_is_empty(block_queue))
    {
        pcb_t * tmp = get_head(block_queue);
        if(spin_lock_try_acquire( &(mlocks[lock_id].lock) )){
            spin_lock_acquire( &(mlocks[lock_id].lock) );
            mlocks[lock_id].owner = tmp->pid;
            add_queue(&ready_queue, tmp);
        }else{
            tmp->status = TASK_BLOCKED;
            add_queue(&(mlocks[lock_id].block_queue), tmp);
        }
    }
    do_scheduler();
}

void do_condition_destroy(int cond_idx)
{
    if(list_is_empty(&(conditions[cond_idx].queue)))
    {
        init_list_head(&(conditions[cond_idx].queue));
        conditions[cond_idx].cond_key = -1;
        conditions[cond_idx].lock_idx = -1;
        conditions[cond_idx].valid= FALSE;
    }

}

void init_mbox()
{
    for(int i = 0; i != MBOX_NUM; ++i)
    {
        init_list_head(&(mboxs[i].full_queue));
        init_list_head(&(mboxs[i].empty_queue));
        init_list_head(&(mboxs[i].lock_queue));
        mboxs[i].valid = FALSE;
        mboxs[i].head = 0;
        mboxs[i].tail = 0;
        mboxs[i].lock.status = UNLOCKED;
        mboxs[i].user_num = 0;
        bzero(mboxs[i].name, MBOX_NAME_LENGTH);
        bzero(mboxs[i].content, MAX_MBOX_LENGTH);
    }
}

// 已无问题
int do_mbox_open(char *name)
{
    int i;
    for(i = 0; i != MBOX_NUM; ++i)
    {
        if(mboxs[i].valid == FALSE || strcmp(name, mboxs[i].name) == 0)
        {
            break;
        }
    }
    if(i == MBOX_NUM) return -1;
    if(mboxs[i].valid == FALSE){
        mboxs[i].valid = TRUE;
    }
    strcpy(mboxs[i].name, name);
    return i;
}

// untested
void do_mbox_close(int mbox_idx)
{
    if(mboxs[mbox_idx].user_num == 0)
        mboxs[mbox_idx].valid = TRUE;
}

// wa
int do_mbox_send(int mbox_idx, void * msg, int msg_length)
{
    int blocked = 0;    
    if(mboxs[mbox_idx].valid == FALSE) return -1;
    // 先 num++
    mboxs[mbox_idx].user_num++;
    pcb_t * current_running;

    // 获取锁
    if(mboxs[mbox_idx].lock.status == LOCKED)
    {// 首先尝试获取锁
        ++blocked;
        current_running = core_running[get_current_cpu_id()];
        current_running->status = TASK_BLOCKED;
        add_queue(&mboxs[mbox_idx].lock_queue, current_running);
        do_scheduler();
    }else{
        mboxs[mbox_idx].lock.status = LOCKED;
    }

    // 进入临界区，先计算余下的长度
    int length = MAX_MBOX_LENGTH - 
            (mboxs[mbox_idx].tail - mboxs[mbox_idx].head);
    
    // 准备发送信息，首先看够不够发吧
    while( msg_length > length)
    {// 则不够发，释放锁
        mboxs[mbox_idx].lock.status = UNLOCKED;
        if(!list_is_empty(&mboxs[mbox_idx].lock_queue))
        {
            do_unblock(&mboxs[mbox_idx].lock_queue);
        }
        // 阻塞进队列，等待空队列出现后获取锁
        ++blocked;
        current_running = core_running[get_current_cpu_id()];
        current_running->status = TASK_BLOCKED;
        add_queue(&mboxs[mbox_idx].empty_queue, current_running);
        do_scheduler();
        // 调度回来时，记得获取锁
        if(mboxs[mbox_idx].lock.status == LOCKED){
            ++blocked;
            current_running = core_running[get_current_cpu_id()];
            current_running->status = TASK_BLOCKED;
            add_queue(&mboxs[mbox_idx].lock_queue, current_running);
            do_scheduler();
        }else{
            mboxs[mbox_idx].lock.status = LOCKED;
        }
        length = MAX_MBOX_LENGTH - 
            (mboxs[mbox_idx].tail - mboxs[mbox_idx].head);
    }
    
    // 此时进程能到这里，必然是可以发的
    // send 只改变tail成员
    for(int i = 0 ; i != msg_length; mboxs[mbox_idx].tail++, ++i)
    {
        int index = mboxs[mbox_idx].tail % MAX_MBOX_LENGTH;
        mboxs[mbox_idx].content[index] = *(((char*)msg) + i);
    }

    // 判满
    int full = 
    (mboxs[mbox_idx].head+MAX_MBOX_LENGTH == mboxs[mbox_idx].tail);
    if(full)
    {// 满则释放full_queue
        while(!list_is_empty(&mboxs[mbox_idx].full_queue))
        {
            do_unblock(&mboxs[mbox_idx].full_queue);
        }
    }

    // 最后结束的时候，记得释放锁一次。
    mboxs[mbox_idx].lock.status = UNLOCKED;
    if(!list_is_empty(&mboxs[mbox_idx].lock_queue))
    {
        do_unblock(&mboxs[mbox_idx].lock_queue);
    }

    mboxs[mbox_idx].user_num--;

    // 没办法，不然卡死了，释放一个recv里的full的吧
    if(!list_is_empty(&mboxs[mbox_idx].full_queue))
    {
        do_unblock(&mboxs[mbox_idx].full_queue);
    }
    
    return blocked;
}

// wa
int do_mbox_recv(int mbox_idx, void * msg, int msg_length)
{
    if(mboxs[mbox_idx].valid == FALSE) return -1;
    // 先 num++
    mboxs[mbox_idx].user_num++;
    pcb_t *current_running;
    int blocked = 0;

    // 获取锁
    if(mboxs[mbox_idx].lock.status == LOCKED)
    {// 首先尝试获取锁
        ++blocked;
        current_running = core_running[get_current_cpu_id()];
        current_running->status = TASK_BLOCKED;
        add_queue(&mboxs[mbox_idx].lock_queue, current_running);
        do_scheduler();
    }else{
        mboxs[mbox_idx].lock.status = LOCKED;
    }

    // 进入临界区，计算已有的数据长度
    int length = mboxs[mbox_idx].tail - mboxs[mbox_idx].head;
    
    // 准备接受信息，看看够不够收
    while( msg_length > length)
    {// 则不够收，释放锁
        mboxs[mbox_idx].lock.status = UNLOCKED;
        if(!list_is_empty(&mboxs[mbox_idx].lock_queue))
        {
            do_unblock(&mboxs[mbox_idx].lock_queue);
        }
        // 阻塞进队列，等待空队列出现后获取锁
        ++blocked;
        current_running = core_running[get_current_cpu_id()];
        current_running->status = TASK_BLOCKED;
        add_queue(&mboxs[mbox_idx].full_queue, current_running);
        do_scheduler();
        // 调度回来时，记得获取锁
        if(mboxs[mbox_idx].lock.status == LOCKED){
            ++blocked;
            current_running = core_running[get_current_cpu_id()];
            current_running->status = TASK_BLOCKED;
            add_queue(&mboxs[mbox_idx].lock_queue, current_running);
            do_scheduler();
        }else{
            mboxs[mbox_idx].lock.status = LOCKED;
        }
        length = mboxs[mbox_idx].tail - mboxs[mbox_idx].head;
    }
    
    // 此时进程能到这里，必然是可以收的
    // send 只改变tail成员
    for(int i = 0 ; i != msg_length; mboxs[mbox_idx].head++, ++i)
    {
        int index = mboxs[mbox_idx].head % MAX_MBOX_LENGTH;
        *(((char*)msg) + i) = mboxs[mbox_idx].content[index];
    }

    // 判空
    int empty = 
    (mboxs[mbox_idx].head == mboxs[mbox_idx].tail);
    if(empty)
    {// 空则释放empty_queue
        while(!list_is_empty(&mboxs[mbox_idx].empty_queue))
        {
            do_unblock(&mboxs[mbox_idx].empty_queue);
        }
    }

    // 最后结束的时候，记得释放锁一次。
    mboxs[mbox_idx].lock.status = UNLOCKED;
    if(!list_is_empty(&mboxs[mbox_idx].lock_queue))
    {
        do_unblock(&mboxs[mbox_idx].lock_queue);
    }

    mboxs[mbox_idx].user_num--;

    // 没办法，不然卡死了，释放一个send里的empty的吧
    if(!list_is_empty(&mboxs[mbox_idx].empty_queue))
    {
        do_unblock(&mboxs[mbox_idx].empty_queue);
    }
    
    return 0;
}


void release_pcb_lock(pid_t pid)
{
    for(int i = 0; i != LOCK_NUM; ++i)
    {
        if(mlocks[i].owner == pid)
            do_mutex_lock_release(i);
    }
}

// void release_barrier(void)
// {
//     for(int i = 0; i != BARRIER_NUM; ++i)
//     {
//         if(barriers[i].barrier_num == 0)
//         {
//             while(!list_is_empty(&(barriers[i].barrier_queue)))
//             {
//                 do_unblock(&(barriers[i].barrier_queue));
//                 barriers[i].barrier_num++;
//             }
//         }
//     }
// }
