#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <rte_ether.h>

#include "include/stream_gen.h"
#include "include/tcp_header.h"
#include "libnids-1.24/src/nids.h"
#include "libnids-1.24/src/hash.h"
#include "include/string_matcher.h"

/* Packet type */
#define CONTROL_PKT		1  // Packets in relation to connection
#define DATA_PKT		2


#ifdef SEND_THREAD
static int                  concur_per_thread;
#endif

char src_ip_addr[16];  //IPv4 address	
char dst_ip_addr[16];

uint8_t src_mac[6] = {0x90, 0xe2, 0xba, 0x14, 0xbe, 0xae}; //b0
uint8_t dst_mac[6] = {0x00, 0x30, 0x48, 0xf7, 0xc3, 0x4a}; //b1

static int pkt_type_cnt = 0;

/* Description: Get packet type
 *
 * @param upper_bound	Upper bound of fragment rate
 * @return
 * 		FRAGMENT_PKT	1
 * 		COMPLETE_PKT	2
 * */
static int
get_pkt_type(const int upper_bound)
{
	pkt_type_cnt += 1;
	if (pkt_type_cnt <= upper_bound) {
		return FRAGMENT_PKT;
	} else if (pkt_type_cnt <= MAX_RATE_BOUND) {
		return COMPLETE_PKT;
	} else {
		pkt_type_cnt = 0;
		return COMPLETE_PKT;
	}
}


#define US_TO_TSC(t) ((rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S ) * (t)
/* * 
 * Description  : delay for a while (used for set a sending interval)
 * */
static void
burst_delay(uint16_t t, int id) 
{
    uint64_t cur_tsc = 0, diff_tsc;
    uint64_t drain_tsc;

    drain_tsc = US_TO_TSC(t);
    while (!force_quit) {     
        cur_tsc = rte_rdtsc();
		diff_tsc = cur_tsc - th_info[id].pre_tsc;
        if (unlikely(diff_tsc >= drain_tsc)) {
            break;
        }
    }
    th_info[id].pre_tsc = cur_tsc;
}

#ifdef OOO_SEND
/* Shuffle for out-of-order support 
 * */
static void 
shuffle_mbufs(int id)
{
    int size = th_info[id].tx_mbufs.len;
    int i, t;
    struct rte_mbuf *tmp;
    struct rte_mbuf **mbufs = th_info[id].tx_mbufs.m_table;

    for (i = 0; i < size; ++i) {
        t = rand() % size;
        tmp = mbufs[t];
        mbufs[t] = mbufs[i];
        mbufs[i] = tmp; 
    }    
}
#endif

/* *
 * Description  : transmit packet with DPDK tx burst
 *
 * @param p		send port
 * @param q		send queue
 * @param id 	thread id
 * @flag		packet type
 * 				1 for Packets in relation to connection
 * 				2 for Packets in relation to data transmission
 * */
static void
dpdk_send_burst(uint8_t p, uint16_t q, int id, int flag)
{
    uint32_t nb_tx, cnt, retry;

    cnt = th_info[id].tx_mbufs.len;

#ifdef OOO_SEND
	if (flag != CONTROL_PKT)
		shuffle_mbufs(id);
#endif

    nb_tx = rte_eth_tx_burst(p, q, th_info[id].tx_mbufs.m_table, cnt);  //tx_rings = 1, main.c

    /* retry while sending failed */
    if (unlikely (nb_tx < cnt)) {
        retry = PKT_RETRY_COUNT;
        while (nb_tx < cnt && (retry--)) {
            rte_delay_us(TX_DELAY_TIME);
            nb_tx += rte_eth_tx_burst(p, q, &th_info[id].tx_mbufs.m_table[nb_tx], cnt - nb_tx);
        }
    }
    th_info[id].stats.tx += nb_tx;
    /* free unsent packets */
    if (unlikely (nb_tx < cnt)) {
        th_info[id].stats.dropped += (cnt - nb_tx);
        do {
            rte_pktmbuf_free(th_info[id].tx_mbufs.m_table[nb_tx]);
        } while(++nb_tx < cnt);
    }
}

/* Description  : flush packets remain in mbufs when exit application */
void
dpdk_tx_flush(void)
{
    int i;
    for (i = 0; i < nb_snd_thread; i++ ) {
        if(th_info[i].tx_mbufs.len > 0)
            dpdk_send_burst(snd_port, i, i, 1);
    }
}

/* *
 * Description  : send packets in tx buffer with DPDK
 * */
