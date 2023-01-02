#include <os/list.h>
#include <os/sched.h>
#include <type.h>

uint64_t time_elapsed = 0;
uint64_t time_base = 0;

uint64_t get_ticks()
{
    __asm__ __volatile__(
        "rdtime %0"
        : "=r"(time_elapsed));
    return time_elapsed;
}

uint64_t get_timer()
{
    return get_ticks() / time_base;
}

uint64_t get_time_base()
{
    return time_base;
}

void latency(uint64_t time)
{
    uint64_t begin_time = get_timer();

    while (get_timer() - begin_time < time);
    return;
}

void check_sleeping(void)
{
    // TODO: [p2-task3] Pick out tasks that should wake up from the sleep queue
    list_node_t * it = sleep_queue.next;
    list_node_t * next;
    uint64_t current_time = get_timer();
    while(it != &sleep_queue)
    {
        pcb_t * sleep_pcb = list_entry(it, pcb_t, list);
        next = it->next;
        if(current_time >= sleep_pcb->wakeup_time)
        {
            sleep_pcb->status = TASK_READY;
            del_queue(it);
            add_queue(&ready_queue, sleep_pcb);
        }
        it = next;
        
    }
}