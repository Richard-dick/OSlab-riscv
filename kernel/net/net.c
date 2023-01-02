#include <e1000.h>
#include <type.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/list.h>
#include <os/smp.h>
#include <assert.h>

static LIST_HEAD(send_block_queue);
static LIST_HEAD(recv_block_queue);

int do_net_send(void *txpacket, int length)
{
    // TODO: [p5-task1] Transmit one network packet via e1000 device
    // ! 将txpacket转成物理地址后传入
    int send_bytes = 0;
    // uintptr_t phy_txpacket = do_getpa(txpacket);
    // ! 感觉不需要实地址
    // send_bytes = e1000_transmit(txpacket, length);
    
    // TODO: [p5-task3] Call do_block when e1000 transmit queue is full
    do{
        send_bytes = e1000_transmit(txpacket, length);
        if(send_bytes){
            break;
        }else{// 没有发出信息
            // TODO: [p5-task3] Call do_block when there is no packet on the way
            pcb_t *current_running = core_running[get_current_cpu_id()];
            do_block(&(current_running->list), &recv_block_queue);
            // TODO: [p5-task4] Enable TXQE interrupt if transmit queue is full
            // 此时是不可能发更多包了, 于是阻塞, 只到可以发包...其实每次就发一个, 感觉问题不大
            e1000_enable_txqe();
        }
    } while(1);

    // TODO: 可以关掉
    e1000_disable_txqe();

    return send_bytes;  // Bytes it has transmitted
}

int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens)
{
    // TODO: [p5-task2] Receive one network packet via e1000 device
    int recv_bytes = 0;
    int cur_recv = 0;
    // uintptr_t phy_rxbuffer = do_getpa(rxbuffer);
    for(int i = 0; i != pkt_num; ++i)
    {
        // do{
        //     // * 加上已经收到的字节数做偏移, 稳妥
        //     cur_recv = e1000_poll(rxbuffer + recv_bytes);
        // }while(cur_recv == 0); // ! 啥也没收到

        do{
            // * 加上已经收到的字节数做偏移, 稳妥
            cur_recv = e1000_poll(rxbuffer + recv_bytes);
            if(cur_recv){
                break;
            }else{// 没有收到信息
                // TODO: [p5-task3] Call do_block when there is no packet on the way
                pcb_t *current_running = core_running[get_current_cpu_id()];
                do_block(&(current_running->list), &recv_block_queue);
            }
        }while(1); // ! 啥也没收到
        
        // 出while则肯定收到了
        recv_bytes += cur_recv;
        pkt_lens[i] = cur_recv;
    }
    

    return recv_bytes;  // Bytes it has received
}

void check_net_send()
{
    if(!list_is_empty(&send_block_queue) && is_tx_ready())
    {// 准备好了
        do_unblock(&send_block_queue);
    }
}

void check_net_recv()
{
    if(!list_is_empty(&recv_block_queue) && is_rx_ready())
    {// 准备好了
        do_unblock(&recv_block_queue);
    }
}

void net_handle_irq(void)
{
    // TODO: [p5-task4] Handle interrupts from network device

    // ! 读入ICR值, 判断其中断类型
    uint32_t ICR_value = e1000_read_reg(e1000, E1000_ICR) & e1000_read_reg(e1000, E1000_IMS);
    printk("%x\n\r", ICR_value);
    // TODO: 不是互斥性
    if(ICR_value & E1000_ICR_TXQE){
        // 进入TXQE中断, 处理发送
        e1000_handle_txqe();
    }
    if(ICR_value & E1000_ICR_RXDMT0)
    {// 进入接收中断
        e1000_handle_rxdmt0();
    }
}

void e1000_handle_txqe()
{
    assert(!list_is_empty(&send_block_queue));
    if(!list_is_empty(&send_block_queue) && is_tx_ready())
    {// 准备好了
        do_unblock(&send_block_queue);
    }
}
void e1000_handle_rxdmt0()
{
    assert(!list_is_empty(&recv_block_queue));
    if(!list_is_empty(&recv_block_queue) && is_rx_ready())
    {// 准备好了
        do_unblock(&recv_block_queue);
    }
}