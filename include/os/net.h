#ifndef __INCLUDE_NET_H__
#define __INCLUDE_NET_H__

#include <os/list.h>
#include <type.h>

#define PKT_NUM 32

void net_handle_irq(void);
void check_net_recv(void);
void check_net_send(void);
void e1000_handle_txqe();
void e1000_handle_rxdmt0();
int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens);
int do_net_send(void *txpacket, int length);
extern void e1000_enable_txqe();

#endif  // !__INCLUDE_NET_H__