static inline int
dpdk_send_pkt(uint8_t *pkt, int len, uint8_t p, uint16_t q, int id, int flag)
{
    struct rte_mbuf   *m;
    uint32_t ret;

    /* allocate rte_mbuf */
    m  = rte_pktmbuf_alloc(th_info[id].mbuf_pool);
    if (unlikely(m == NULL)) {
        printf("allocate mbuf failed.\n");
        return -1;
    }
    rte_memcpy((uint8_t *)((uint8_t *)m->buf_addr + m->data_off), (uint8_t *)pkt, len);
    m->pkt_len  = len;
    m->data_len = len;

    /* Add packet to the TX list. */
    rte_prefetch0(rte_pktmbuf_mtod(m, void *));
    th_info[id].tx_mbufs.m_table[th_info[id].tx_mbufs.len++] = m;

    /* transmit while reaching tx_burst */
#ifdef OOO_SEND
    if ((flag == CONTROL_PKT && th_info[id].tx_mbufs.len >= 1 ) || 
		 th_info[id].tx_mbufs.len >= burst) {
#else
    if (th_info[id].tx_mbufs.len >= burst) {
#endif
        /* sending interval*/
        burst_delay(10, id);        
        dpdk_send_burst(p, q, id, flag);
        /* update size of th_info[id].tx_mbufs.*/
        th_info[id].tx_mbufs.len = 0;
    }

    return 1;  
}

/* Description 	: setting common fields for the same stream
 *              4-tuple, identifier, seq, ack
 * */
static inline void 
set_field(struct buf_node* node)
{
#ifdef ORIGINAL_TUPLE4
    node->saddr = node->tup.saddr;
    node->daddr = node->tup.daddr;
    node->sport = node->tup.source;
    node->dport = node->tup.dest;
	//printf("%u:%u, %u:%u\n", node->tup.saddr, node->tup.source, node->tup.daddr,node->tup.dest);
#else
    uint8_t adr_3 = (uint8_t) libnet_get_prand(LIBNET_PR8); /*  0~255 */
	uint8_t adr_4 = (uint8_t) libnet_get_prand(LIBNET_PR8); /*  0~255 */

	sprintf(src_ip_addr, "10.0.%u.%u", adr_3, adr_4);	
	sprintf(dst_ip_addr, "10.1.%u.%u", adr_4, adr_3);	

    node->saddr = inet_addr(src_ip_addr);
	node->daddr = inet_addr(dst_ip_addr);

	node->sport = (uint16_t) libnet_get_prand(LIBNET_PRu16);
	node->dport = (uint16_t) libnet_get_prand(LIBNET_PRu16);
#endif

	node->id = (uint16_t) (libnet_get_prand(LIBNET_PR16) % 32768);
    node->rcv_id = 0;
	
	/* sequence number 
	 * acknowledge number
	 * give a random 32-bit number for initialization temporarily*/
	node->seq = (uint32_t) (libnet_get_prand(LIBNET_PR32) % 100000000 + 10000000); 
	node->ack_seq = (uint32_t) (libnet_get_prand(LIBNET_PR32) % 100000000 + 10000000); 
    /* initialize TCP timestamp (see 'set_start_ts' for assignment)*/
    node->ts = 0;
    node->ts_peer = 0;

	node->state = TCP_ST_CLOSED;
	node->offset = 0;
}

void 
prepare_header(int id) 
{
	struct ethhdr *eth = (struct ethhdr *) th_info[id].pkt;
    int i;

	for (i = 0; i < 6; i++) {
		th_info[id].pkt[i] = dst_mac[i];
	}   

	if (syn_flood_set) {
	// While simulating SYN flooding, set packets' source MAC address with MAC address of snd_port
		struct ether_addr port_addr;
		rte_eth_macaddr_get(snd_port, &port_addr);
		ether_addr_copy(&port_addr, (struct ether_addr *)&th_info[id].pkt[6]);
	} else {
		for (i = 0; i < 6; i++)
			th_info[id].pkt[i + 6] = src_mac[i];
	}
	eth->h_proto = htons(0x0800); /* IP */

    /* set iphdr pointer */
    th_info[id].iph = (struct iphdr *)(eth + 1);
    if (!th_info[id].iph) {
        fprintf (stderr, "initialize iphdr failed.\n");
        return;
    }
     /* Fill in the IP Header */
    th_info[id].iph->ihl = 5;
    th_info[id].iph->version = 4;
    th_info[id].iph->tos = 0;
    th_info[id].iph->id = htons(54321); //Id of this packet
    th_info[id].iph->frag_off = htons(0x4000);
    th_info[id].iph->ttl = 64;

	th_info[id].iph->saddr = inet_addr("10.0.0.67");
	th_info[id].iph->daddr = inet_addr("10.0.0.68");
	th_info[id].iph->protocol = IPPROTO_TCP;
    
    /* set tcphdr pointer */
    th_info[id].tcph = (struct tcphdr *)(th_info[id].iph + 1);
    if (!th_info[id].tcph) {
        printf ("initialize tcphdr failed.\n");
        return;
    }
    /* Fill in TCP Header */
    th_info[id].tcph->source = htons(46930);
    th_info[id].tcph->dest = htons(50001);
    th_info[id].tcph->seq = 0;
    th_info[id].tcph->ack_seq = 0;
    
    th_info[id].tcph->res1 = 0;
	th_info[id].tcph->doff = 5;  //tcp header size/* need updating */
    th_info[id].tcph->fin = 0;
    th_info[id].tcph->syn = 0;
    th_info[id].tcph->rst = 0;
    th_info[id].tcph->psh = 1;
    th_info[id].tcph->ack = 1;
    th_info[id].tcph->urg = 0;
    th_info[id].tcph->res2 = 0;
    th_info[id].tcph->window = htons(65535); /* maximum allowed window size */
    th_info[id].tcph->check = 0; //leave checksum 0 now, filled later by pseudo header
    th_info[id].tcph->urg_ptr = 0;
}

static inline uint16_t
cal_opt_len(uint8_t flags)
{
	uint16_t optlen = 0;

	if (flags & TCP_FLAG_SYN) {
		optlen += TCP_OPT_MSS_LEN;
#if TCP_OPT_SACK_ENABLED
		optlen += TCP_OPT_SACK_PERMIT_LEN;
#if !TCP_OPT_TIMESTAMP_ENABLED
		optlen += 2;	// insert NOP padding
#endif /* TCP_OPT_TIMESTAMP_ENABLED */
#endif /* TCP_OPT_SACK_ENABLED */

#if TCP_OPT_TIMESTAMP_ENABLED
		optlen += TCP_OPT_TIMESTAMP_LEN;
#if !TCP_OPT_SACK_ENABLED
		optlen += 2;	// insert NOP padding
#endif /* TCP_OPT_SACK_ENABLED */
#endif /* TCP_OPT_TIMESTAMP_ENABLED */

		optlen += TCP_OPT_WSCALE_LEN + 1;

	} else {

#if TCP_OPT_TIMESTAMP_ENABLED
		optlen += TCP_OPT_TIMESTAMP_LEN + 2;
#endif

#if TCP_OPT_SACK_ENABLED
		if (flags & TCP_FLAG_SACK) {
			optlen += TCP_OPT_SACK_LEN + 2;
		}
#endif
	}

	if (optlen % 4 != 0) {
        printf("optlen error.\n");
        return 0;
    }

	return optlen;
}

static inline void
generate_timestamp(uint8_t *tcpopt, uint32_t cur_ts, uint32_t ts_recent)
{
	uint32_t *ts = (uint32_t *)(tcpopt + 2);

	tcpopt[0] = TCP_OPT_TIMESTAMP;
	tcpopt[1] = TCP_OPT_TIMESTAMP_LEN;
	ts[0] = htonl(cur_ts);
	ts[1] = htonl(ts_recent);
}

/* * 
 * Descripton   : generate TCP options 
 * */
static inline void
generate_opt(uint32_t cur_ts, uint8_t flags, uint8_t *tcpopt, uint16_t optlen, uint32_t ts_recent)
{
    int i = 0;

	if (flags & TCP_FLAG_SYN) {
		uint16_t mss;

		/* MSS option */
		mss = TCP_DEFAULT_MSS;
		tcpopt[i++] = TCP_OPT_MSS;
		tcpopt[i++] = TCP_OPT_MSS_LEN;
		tcpopt[i++] = mss >> 8;
		tcpopt[i++] = mss % 256;

		/* SACK permit */
#if TCP_OPT_SACK_ENABLED
#if !TCP_OPT_TIMESTAMP_ENABLED
		tcpopt[i++] = TCP_OPT_NOP;
		tcpopt[i++] = TCP_OPT_NOP;
#endif /* TCP_OPT_TIMESTAMP_ENABLED */
		tcpopt[i++] = TCP_OPT_SACK_PERMIT;
		tcpopt[i++] = TCP_OPT_SACK_PERMIT_LEN;
#endif /* TCP_OPT_SACK_ENABLED */

		/* Timestamp */
#if TCP_OPT_TIMESTAMP_ENABLED
#if !TCP_OPT_SACK_ENABLED
		tcpopt[i++] = TCP_OPT_NOP;
		tcpopt[i++] = TCP_OPT_NOP;
#endif /* TCP_OPT_SACK_ENABLED */
		generate_timestamp(tcpopt + i, cur_ts, ts_recent);
		i += TCP_OPT_TIMESTAMP_LEN;
#endif /* TCP_OPT_TIMESTAMP_ENABLED */

		/* Window scale */
		tcpopt[i++] = TCP_OPT_NOP;
		tcpopt[i++] = TCP_OPT_WSCALE;
		tcpopt[i++] = TCP_OPT_WSCALE_LEN;
		tcpopt[i++] = TCP_DEFAULT_WSCALE;

	} else {

#if TCP_OPT_TIMESTAMP_ENABLED
		tcpopt[i++] = TCP_OPT_NOP;
		tcpopt[i++] = TCP_OPT_NOP;
		generate_timestamp(tcpopt + i, cur_ts, ts_recent);
		i += TCP_OPT_TIMESTAMP_LEN;
#endif

#if TCP_OPT_SACK_ENABLED
		if (flags & TCP_OPT_SACK) {
			// TODO: implement SACK support
		}
#endif
	}

	if (i != optlen) {
		printf("denerate TCP options: length error\n");
	}
}

static void
set_start_ts(struct buf_node *node)
{
    struct timeval cur_ts = {0};
    /* TCP timestamp */
    gettimeofday(&cur_ts, NULL);
    node->ts = TIMEVAL_TO_TS(&cur_ts);
    node->ts_peer = node->ts + 1234; //pseudo timestamp
}

/* Description	: send packets for establishing connection
 * 			 	  SYN, SYN/ACK, ACK;
 * */
static inline void
send_syn(struct buf_node* node, uint8_t p, uint16_t q, int id)
{
    uint16_t    optlen = 0;
    uint32_t    ts_recent;

    if (node->state == TCP_ST_CLOSED) {
        /* SYN   '->' */
        optlen = cal_opt_len(TCP_FLAG_SYN);
        set_start_ts(node);

        th_info[id].iph->id = htons(node->id);
        th_info[id].iph->tot_len = htons(IP_HEADER_LEN + TCP_HEADER_LEN + optlen);
        th_info[id].iph->saddr = node->saddr;
        th_info[id].iph->daddr = node->daddr;
        th_info[id].iph->check = ip_checksum( (struct iphdr *)th_info[id].iph);	
        
        ts_recent = 0;
        generate_opt(node->ts, TCP_FLAG_SYN, (uint8_t *)th_info[id].tcph + TCP_HEADER_LEN, optlen, ts_recent);
        
        th_info[id].tcph->source = htons(node->sport);
        th_info[id].tcph->dest = htons(node->dport);
        th_info[id].tcph->seq = htonl(node->seq);
        th_info[id].tcph->ack_seq = htonl(0);
        th_info[id].tcph->doff = (TCP_HEADER_LEN + optlen) >> 2;
        th_info[id].tcph->fin = 0;
        th_info[id].tcph->syn = 1;         // SYN
        th_info[id].tcph->psh = 0;
        th_info[id].tcph->ack = 0;
        th_info[id].tcph->check = tcp_checksum((struct iphdr*)th_info[id].iph, (struct tcphdr*)th_info[id].tcph);	

        dpdk_send_pkt((uint8_t *)th_info[id].pkt, HEADER_LEN + optlen, p, q, id, 1);
        node->state = TCP_ST_SYN_SENT;
    } else if(node->state == TCP_ST_SYN_SENT) {
        /* syn / ack   ' <- '*/
#if 0
        int         i;
        for (i = 0; i < 6; i++) {
            th_info[id].pkt[i] = src_mac[i];
            th_info[id].pkt[i + 6] = dst_mac[i];
        }
#endif
        optlen = cal_opt_len(TCP_FLAG_SYN | TCP_FLAG_ACK);
        th_info[id].iph->id = htons(node->rcv_id++);     //id = 0
        th_info[id].iph->tot_len = htons(IP_HEADER_LEN + TCP_HEADER_LEN + optlen);
        th_info[id].iph->saddr = node->daddr; //exchange src/dst ip
        th_info[id].iph->daddr = node->saddr;
        th_info[id].iph->check = ip_checksum((struct iphdr *)th_info[id].iph);	
        
        ts_recent = node->ts;
        generate_opt(node->ts_peer, TCP_FLAG_SYN | TCP_FLAG_ACK, (uint8_t *)th_info[id].tcph + TCP_HEADER_LEN, optlen, ts_recent);
        
        th_info[id].tcph->source = htons(node->dport);       // exchange src/dst port
        th_info[id].tcph->dest = htons(node->sport);
        node->ack_seq = (uint32_t)(libnet_get_prand(LIBNET_PR32) % 100000000) + 10000000 ; 
        th_info[id].tcph->seq = htonl(node->ack_seq - 1);
        th_info[id].tcph->ack_seq = htonl(node->seq+1);
        th_info[id].tcph->doff = (TCP_HEADER_LEN + optlen) >> 2;
        th_info[id].tcph->fin = 0;
        th_info[id].tcph->syn = 1;           // SYN
        th_info[id].tcph->psh = 0;
        th_info[id].tcph->ack = 1;           // ACK
        th_info[id].tcph->check = tcp_checksum((struct iphdr*)th_info[id].iph, (struct tcphdr*)th_info[id].tcph);	

        dpdk_send_pkt((uint8_t *)th_info[id].pkt, HEADER_LEN + optlen, p, q, id, 1);
        node->state = TCP_ST_SYN_RCVD;
    } else if(node->state == TCP_ST_SYN_RCVD) {
        /* ACK    '->' */
#if 0
        int         i;
        for (i = 0; i < 6; i++) {
            th_info[id].pkt[i] = dst_mac[i];
            th_info[id].pkt[i + 6] = src_mac[i];
        }
#endif
        optlen = cal_opt_len(TCP_FLAG_ACK);
        node->id++;
        th_info[id].iph->id = htons(node->id);
        th_info[id].iph->tot_len = htons(IP_HEADER_LEN + TCP_HEADER_LEN + optlen);
        th_info[id].iph->saddr = node->saddr; //exchange src/dst ip
        th_info[id].iph->daddr = node->daddr;
        th_info[id].iph->check = ip_checksum((struct iphdr *)th_info[id].iph);	
        
        ts_recent = node->ts_peer;
        generate_opt(node->ts, TCP_FLAG_ACK, (uint8_t *)th_info[id].tcph + TCP_HEADER_LEN, optlen, ts_recent);
        
        th_info[id].tcph->source = htons(node->sport);       // exchange src/dst port
        th_info[id].tcph->dest = htons(node->dport);
        node->seq++;
        th_info[id].tcph->seq = htonl(node->seq); 
        th_info[id].tcph->ack_seq = htonl(node->ack_seq);
        th_info[id].tcph->doff = (TCP_HEADER_LEN + optlen) >> 2;
        th_info[id].tcph->fin = 0;
        th_info[id].tcph->syn = 0;
        th_info[id].tcph->psh = 0;
        th_info[id].tcph->ack = 1;           // ACK
        th_info[id].tcph->check = tcp_checksum((struct iphdr*)th_info[id].iph, (struct tcphdr*)th_info[id].tcph);	

        dpdk_send_pkt((uint8_t *)th_info[id].pkt, HEADER_LEN + optlen, p, q, id, 1);

        th_info[id].tcph->psh = 1;
        node->id++;
        node->state =  TCP_ST_ESTABLISHED; 
    } else {
        printf("Got TCP state fault when establishing stream.\n");
    }
}

/* Description: hash according to 4-tuple */
static inline int
hash_index(struct tuple4 addr)
{
  int hash=mkhash(addr.saddr, addr.source, addr.daddr, addr.dest);
  return hash % nids_params.n_tcp_streams;
}

/* Description	: send packets for closing connection
 * 			 	  FIN, ACK, FIN, ACK;
 * @ q          : tx_queue id
 * */
static inline void
send_fin(struct buf_node* node, uint8_t p, uint16_t q, int id)
{
    int         optlen = 0;
    uint32_t    ts_recent;

    if (node->state == TCP_ST_CLOSING) {
        /* FIN, ACK   '->' */
        optlen = cal_opt_len(TCP_FLAG_FIN |  TCP_FLAG_ACK);
        th_info[id].iph->id = htons(node->id);
        th_info[id].iph->tot_len = htons(IP_HEADER_LEN + TCP_HEADER_LEN + optlen);
        th_info[id].iph->saddr = node->saddr; //exchange src/dst ip
        th_info[id].iph->daddr = node->daddr;
        th_info[id].iph->check = ip_checksum((struct iphdr *)th_info[id].iph);	
        
        ts_recent = node->ts_peer++;
        generate_opt(++node->ts, TCP_FLAG_FIN | TCP_FLAG_ACK, (uint8_t *)th_info[id].tcph + TCP_HEADER_LEN, optlen, ts_recent);

        th_info[id].tcph->source = htons(node->sport);
        th_info[id].tcph->dest = htons(node->dport);
        th_info[id].tcph->seq = htonl(node->seq);  //same as previous packet
        th_info[id].tcph->ack_seq = htonl(node->ack_seq);
        th_info[id].tcph->doff = (TCP_HEADER_LEN + optlen) >> 2;
        th_info[id].tcph->fin = 1;         // FIN
        th_info[id].tcph->syn = 0;
        th_info[id].tcph->psh = 0;
        th_info[id].tcph->ack = 1;
        th_info[id].tcph->check = tcp_checksum((struct iphdr*)th_info[id].iph, (struct tcphdr*)th_info[id].tcph);	

        dpdk_send_pkt((uint8_t *)th_info[id].pkt, HEADER_LEN + optlen, p, q, id, 1);
        node->state = TCP_ST_FIN_SENT_1; 
    } else if (node->state == TCP_ST_FIN_SENT_1){
        /* fin, ack   ' <- '*/
#if 0
        int         i;
        for (i = 0; i < 6; i++) {
            th_info[id].pkt[i] = src_mac[i];
            th_info[id].pkt[i + 6] = dst_mac[i];
        }
#endif
        optlen = cal_opt_len(TCP_FLAG_FIN |  TCP_FLAG_ACK);
        th_info[id].iph->id = htons(node->rcv_id++);     //id = 1, temp!!!
        th_info[id].iph->tot_len = htons(IP_HEADER_LEN + TCP_HEADER_LEN + optlen);
        th_info[id].iph->saddr = node->daddr; //exchange src/dst ip
        th_info[id].iph->daddr = node->saddr;
        th_info[id].iph->check = ip_checksum((struct iphdr *)th_info[id].iph);	
        
        ts_recent = node->ts++;
        generate_opt(node->ts_peer, TCP_FLAG_FIN | TCP_FLAG_ACK, (uint8_t *)th_info[id].tcph + TCP_HEADER_LEN, optlen, ts_recent);
        
        th_info[id].tcph->source = htons(node->dport);       // exchange src/dst port
        th_info[id].tcph->dest = htons(node->sport);
        th_info[id].tcph->seq = htonl(node->ack_seq); //temp !!!
        th_info[id].tcph->ack_seq = htonl(node->seq + 1);
        th_info[id].tcph->doff = (TCP_HEADER_LEN + optlen) >> 2;
        th_info[id].tcph->fin = 1;           // FIN
        th_info[id].tcph->syn = 0;
        th_info[id].tcph->psh = 0;
        th_info[id].tcph->ack = 1;           // ACK
        th_info[id].tcph->check = tcp_checksum((struct iphdr*)th_info[id].iph, (struct tcphdr*)th_info[id].tcph);	

        dpdk_send_pkt((uint8_t *)th_info[id].pkt, HEADER_LEN + optlen, p, q, id, 1);
        node->state = TCP_ST_FIN_SENT_2;    
    } else if(node->state == TCP_ST_FIN_SENT_2) {
        /* ACK    '->' */
#if 0
        int         i;
        for (i = 0; i < 6; i++) {
            th_info[id].pkt[i] = dst_mac[i];
            th_info[id].pkt[i + 6] = src_mac[i];
        }
#endif
        optlen = cal_opt_len(TCP_FLAG_ACK);
        node->id++;
        th_info[id].iph->id = htons(node->id);
        th_info[id].iph->tot_len = htons(IP_HEADER_LEN + TCP_HEADER_LEN + optlen);
        th_info[id].iph->saddr = node->saddr; //exchange src/dst ip
        th_info[id].iph->daddr = node->daddr;
        th_info[id].iph->check = ip_checksum((struct iphdr *)th_info[id].iph);	
        
        ts_recent = node->ts_peer;
        generate_opt(node->ts, TCP_FLAG_ACK, (uint8_t *)th_info[id].tcph + TCP_HEADER_LEN, optlen, ts_recent);
        
        th_info[id].tcph->source = htons(node->sport);       // exchange src/dst port
        th_info[id].tcph->dest = htons(node->dport);
        th_info[id].tcph->seq = htonl(node->seq + 1); 
        th_info[id].tcph->ack_seq = htonl(node->ack_seq + 1);    //temp !!!
        th_info[id].tcph->doff = (TCP_HEADER_LEN + optlen) >> 2;
        th_info[id].tcph->fin = 0;          
        th_info[id].tcph->syn = 0;
        th_info[id].tcph->psh = 0;
        th_info[id].tcph->ack = 1;           // ACK
        th_info[id].tcph->check = tcp_checksum((struct iphdr*)th_info[id].iph, (struct tcphdr*)th_info[id].tcph);	

        dpdk_send_pkt((uint8_t *)th_info[id].pkt, HEADER_LEN + optlen, p, q, id, 1);
        
#ifdef NON_REUSE
        list_delete_entry(&node->list);
        free(node);
        return;
#endif

        /* reset header fields */
        set_field(node);
		/* To cover as many stream data as possible, 
		 * some random action was executed here.*/
#ifndef SEND_THREAD
        /* For single thread mode,
         * 1. remove the node from link list
         * 2. reinsert the node into a different location of link list
         * */
        list_delete_entry(&node->list);
        /* Used randomly generated tuple4(initialized in insert_buf_node()), or rand() % size */ 
        struct tuple4 tup;
        tup.saddr = ntohl(node->saddr);
        tup.daddr = ntohl(node->daddr);
        tup.source = node->sport;
        tup.dest = node->dport;
		struct list_head *buf_list_t = &hash_buf.buf_list[hash_index(tup)];

        list_add_head(&node->list, buf_list_t);
#else
        /* For multi-thread mode, 
		 * Exchange stream data held in current buf_node with data in another raw random buf_node
		 * */
		int diff = nb_stream - nb_concur - 2;
		if (diff > 10) {
			int new_ind = rand() % diff + nb_concur + 1;
 			uint8_t *tmp_buf = node->tot_buf;
			int tmp_len = node->len;

			node->tot_buf = th_info[id].nodes[new_ind]->tot_buf;
			node->len = th_info[id].nodes[new_ind]->len;
			th_info[id].nodes[new_ind]->tot_buf = tmp_buf;
			th_info[id].nodes[new_ind]->len = tmp_len;
		}        
#endif
    } else {
        printf("Got TCP state fault when ending stream.\n");
    }
}
/* Description	: initialize hash table: hash_buf */
void 
init_hash_buf(void)
{
	int i;
	memset(&hash_buf, 0, sizeof(struct hash_table));

	for (i = 0; i < MAX_HASH_TABLE_SIZE; i++) {
		init_list_head(&hash_buf.buf_list[i]);
	}
    hash_buf.thread = 0;
	pthread_mutex_init(&hash_buf.lock, NULL);
}

/* free hash table */
void
destroy_hash_buf(void)
{
	int i;
    int size = nids_params.n_tcp_streams;
	pthread_mutex_lock(&hash_buf.lock);
	
	for(i = 0; i < size; i++) {
		struct list_head *head = &hash_buf.buf_list[i];
		struct buf_node *buf_entry, *q;
        list_for_each_entry_safe(buf_entry, q, head, list) {
			list_delete_entry(&buf_entry->list);
            free(buf_entry);
        } 
	}
	pthread_mutex_unlock(&hash_buf.lock);
}

/*
 * Description	: compare two 4-tuple
 * Return 		: 0, if not equal; 1, if equal
 * */
static int
is_tuple4_equal (struct tuple4 pre, struct tuple4 cur)
{
    if (pre.saddr == cur.saddr && pre.daddr == cur.daddr
        && pre.source == cur.source && pre.dest == cur.dest) {  
		return 1;
	} else {
		return 0;
	}
}

/* Description	: get buf_node from hash table 
 * Return 		: buf_entry, if get satisfied buf_node;
 * 				  NULL, if has no satisfied buf_node.
 * */
static struct buf_node *
get_buf_node(struct tuple4 tup, int ind)
{
	struct list_head *buf_list_t = &hash_buf.buf_list[ind];
	struct buf_node *buf_entry = NULL;

	list_for_each_entry(buf_entry, buf_list_t, list) {
		if (is_tuple4_equal(tup, buf_entry->tup)) //or use memcmp.
			return buf_entry;
	}
	
	return NULL;
}

/* Description 	: insert buf_node to hash table  
 * @ buf		: data of TCP stream
 * @ length		: length of buf
 * @ tup		: 4-tuple of the TCP stream
 * */
static struct buf_node *
insert_buf_node(struct list_head *buf_list, uint8_t *buf, int length, struct tuple4 tup) 
{
    int size_alloc = MAX_BUFFER_SIZE;
	struct buf_node *buf_entry = malloc(sizeof(struct buf_node));

    /* Used for 'copy_stream_data()' */
    if (length > size_alloc) {
        size_alloc = length + 1;
    }

	buf_entry->tot_buf = (uint8_t *)malloc(size_alloc);

	if(buf_entry == NULL || buf_entry->tot_buf == NULL) {
		fprintf(stderr, "Allocate memory for buf_node failed.\n");
		exit(1);
	}
	buf_entry->tup = tup;
	memcpy(buf_entry->tot_buf, buf, length);
	buf_entry->len = length;
    buf_entry->offset = 0;
    buf_entry->state = TCP_ST_CLOSED;

    /* Initialize other members of buf_entry */
    set_field(buf_entry);
	
    /* lock to be safe */
//	pthread_mutex_lock(&hash_buf.lock);
	list_add_tail(&buf_entry->list, buf_list);
//	pthread_mutex_unlock(&hash_buf.lock);

	return buf_entry;
}

/* Generating ACK correspond to PSH/ACK packet sent with send_data_packet */
static void
send_ack(struct buf_node *node, uint8_t p, uint16_t q, int id)
{
	int         optlen = 0;
    uint32_t    ts_recent;

    optlen = cal_opt_len(TCP_FLAG_ACK);
	th_info[id].iph->id = htons(node->rcv_id++);    
	th_info[id].iph->tot_len = htons(IP_HEADER_LEN + TCP_HEADER_LEN + optlen);
    th_info[id].iph->saddr = node->daddr;
    th_info[id].iph->daddr = node->saddr;
    th_info[id].iph->check = ip_checksum((struct iphdr *)th_info[id].iph);	
	
	ts_recent = node->ts;
    generate_opt(++(node->ts_peer), TCP_FLAG_ACK, (uint8_t *)th_info[id].tcph + TCP_HEADER_LEN, optlen, ts_recent);
    
    th_info[id].tcph->source = htons(node->dport);
    th_info[id].tcph->dest = htons(node->sport);
	th_info[id].tcph->seq = htonl(node->ack_seq);
	th_info[id].tcph->ack_seq = htonl(node->seq);
    th_info[id].tcph->doff = (TCP_HEADER_LEN + optlen) >> 2;
    th_info[id].tcph->syn = 0;
    th_info[id].tcph->fin = 0;
    th_info[id].tcph->ack = 1;
    th_info[id].tcph->psh = 0;
	th_info[id].tcph->check = tcp_checksum((struct iphdr*)th_info[id].iph, (struct tcphdr*)th_info[id].tcph);	

    dpdk_send_pkt((uint8_t *)th_info[id].pkt, HEADER_LEN + optlen, p, q, id, 2);
}

#ifdef DUMP_PAYLOAD
static void
dump_data(struct buf_node* node, int len)
{
    char file_name[30];
    char tup_str[50];
    char src_addr[10];
    char dst_addr[10];
    struct in_addr addr_s, addr_d;

    addr_s.s_addr = node->saddr;
    addr_d.s_addr = node->daddr;

    strcpy(src_addr, inet_ntoa(addr_s));
    strcpy(dst_addr, inet_ntoa(addr_d));

    sprintf(tup_str, "%s:%d-%s:%d",
            src_addr, node->sport,
            dst_addr, node->dport);
    sprintf(file_name, "files/file_%s", tup_str);
    FILE *file_fd = fopen(file_name, "a+");
    if (!file_fd) {
        perror("fopen");
        return;
    }
	fwrite("\n--------\n", sizeof(char), 10, file_fd);
    fwrite((const char *)((uint8_t *)node->tot_buf + node->offset), sizeof(char), len, file_fd);
	fwrite("\n--------\n", sizeof(char), 10, file_fd);
    fclose(file_fd);
}

#endif //DUMP_PAYLOAD

/* Description  : encapsulate data with headers, and send crafted packets out 
 * @ node       : buf_node which contains data to send
 * @ offset     : offset of the sending data
 * @ size       : length of data to send
 * @ p          : network interface
 * @ q          : sending queue of interface
 * */
static void
send_data_pkt(struct buf_node *node, uint32_t length, uint8_t p, uint16_t q, int id)
{
	int         optlen = 0;
	int         payload_offset = 0;
    uint32_t    ts_recent;

    uint32_t    rest_len = node->len - node->offset;
    if (length >= rest_len){
        length = rest_len;
        /* stream is ending */
        node->state = TCP_ST_CLOSING;
    }

    optlen = cal_opt_len(TCP_FLAG_ACK);
	th_info[id].iph->id = htons(node->id);    
	th_info[id].iph->tot_len = htons(IP_HEADER_LEN + TCP_HEADER_LEN + optlen + length);
    th_info[id].iph->saddr = node->saddr;
    th_info[id].iph->daddr = node->daddr;
    th_info[id].iph->check = ip_checksum((struct iphdr *)th_info[id].iph);	
	
	ts_recent = node->ts_peer;
    generate_opt(++(node->ts), TCP_FLAG_ACK, (uint8_t *)th_info[id].tcph + TCP_HEADER_LEN, optlen, ts_recent);
    
    th_info[id].tcph->source = htons(node->sport);
    th_info[id].tcph->dest = htons(node->dport);
	th_info[id].tcph->seq = htonl(node->seq);
	th_info[id].tcph->ack_seq = htonl(node->ack_seq);
    th_info[id].tcph->doff = (TCP_HEADER_LEN + optlen) >> 2;
    th_info[id].tcph->syn = 0;
    th_info[id].tcph->fin = 0;
    th_info[id].tcph->ack = 1;
    th_info[id].tcph->psh = 1;
	th_info[id].tcph->check = tcp_checksum((struct iphdr*)th_info[id].iph, (struct tcphdr*)th_info[id].tcph);	

	/* Fill in the payload */
	payload_offset = HEADER_LEN + optlen;

	/* debug */
	if (rest_len < 0)
		fprintf(stderr, "%d %d %d %d\n", length, node->len, node->offset, rest_len);
	memcpy(((uint8_t *)th_info[id].pkt + payload_offset), ((uint8_t *)node->tot_buf + node->offset), length);

#ifdef DUMP_PAYLOAD
    dump_data(node, length);
#endif 

    /* update data offset */
    node->offset += length;
    /* update header fields */
    node->id++;
    node->seq += length;

    dpdk_send_pkt((uint8_t *)th_info[id].pkt, HEADER_LEN + optlen + length, p, q, id, 2);
    /* send correspond ACK */
#ifndef NO_ACK
	send_ack(node, p, q, id);
#endif
}

/* Description: cache total data of streams, where data for the same stream will be stored in the same buffer
 * @ tup	: 4-tuple
 * @ data	: data chunk with message for the same stream
 * @ length	: length of data
 * @ flag	: state of nids stream
 * */
int
store_stream_data(struct tuple4 tup, char *data, int length, int flag)
{
	int index;

	index = hash_index(tup);

	if (flag == NIDS_JUST_EST) {
		/* new stream */
	
	} else if (flag == NIDS_DATA) {
		/* receiving data */	
		struct buf_node *node = get_buf_node(tup, index);
		/* data for a new stream */
		if (node == NULL) {
			struct list_head *buf_list_t = &hash_buf.buf_list[index];

			/* data length exceeds the buffer size*/
			if (length > MAX_BUFFER_SIZE) {
				node = insert_buf_node(buf_list_t, (uint8_t *)data, MAX_BUFFER_SIZE, tup);

				/* reallocate more memory for large data */
				node->tot_buf = (uint8_t *)realloc(node->tot_buf, length);
                if (node->tot_buf == NULL) {
                    fprintf(stderr, "reallocate memory failed.\n");
                    return 0;
                }
				memcpy(node->tot_buf+node->len, data + MAX_BUFFER_SIZE, length - MAX_BUFFER_SIZE);
				node->len = length;
			} else {
				node = insert_buf_node(buf_list_t, (uint8_t *)data, length, tup);
			}
			
            return 0;
		} else {	
            /* Found existing buf_node in hash table */	
            /* Length of the total data exceeds buffer size*/
            if (node->len + length > MAX_BUFFER_SIZE) {
                /* reallocate more memory for large data */
                node->tot_buf = (uint8_t *)realloc(node->tot_buf, node->len + length);
                if (node->tot_buf == NULL) {
                    fprintf(stderr, "reallocate memory failed.\n");
                    return 0;
                }
                memcpy(node->tot_buf + node->len, data, length);
                node->len += length;
            } else {
                memcpy(node->tot_buf + node->len, data, length);
                node->len += length;
            }
        }
	} else if (flag == NIDS_CLOSE) {
		/* Not used for now !! */
    } else {
	/* other state */
#ifdef DEBUG_SEGMENTOR
		fprintf(stderr, "split_stream.c, stream_segmentor, flag error.\n");
#endif
	}

	return 1; 
}

/* Get length (40 ~ MAX_SEG_SIZE bytes)of payload */
static int
get_data_len(struct buf_node *node, int n_parts)
{
	int length;

	/* TODO: give a appropriate data length randomly*/
	if (is_len_fixed) {
		length = len_cut;
	} else {
		length = node->len / n_parts;
	}
	
	if (length < 40) { // Ignore small packets 
		length = 40;
	} else if (length > MAX_SEG_SIZE) {
		length = MAX_SEG_SIZE;
	}

	return length;
}

/* send packet according to TCP state */
static void
send_packet(struct buf_node *node, int n_parts, uint8_t p, uint16_t q, int id)
{
    uint32_t length;
	int 	type;

    /* sending packet according to TCP state */
    if(node->state == TCP_ST_CLOSED) {
        if (node->offset == 0) {
            send_syn(node, p, q, id);
        } else {
            printf("offset = %d, should be 0 here.\n", node->offset);
        }
    } else if (node->state == TCP_ST_SYN_SENT || node->state == TCP_ST_SYN_RCVD) {
        send_syn(node, p, q, id);
    } else if (node->state == TCP_ST_ESTABLISHED) { /* Transmit data */
		type = get_pkt_type(frag_rate);
		if (type == FRAGMENT_PKT) {
			length = get_data_len(node, n_parts);
		} else { // COMPLETE_PKT
			length = sunday_match((uint8_t *)node->tot_buf + node->offset, node->len - node->offset,
									"\r\n\r\n", 4);	
			if (length < 0) { // No "\r\n" found;
				length = node->len - node->offset;
			}
			if (length > MAX_SEG_SIZE) { // Jumbo packet
				length = get_data_len(node, n_parts);
			}
		}

        send_data_pkt(node, length, p, q, id);
    } else if (node->state == TCP_ST_CLOSING || node->state == TCP_ST_FIN_SENT_1 
                                             || node->state == TCP_ST_FIN_SENT_2) {
        send_fin(node, p, q, id);
    } else {
        printf("Sending packet failed, wrong TCP state.\n");
    }
}
/* Copy stream data stored in hash table to get more streams */
static void
copy_stream_data(int nb_copy)
{
    int i;
    int cnt;
    /* TODO: may modify nids_params.n_tcp_streams later*/
    int size = nids_params.n_tcp_streams;

    cnt = 0;
    while (!force_quit) {
        for (i = size - 1; i > 0; i--) {
            struct list_head *head = &hash_buf.buf_list[i];
            struct buf_node *buf_entry, *q;
            list_for_each_entry_safe(buf_entry, q, head, list) {
                /* Get a stochastic location for the new buf_node */
                int index = rand() % size;
                struct list_head *head_tmp = &hash_buf.buf_list[index];
                /* Giving a same tuple4 does not matter here, 
                 * because tuple4 only matters while reading packets from pcap file.
                 * */
                insert_buf_node(head_tmp, buf_entry->tot_buf, buf_entry->len, buf_entry->tup);
                
                if(++cnt >= nb_copy) {
                    printf("Succeed in generating more stream data.(%d)\n", nb_copy);
                    return;
                }
            }
        }
    }
}
/* Count number of streams held in hash buffer */
static int
counter (void)
{
    int i;
    int cnt = 0;
    int size = nids_params.n_tcp_streams;
#ifdef DUMP_PAYLOAD
    system("rm -rf files/*");
#endif
#ifdef SINGLE_STREAM
    int single_flag = 1; 
#endif

    for (i = 0; i < size; i++) {
        struct list_head *head = &hash_buf.buf_list[i];
        struct buf_node *buf_entry;
        list_for_each_entry(buf_entry, head, list) {
#ifdef DUMP_PAYLOAD_ALL
#ifdef SINGLE_STREAM
            if (single_flag && buf_entry->len >= 1500) {
#endif
            char file_name[30];
            char tup_str[50];
			char src_addr[10];
			char dst_addr[10];
            struct in_addr addr_s, addr_d;

			addr_s.s_addr = buf_entry->saddr;
			addr_d.s_addr = buf_entry->daddr;

			strcpy(src_addr, inet_ntoa(addr_s));
			strcpy(dst_addr, inet_ntoa(addr_d));

			sprintf(tup_str, "%s:%d-%s:%d", 
                    src_addr, buf_entry->sport,     
                    dst_addr, buf_entry->dport);
            sprintf(file_name, "files/file_%s", tup_str);
            FILE *file_fd = fopen(file_name, "wb+");
            if (!file_fd) {
                perror("fopen");
                return;
            }    
			fwrite((const char *)(buf_entry->tot_buf), sizeof(char), buf_entry->len, file_fd);
            fclose(file_fd);
#ifdef SINGLE_STREAM
                single_flag = 0;
            }
#endif
#endif //DUMP_PAYLOAD_ALL
            cnt++;
        }
    }
    printf("\nCounter, Number of streams read: %d\n", cnt);
    return cnt;
}

/* Initialize thread infos */
static void 
init_thread_info(void)
{
	int i;

    for (i = 0; i < nb_snd_thread; i++) {
		/* Initialize tx mbufs */
        memset(&th_info[i].tx_mbufs, 0, sizeof(th_info[i].tx_mbufs));
        th_info[i].tx_mbufs.len = 0;
        
        th_info[i].pre_tsc = rte_rdtsc();
        /* Note: temporary setting */
        usleep(1);

        memset(&th_info[i].stats, 0, sizeof(th_info[i].stats));

	 	char name[RTE_MEMPOOL_NAMESIZE];
		sprintf(name, "mbuf_pool_%d", i);
        
        th_info[i].mbuf_pool = rte_pktmbuf_pool_create(name, MAX_MBUF_PER_THREAD,
			 MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
        if(th_info[i].mbuf_pool == NULL) {
            printf("Thread %d, creating mempool failed.\n", i);
            exit(1);
        }
    }
}

#ifndef SEND_THREAD

/* Simulating SYN flood */
void
SYN_flood_simulator(void) 
{
    uint16_t    optlen;
    int         i;   
    uint32_t    ts_recent;
    int         size = nids_params.n_tcp_streams;
	char 		tmp_addr[20];

	/* Initializing */
	init_thread_info();

	// Get IP address and MAC address of destination from given file.
	if (get_dst_from_file){
		FILE *fp;
		if ((fp = fopen(dst_addr_file, "rb")) == NULL) {
			fprintf(stderr, "Failed to open file %s.\n", dst_addr_file);
			exit(1);
		}		
		fscanf(fp, "%s", tmp_addr);
		fscanf(fp, "%02X:%02X:%02X:%02X:%02X:%02X", 
					(unsigned int*)&dst_mac[0], (unsigned int*)&dst_mac[1], (unsigned int*)&dst_mac[2], 
					(unsigned int*)&dst_mac[3], (unsigned int*)&dst_mac[4], (unsigned int*)&dst_mac[5]);

		fclose(fp);
	}

    srand((int)time(0));
    prepare_header(0);

    ts_recent = 0; 

    printf(" Attention please!!\nSYN Flood appears...\n");
    while(!force_quit) {
        for (i = 0; i < size; i++) {
            struct list_head *head = &hash_buf.buf_list[i];
            struct buf_node *buf_entry, *q;
            list_for_each_entry_safe(buf_entry, q, head, list) {
				/* SYN   '->' */
				optlen = cal_opt_len(TCP_FLAG_SYN);
				set_start_ts(buf_entry);

				th_info[0].iph->id = htons(buf_entry->id);
				th_info[0].iph->tot_len = htons(IP_HEADER_LEN + TCP_HEADER_LEN + optlen);
				th_info[0].iph->saddr = buf_entry->saddr;
				
				if (!get_dst_from_file)
					th_info[0].iph->daddr = buf_entry->daddr;
				else
					th_info[0].iph->daddr = inet_addr(tmp_addr);
					
				th_info[0].iph->check = ip_checksum( (struct iphdr *)th_info[0].iph);	
				
				generate_opt(buf_entry->ts, TCP_FLAG_SYN, (uint8_t *)th_info[0].tcph + TCP_HEADER_LEN, optlen, ts_recent);
				
				th_info[0].tcph->source = htons(buf_entry->sport);

				if (!dst_port_fixed)
					th_info[0].tcph->dest = htons(buf_entry->dport);
				else
					th_info[0].tcph->dest = htons(dst_port);

				th_info[0].tcph->seq = htonl(buf_entry->seq);
				th_info[0].tcph->ack_seq = htonl(0);
				th_info[0].tcph->doff = (TCP_HEADER_LEN + optlen) >> 2;
				th_info[0].tcph->fin = 0;
				th_info[0].tcph->syn = 1;         // SYN
				th_info[0].tcph->psh = 0;
				th_info[0].tcph->ack = 0;
				th_info[0].tcph->check = tcp_checksum((struct iphdr*)th_info[0].iph, (struct tcphdr*)th_info[0].tcph);	

				dpdk_send_pkt((uint8_t *)th_info[0].pkt, HEADER_LEN + optlen, snd_port, 0, 0, 1);
                set_field(buf_entry);
            }
        }
    }
}

/* *
 * Description  : send stream stored in buffer table
 *
 * */
void 
send_streams(void)
{
    int i;
    int cnt;
    int n_snd;
    int n_part;     //divide total data of a stream into n_part patitions
    bool reach_concur;
    int size = nids_params.n_tcp_streams;

	/* Initializing */
	init_thread_info();
	
    srand((int)time(0));
    cnt = counter();
    /* if there is no enough stream data, copy to generate more data*/
    if (cnt < nb_concur - 1) {
        copy_stream_data( nb_concur + 1 - cnt);
    }
    nb_stream = counter();

    /* initialize packet header (ethernet header; IP header; TCP header)*/
	prepare_header(0);

    n_part = 1 + rand() % 10;
    while(!force_quit) {
        cnt = 0;
        reach_concur = false;

        /* Sending 'nb_concur' packets */
        for (i = 0; i < size; i++) {
            struct list_head *head = &hash_buf.buf_list[i];
            struct buf_node *buf_entry, *q;
            list_for_each_entry_safe(buf_entry, q, head, list) {
				/* skip packets with small payload*/
                if (is_len_fixed && buf_entry->len < len_cut) {
                    continue;
                } 	
#ifdef SINGLE_STREAM
                if (buf_entry->len < 1500)
                    continue;
                while (1) {
                    if (buf_entry->state == TCP_ST_FIN_SENT_2) {
                        send_packet(buf_entry, n_part, snd_port, 0, 0);
						dpdk_tx_flush();
                        return;
                    }
                    send_packet(buf_entry, n_part, snd_port, 0, 0);
                }
#endif
                /* Keep sending several packets of a stream */
                n_snd = rand() % 3 + 1;         // 1 ~ 3
                while (n_snd--) {
                    /* TODO: modify delivered queue number for multi-threading mode */
                    if (buf_entry->state == TCP_ST_FIN_SENT_2) {
                        send_packet(buf_entry, n_part, snd_port, 0, 0);
                        break;
                    } else {
                        send_packet(buf_entry, n_part, snd_port, 0, 0);
                    }
                }
                cnt++;
                /* Reach concurrency */
                if(cnt >= nb_concur) {
                    reach_concur = true; 
                    break;
                }
            }
            if(reach_concur)
                break;
        }
    }
}

#else    //#ifndef SEND_THREAD
    
/* Copy data for every single sending thread */
static void
copy_data_per_thread(void)
{
    int i, k;
    int cnt;
    int node_size = sizeof(struct buf_node);
    /* TODO: may modify nids_params.n_tcp_streams later*/
    int size = nids_params.n_tcp_streams;

    srand((int)time(0));
    cnt = counter();
    /* if there is no enough stream data, copy to generate more data*/
    if (cnt < nb_concur - 1) {
        copy_stream_data( nb_concur + 1 - cnt);
    }
    nb_stream = counter();
    
    for(i = 0; i < nb_snd_thread; i++) {
        th_info[i].nodes = (struct buf_node **)malloc(sizeof(struct buf_node *) * nb_stream);
    }

    cnt = 0;
    for (i = 0; i < size; i++) {
        struct list_head *head = &hash_buf.buf_list[i];
        struct buf_node *buf_entry, *q;
        list_for_each_entry_safe(buf_entry, q, head, list) {
            for (k = 0; k < nb_snd_thread; k++) {
                th_info[k].nodes[cnt] = (struct buf_node*)malloc(node_size);
                th_info[k].nodes[cnt]->tot_buf = (uint8_t *)malloc(sizeof(uint8_t) * buf_entry->len);
                if (th_info[k].nodes[cnt] == NULL || th_info[k].nodes[cnt]->tot_buf == NULL) {
                    printf("Allocate memory for buf_node failed.\n");
                    exit(1);
                }
                memcpy(th_info[k].nodes[cnt], buf_entry, node_size);
                memcpy(th_info[k].nodes[cnt]->tot_buf, buf_entry->tot_buf, buf_entry->len);
                
                set_field(th_info[k].nodes[cnt]);
            }
            cnt++;
        }
    }
    if (cnt < nb_stream) {
        printf("\nWarning: number of stream copied is less than  number of stream read.(cnt:%d, nb_stream:%d)\n", cnt, nb_stream);
    }
}

/* Free stream data copied before */
void
destroy_data_per_thread(void)
{
	int i;
	for (i = 0; i < nb_snd_thread; i++) {
		free(th_info[i].nodes);
	}
}

/* Main loop for sending thread */
void *
send_loop(void* args)
{
	int th_id = (int)args;
    int cnt;
    int i;
    int n_snd;
    int n_part;     //divide total data of a stream into n_part patitions
    printf("Thread %d start.\n", th_id);

    /* initialize packet header (ethernet header; IP header; TCP header)*/
	prepare_header(th_id);

    n_part = 25 + th_id;
    while(!force_quit) {
        cnt = 0;

        /* Sending 'concur_per_thread' packets */
        for (i = th_id ; i < nb_stream; i+= nb_snd_thread) {
            /* skip packets with small payload */
			#if 0
            if (is_len_fixed && th_info[th_id].nodes[i]->len < len_cut){
                continue;
            }
			#endif
            /* Keep sending several packets of a stream */
            n_snd = rand() % 3 + 1;         // 1 ~ 3
            while (n_snd--) {
				if (th_info[th_id].nodes[i]->state == TCP_ST_FIN_SENT_2) {
					send_packet(th_info[th_id].nodes[i], n_part, snd_port, th_id, th_id);
					break;
				} else {
					send_packet(th_info[th_id].nodes[i], n_part, snd_port, th_id, th_id);
				}
            }
            cnt++;
            /* Reach concurrency */
            if(cnt >= concur_per_thread) {
                break;
            }
        }
    }
    printf("Thread %d exit.\n", th_id);
}

/* Description  : create threads for sending stream to simulate scenario of sending concurrently */
void
run_send_threads(void)
{
	int 			i;
    int             thread_arg[(const int)nb_snd_thread];

	pthread_attr_t 	attr;
	cpu_set_t 		cpus;
	pthread_attr_init(&attr);
	
    /* get number of CPU cores */
	int nb_cores = sysconf(_SC_NPROCESSORS_ONLN);
    //int nb_cores = 20;   
    printf("CPU available : %d\nSending Threads :%d(Maximum %d)\n", nb_cores, nb_snd_thread, NUM_SEND_THREAD);
    
    /* copy total stream data for every thread */
    copy_data_per_thread();
	
	/* Free hash buffer table*/
	destroy_hash_buf();
    
    /* concurrency per thread */
    concur_per_thread = nb_concur / nb_snd_thread;
    if(concur_per_thread <= 0) {
        printf("Please give a larger concurrency!\n");
        exit(1);
    }

    /* Initializing !!! */
	init_thread_info();

	/* Creating sending thread */
	for (i = 0; i < nb_snd_thread; i++) {
		/* Necessary!! Args delivered to thread may be changed */
		thread_arg[i] = i;
		
		CPU_ZERO(&cpus);
		CPU_SET((i + 1) % nb_cores, &cpus);
		pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
		/* Note, pay attention to the arg delivered */	
		if (pthread_create(&th_info[i].thread_id, &attr, send_loop, (void *) thread_arg[i]) != 0)
			printf("Creating sending thread %d failed.\n", i);
	}
}

/* wait sending threads  */
void
wait_threads(void)
{
    int i;

	printf("\n");
	for (i = 0; i < nb_snd_thread; ++i) {
        pthread_join(th_info[i].thread_id, NULL);
	}
}
/* kill threads  */
void
destroy_threads(void)
{
    int i;

	for (i = 0; i < nb_snd_thread; ++i) {
        pthread_cancel(th_info[i].thread_id);
	}
}
#endif  //ifndef SEND_THREAD 
