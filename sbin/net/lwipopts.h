#ifndef _NET_LWIPOPTS_H
#define _NET_LWIPOPTS_H

#define NO_SYS                   1
#define SYS_LIGHTWEIGHT_PROT     0
#define LWIP_NETCONN             0
#define LWIP_SOCKET              0
#define LWIP_IPV4                1
#define LWIP_IPV6                0
#define LWIP_DHCP                0
#define LWIP_RAW                 0
#define IP_REASSEMBLY            0
#define IP_FRAG                  0
#define LWIP_ICMP                1
#define LWIP_UDP                 1
#define LWIP_TCP                 1
#define LWIP_NETIF_LOOPBACK      1
#define LWIP_LOOPBACK_MAX_PBUFS  32
#define LWIP_SUPPORT_CUSTOM_PBUF 1
#define MEM_ALIGNMENT            8
#define MEM_SIZE                 (64 * 1024)
#define TCP_MSS                  1460
#define TCP_WND                  (4 * TCP_MSS)
#define TCP_SND_BUF              (4 * TCP_MSS)
#define TCP_SND_QUEUELEN         16
#define TCP_LISTEN_BACKLOG       1
#define MEMP_NUM_TCP_PCB         16
#define MEMP_NUM_TCP_PCB_LISTEN  16
#define MEMP_NUM_UDP_PCB         16
#define MEMP_NUM_TCP_SEG         64
#define PBUF_POOL_SIZE           32
#define MEMP_NUM_PBUF            32
#define ARP_TABLE_SIZE           8
#define ARP_MAXAGE               600
#define ARP_QUEUEING             1
#define ARP_QUEUE_LEN            8
#define MEMP_NUM_ARP_QUEUE       8

#endif
