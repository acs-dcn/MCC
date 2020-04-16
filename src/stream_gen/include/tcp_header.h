#ifndef __TCP_HEADER_H__
#define __TCP_HEADER_H__

#define TCP_FLAG_FIN	0x01	// 0000 0001
#define TCP_FLAG_SYN	0x02	// 0000 0010
#define TCP_FLAG_RST	0x04	// 0000 0100
#define TCP_FLAG_PSH	0x08	// 0000 1000
#define TCP_FLAG_ACK	0x10	// 0001 0000
#define TCP_FLAG_URG	0x20	// 0010 0000

#define TCP_FLAG_SACK	0x40	// 0100 0000
#define TCP_FLAG_WACK	0x80	// 1000 0000

#define TCP_OPT_FLAG_MSS			0x02	// 0000 0010
#define TCP_OPT_FLAG_WSCALE			0x04	// 0000 0100
#define TCP_OPT_FLAG_SACK_PERMIT	0x08	// 0000 1000
#define TCP_OPT_FLAG_SACK			0x10	// 0001 0000
#define TCP_OPT_FLAG_TIMESTAMP		0x20	// 0010 0000	

#define TCP_OPT_MSS_LEN			4
#define TCP_OPT_WSCALE_LEN		3
#define TCP_OPT_SACK_PERMIT_LEN	2
#define TCP_OPT_SACK_LEN		10
#define TCP_OPT_TIMESTAMP_LEN	10

#define TCP_DEFAULT_MSS			1460
#define TCP_DEFAULT_WSCALE		7
#define TCP_INITIAL_WINDOW		14600	// initial window size


#define ETHERNET_HEADER_LEN		14	// sizeof(struct ethhdr)
#define IP_HEADER_LEN			20	// sizeof(struct iphdr)
#define TCP_HEADER_LEN			20	// sizeof(struct tcphdr)
#define TOTAL_TCP_HEADER_LEN	54	// total header length

/* convert timeval to ms */
#define TIMEVAL_TO_TS(t)        (uint32_t)((t)->tv_sec * 1000 + \
                                ((t)->tv_usec / 1000))

enum tcp_option
{
	TCP_OPT_END			= 0,
	TCP_OPT_NOP			= 1,
	TCP_OPT_MSS			= 2,
	TCP_OPT_WSCALE		= 3,
	TCP_OPT_SACK_PERMIT	= 4, 
	TCP_OPT_SACK		= 5,
	TCP_OPT_TIMESTAMP	= 8
};

enum tcp_state
{
	TCP_ST_CLOSED		= 0, 
	TCP_ST_LISTEN		= 1, 
	TCP_ST_SYN_SENT		= 2, 
	TCP_ST_SYN_RCVD		= 3,  
	TCP_ST_ESTABLISHED	= 4, 
	TCP_ST_FIN_SENT_1	= 5,    //self defined 
	TCP_ST_FIN_SENT_2	= 6,    //self defined
    TCP_ST_CLOSING      = 7
};

#endif
