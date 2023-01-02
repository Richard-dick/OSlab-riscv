#include <e1000.h>
#include <type.h>
#include <os/string.h>
#include <os/time.h>
#include <assert.h>
#include <pgtable.h>

// E1000 Registers Base Pointer
volatile uint8_t *e1000;  // use virtual memory address

// E1000 Tx & Rx Descriptors
static struct e1000_tx_desc tx_desc_array[TXDESCS] __attribute__((aligned(16)));
static struct e1000_rx_desc rx_desc_array[RXDESCS] __attribute__((aligned(16)));

// E1000 Tx & Rx packet buffer
static char tx_pkt_buffer[TXDESCS][TX_PKT_SIZE];
static char rx_pkt_buffer[RXDESCS][RX_PKT_SIZE];

// Fixed Ethernet MAC Address of E1000
static const uint8_t enetaddr[6] = {0x00, 0x0a, 0x35, 0x00, 0x1e, 0x53};

/**
 * e1000_reset - Reset Tx and Rx Units; mask and clear all interrupts.
 **/
static void e1000_reset(void)
{
	/* Turn off the ethernet interface */
    e1000_write_reg(e1000, E1000_RCTL, 0);
    e1000_write_reg(e1000, E1000_TCTL, 0);

	/* Clear the transmit ring */
    e1000_write_reg(e1000, E1000_TDH, 0);
    e1000_write_reg(e1000, E1000_TDT, 0);

	/* Clear the receive ring */
    e1000_write_reg(e1000, E1000_RDH, 0);
    e1000_write_reg(e1000, E1000_RDT, 0);

	/**
     * Delay to allow any outstanding PCI transactions to complete before
	 * resetting the device
	 */
    latency(1);

	/* Clear interrupt mask to stop board from generating interrupts */
    e1000_write_reg(e1000, E1000_IMC, 0xffffffff);

    /* Clear any pending interrupt events. */
    while (0 != e1000_read_reg(e1000, E1000_ICR)) ;
}

/**
 * e1000_configure_tx - Configure 8254x Transmit Unit after Reset
 **/
static void e1000_configure_tx(void)
{
    /* TODO: [p5-task1] Initialize tx descriptors */
    for(int i = 0; i != TXDESCS; ++i)
    {
        /* TODO: [p5-task1] Set up the Tx descriptor base address and length */
        tx_desc_array[i].addr = kva2pa((uint64_t)tx_pkt_buffer[i]);
        tx_desc_array[i].length = 0;
        tx_desc_array[i].cmd = E1000_TXD_CMD_RS |  E1000_TXD_CMD_EOP;
        tx_desc_array[i].cso = 0;
        tx_desc_array[i].css = 0;
        tx_desc_array[i].special = 0;
        tx_desc_array[i].status = E1000_TXD_STAT_DD; // ! 初始默认全部发送过
    }

	/* TODO: [p5-task1] Set up the HW Tx Head and Tail descriptor pointers */
    uintptr_t tx_array_base = kva2pa((uintptr_t)tx_desc_array);
    e1000_write_reg(e1000, E1000_TDBAL, tx_array_base & 0xffffffff);
    e1000_write_reg(e1000, E1000_TDBAH, tx_array_base >> 32);
    e1000_write_reg(e1000, E1000_TDLEN, sizeof(struct e1000_tx_desc)*TXDESCS);
    e1000_write_reg(e1000, E1000_TDH, 0);
    e1000_write_reg(e1000, E1000_TDT, 0);
    e1000_write_reg(e1000, E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (E1000_TCTL_CT & 0x100) | (E1000_TCTL_COLD & 0x40000));

}


/**
 * e1000_configure_rx - Configure 8254x Receive Unit after Reset
 **/
static void e1000_configure_rx(void)
{
    /* TODO: [p5-task2] Set e1000 MAC Address to RAR[0] */
    uint32_t ral = 0, rah = 0;
    ral = enetaddr[3] << 24 | enetaddr[2] << 16 | enetaddr[1] << 8 | enetaddr[0];
    rah = E1000_RAH_AV | enetaddr[5] << 8 | enetaddr[4];
    e1000_write_reg_array(e1000, E1000_RA, 0, ral);//RAL
    e1000_write_reg_array(e1000, E1000_RA, 1, rah);//RAH
    
    // for(int i = 0; i != 4; ++i) ral |= (enetaddr[i] << (8*i));
    // for(int i = 4; i != 6; ++i) rah |= (enetaddr[i] << (8*(i-4)));
    // rah |= E1000_RAH_AV;
    // e1000_write_reg_array(e1000, E1000_RA, 0, ral);//RAL
    // e1000_write_reg_array(e1000, E1000_RA, 1, rah);//RAH

    /* TODO: [p5-task2] Initialize rx descriptors */
    for(int i = 0; i < RXDESCS; ++i)
    {
        rx_desc_array[i].addr = kva2pa((uint64_t)rx_pkt_buffer[i]);
        // rx_desc_array[i].length = 0;
        // rx_desc_array[i].csum = 0;
        // rx_desc_array[i].errors = 0;
        // rx_desc_array[i].special = 0;
        // rx_desc_array[i].status = 0;
    }

    /* TODO: [p5-task2] Set up the Rx descriptor base address and length */
    uintptr_t rx_desc_base = kva2pa((uintptr_t)rx_desc_array);
    e1000_write_reg(e1000, E1000_RDBAL, rx_desc_base & 0xffffffff);
    e1000_write_reg(e1000, E1000_RDBAH, rx_desc_base >> 32);
    e1000_write_reg(e1000, E1000_RDLEN, sizeof(struct e1000_rx_desc)*RXDESCS);

    /* TODO: [p5-task2] Set up the HW Rx Head and Tail descriptor pointers */
    e1000_write_reg(e1000, E1000_RDH, 0);
    e1000_write_reg(e1000, E1000_RDT, RXDESCS - 1);

    /* TODO: [p5-task2] Program the Receive Control Register */
    e1000_write_reg(e1000, E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM);

    // ! 防止DMA攻击--> 其余为0即可
    local_flush_dcache();

    /* TODO: [p5-task4] Enable RXDMT0 Interrupt */
    e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0);
    // e1000_write_reg(e1000, E1000_RCTL, ); RCTL里面应该就是00了
}

/**
 * e1000_init - Initialize e1000 device and descriptors
 **/
void e1000_init(void)
{
    /* Reset E1000 Tx & Rx Units; mask & clear all interrupts */
    e1000_reset();

    /* Configure E1000 Tx Unit */
    e1000_configure_tx();

    /* Configure E1000 Rx Unit */
    e1000_configure_rx();
}

/**
 * e1000_transmit - Transmit packet through e1000 net device
 * @param txpacket - The buffer address of packet to be transmitted
 * @param length - Length of this packet
 * @return - Number of bytes that are transmitted successfully
 **/
int e1000_transmit(void *txpacket, int length)
{
    /* TODO: [p5-task1] Transmit one packet from txpacket */
    // ! 按约定, 传进来就是物理地址了
    local_flush_dcache();
    uint32_t tail;
    assert(length < TX_PKT_SIZE);

    tail = e1000_read_reg(e1000, E1000_TDT);
    // while(!(tx_desc_array[tail].status & E1000_TXD_STAT_DD))
    // { // ! 如果进入, 则还未准备好
    //     ; // * 则反复循环, 直到这个tail被硬件发送, 即准备好给软件填入使用
    // }
    if(!(tx_desc_array[tail].status & E1000_TXD_STAT_DD))
        return 0; // 如果进入, 返回0, 阻塞队列

    tx_desc_array[tail].length = length;
    tx_desc_array[tail].status &= ~E1000_TXD_STAT_DD;
    tx_desc_array[tail].cmd |= (E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS);
    assert(tx_desc_array[tail].addr == kva2pa(tx_pkt_buffer[tail]));
    memcpy(tx_pkt_buffer[tail], txpacket, length); // 直接使用用户地址
    ++tail;
    e1000_write_reg(e1000, E1000_TDT, tail % TXDESCS);

    local_flush_dcache();
    return length;
}

/**
 * e1000_poll - Receive packet through e1000 net device
 * @param rxbuffer - The address of buffer to store received packet(已经为物理地址!!)
 * @return - Length of received packet
 **/
int e1000_poll(void *rxbuffer)
{
    /* TODO: [p5-task2] Receive one packet and put it into rxbuffer */
    local_flush_dcache();
    uint32_t tail = (e1000_read_reg(e1000, E1000_RDT) + 1) % RXDESCS;
    // uint32_t tail = e1000_read_reg(e1000, E1000_RDT);
    // while(!(rx_desc_array[tail].status & E1000_RXD_STAT_DD))
    // { // ! 如果进入, 则还未准备好
    //     ; // * 则反复循环, 直到这个tail被硬件收到, 即准备好给软件填入使用
    // }

    if(!(rx_desc_array[tail].status & E1000_RXD_STAT_DD))
        return 0; // 如果进入, 返回0, 阻塞队列

    uint32_t length = rx_desc_array[tail].length;

    assert(rx_desc_array[tail].addr == kva2pa(rx_pkt_buffer[tail]));
    
    memcpy(rxbuffer, rx_pkt_buffer[tail], length);
    // rx_desc_array[tail].csum = 0;
    // rx_desc_array[tail].errors = 0;
    // rx_desc_array[tail].special = 0;
    rx_desc_array[tail].status &= ~E1000_RXD_STAT_DD;
    // rx_desc_array[tail].length = 0;
    // ++tail;

    e1000_write_reg(e1000, E1000_RDT, tail % RXDESCS);

    local_flush_dcache();
    return length;
}

void e1000_enable_txqe()
{
    e1000_write_reg(e1000, E1000_IMS, E1000_IMS_TXQE);
}

void e1000_disable_txqe()
{
    e1000_write_reg(e1000, E1000_IMC, E1000_IMC_TXQE);
}

int is_tx_ready(void)
{
    local_flush_dcache();
    uint32_t tail = e1000_read_reg(e1000, E1000_TDT);
    int ready = tx_desc_array[tail].status & E1000_TXD_STAT_DD;
    local_flush_dcache();
    return ready;
}

int is_rx_ready(void)
{
    local_flush_dcache();
    uint32_t tail = (e1000_read_reg(e1000, E1000_RDT) + 1) % RXDESCS;
    int ready = rx_desc_array[tail].status & E1000_RXD_STAT_DD;
    local_flush_dcache();
    return ready;
}