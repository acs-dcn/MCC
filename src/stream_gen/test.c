/*
Copyright (c) Wenqing Wu  <wuwenqing@ict.ac.cn>. All rights reserved.
See the file COPYING for license details.
*/
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <arpa/inet.h>
#include <string.h>

#include <getopt.h>
#include <pcap/pcap.h>
#include "libnids-1.24/src/nids.h"

#include "include/stream_gen.h"

#include <inttypes.h>
#include <rte_eal.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_malloc.h>

#ifdef USE_PDUMP
#include <rte_pdump.h>
#endif

static const struct rte_eth_conf port_conf_default = { 
    .rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN }
};
struct rte_eth_stats stats_start;

#ifdef STAT_THREAD
int stat_interval = 2;
pthread_t 		stat_id;
#endif
volatile bool force_quit;

struct thread_info              th_info[NUM_SEND_THREAD];

int                             snd_port = 0;
uint16_t                        burst = DEFAULT_BURST_SIZE;           // tx burst, default 64
struct rte_mempool*             mp;

#define int_ntoa(x)	inet_ntoa(*((struct in_addr *)&x))

struct hash_table               hash_buf;          //hash table
int         nb_stream;
/* commandline arguments */
int         nb_concur = 10;
int         nb_snd_thread = 1;
int         mode_run = 1;
int         len_cut = 5;
bool 		is_len_fixed = false;
bool 		syn_flood_set = false;
bool 		dst_port_fixed = false;
uint16_t	dst_port;
bool 		get_dst_from_file = false;
char		dst_addr_file[20];
int 		frag_rate = 100;

#define S_TO_TSC(t) rte_get_tsc_hz() * (t)
/* delay for 't' seconds */
static void
time_delay(int t)
{
	uint64_t drain_cycle;
	uint64_t cur_cycle, pre_cycle, diff_cycles;

	pre_cycle = rte_rdtsc();
	drain_cycle = S_TO_TSC(t);
	while (!force_quit) {
		cur_cycle = rte_rdtsc();
		diff_cycles = cur_cycle - pre_cycle;
		if (unlikely(diff_cycles >= drain_cycle)) {
			break;
		}
	}
}

static void
print_final_stat(void)
{
	struct rte_eth_stats stats_end;

    printf("\n\n\n++++++ Accumulated Statistics for streamGen ++++++\n");
    
	int i;
    uint64_t tx_total = 0, drop_total = 0;
    for (i = 0; i < nb_snd_thread; ++i) {
        tx_total += th_info[i].stats.tx;
        drop_total += th_info[i].stats.dropped;
    }   

	rte_eth_stats_get(snd_port, &stats_end);

	printf("----------- Statistics from application ----------\n");
    printf("   TX-packets:\t\t\t%"PRIu64"\n", tx_total);
    printf("   TX-dropped:\t\t\t%"PRIu64"\n", drop_total);
    printf("-------------- Statistics from NICs --------------\n");
    printf("   TX-packets:\t\t\t%"PRIu64"\n", stats_end.opackets - stats_start.opackets);
    printf("   TX-bytes:\t\t\t%"PRIu64"\n", stats_end.obytes - stats_start.obytes);
    printf("   TX-errors:\t\t\t%"PRIu64"\n", stats_end.oerrors - stats_start.oerrors);
    printf("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
}


static void
signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {

        force_quit = true;

#ifdef STAT_THREAD
		pthread_join(stat_id, NULL);
#endif

#ifdef USE_PDUMP
		/* uninitialize packet capture framework */
		rte_pdump_uninit();
#endif

#ifdef SEND_THREAD
		wait_threads();
		destroy_data_per_thread();
#else
        /* free hash table */
        destroy_hash_buf();
#endif
        
        /* print statistics */
        print_final_stat();

		/* exit with the expected status */
		signal(signum, SIG_DFL);
		kill(getpid(), signum);
    }   
}

/* struct tuple4 contains addresses and port numbers of the TCP connections
 * the following auxiliary function produces a string looking like
 * 10.0.0.1,1024,10.0.0.2,23
 */
static char *
adres (struct tuple4 addr)
{
	static char buf[256];
	strcpy (buf, int_ntoa (addr.saddr));
	sprintf (buf + strlen (buf), ",%i,", addr.source);
	strcat (buf, int_ntoa (addr.daddr));
	sprintf (buf + strlen (buf), ",%i", addr.dest);
	return buf;
}

void
tcp_callback (struct tcp_stream *a_tcp, void ** this_time_not_needed)
{
    struct      tuple4 tp4;
	char        buf[1024];

    memset(buf, 0, 1024);
	strcpy (buf, adres (a_tcp->addr)); // we put conn params into buf
	if (a_tcp->nids_state == NIDS_JUST_EST) {
		/* connection described by a_tcp is established
		 * here we decide, if we wish to follow this stream
		 * sample condition: if (a_tcp->addr.dest!=23) return;
		 * in this simple app we follow each stream, so..
		 */
		/* we want data received by a client */
		a_tcp->client.collect++; 
		/* and by a server, too */
		a_tcp->server.collect++; 
		/* we want urgent data received by a server */
		a_tcp->server.collect_urg++; 
								   
#ifdef WE_WANT_URGENT_DATA_RECEIVED_BY_A_CLIENT
		/* if we don't increase this value, we won't be notified of urgent data arrival */
		a_tcp->client.collect_urg++; 
#endif
		return;
	}
	if (a_tcp->nids_state == NIDS_CLOSE) {
		/* connection has been closed normally */
		/* TODO, determine in what direction (dual direction for now!!)*/
		/* same direction  */
		tp4 = a_tcp->addr;
		store_stream_data(tp4, NULL, 0, NIDS_CLOSE);	
		/* opposite direction */
		tp4.source = a_tcp->addr.dest;
		tp4.dest = a_tcp->addr.source;
		tp4.saddr = a_tcp->addr.daddr;
		tp4.daddr = a_tcp->addr.saddr;
		store_stream_data(tp4, NULL, 0, NIDS_CLOSE);	
		
		return;
	}
	if (a_tcp->nids_state == NIDS_RESET) {
		/* connection has been closed by RST */
		return;
	}

	if (a_tcp->nids_state == NIDS_DATA) {
		/* new data has arrived; gotta determine in what direction
		 * and if it's urgent or not
		 */
		struct half_stream *hlf = NULL;

		if (a_tcp->server.count_new_urg) {
			/* new byte of urgent data has arrived*/ 
			strcat(buf,"(urgent->)");
			buf[strlen(buf)+1]=0;
			buf[strlen(buf)]=a_tcp->server.urgdata;
#ifdef DEBUG_SEGMENTOR	
			write(1,buf,strlen(buf));
#endif
			tp4.source = a_tcp->addr.dest;
			tp4.dest = a_tcp->addr.source;
			tp4.saddr = a_tcp->addr.daddr;
			tp4.daddr = a_tcp->addr.saddr;

			/* TODO: call for segmentation function */
			/* segment data buffer */
//			store_stream_data(tp4, (char *)hlf->data, hlf->count - hlf->offset, NIDS_DATA);	

			return;
		}
		/* *
         * We don't have to check if urgent data to client has arrived,
		 * because we haven't increased a_tcp->client.collect_urg variable.
		 * So, we have some normal data to take care of.
		 * */
        /* Note!
         * data of two direcroty( client -> server; server -> client) is seperated into two parts,
         * see "if...else..." for details.
         * */
		if (a_tcp->client.count_new) {
#ifdef ONLY_REQUEST
            return;
#endif
			/* new data for client */
			tp4 = a_tcp->addr;
			
			/* from now on, we will deal with hlf var
   			 * which will point to client side of conn symbolic direction 
			 * of data
             */
			hlf = &a_tcp->client; 
			strcat (buf, "(<-)"); 
		} else {
			tp4.source = a_tcp->addr.dest;
			tp4.dest = a_tcp->addr.source;
			tp4.saddr = a_tcp->addr.daddr;
			tp4.daddr = a_tcp->addr.saddr;

			hlf = &a_tcp->server; // analogical
			strcat (buf, "(->)");
		}
		/* we print the connection parameters:
		 * (saddr, daddr, sport, dport) accompanied by data flow direction (-> or <-)
		 */
		/* segment data buffer */
		store_stream_data(tp4, (char *)hlf->data, hlf->count - hlf->offset, NIDS_DATA);	
#ifdef DEBUG_SEGMENTOR	
		write(2,hlf->data,hlf->count - hlf->offset); // we print the newly arrived data
#endif
 	}
	return ;
}

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf = port_conf_default;
    const uint16_t rx_rings = 1, tx_rings = NUM_SEND_THREAD;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    uint16_t q;

    if (port >= rte_eth_dev_count())
        return -1;

    /* Configure the Ethernet device. */
    fflush(stdout);
    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval < 0) {
        fprintf(stderr, "rte_eth_dev_configure failed.\n");
        return retval;
    }

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval < 0) {
        fprintf(stderr, "rte_eth_dev_configure failed.\n");
        return retval;
    }
    /* no need for packet generator for now */
    /* Allocate and set up 1 RX queue per Ethernet port. */
    fflush(stdout);
    q = 0;
    retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
                    rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0)
        return retval;
    
    /* Allocate and set up 1 TX queue per Ethernet port. */
    fflush(stdout);
    for (q = 0; q < tx_rings; q++) {
        retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                        rte_eth_dev_socket_id(port), NULL);
        if (retval < 0)
            return retval;
    }

    /* Start the Ethernet port. */
    retval = rte_eth_dev_start(port);
    if (retval < 0)
        return retval;

    /* Display the port MAC address. */
	struct ether_addr addr;
    rte_eth_macaddr_get(port, &addr);
    printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
               " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
            port,
            addr.addr_bytes[0], addr.addr_bytes[1],
            addr.addr_bytes[2], addr.addr_bytes[3],
            addr.addr_bytes[4], addr.addr_bytes[5]);

    /* Enable RX in promiscuous mode for the Ethernet device. */
    rte_eth_promiscuous_enable(port);

    return 0;
}

#ifdef STAT_THREAD
/* loop for statistics thread */
void *
stat_loop(void *arg)
{	 
	uint64_t drain_cycle;
	uint64_t cur_cycle, pre_cycle, diff_cycles;
	struct rte_eth_stats stat_s, stat_e;
	uint64_t pps_tx, bps_tx;

	drain_cycle = S_TO_TSC(stat_interval);
    
	time_delay(2);
	pre_cycle = rte_rdtsc();
	rte_eth_stats_get(snd_port, &stat_s);

	while (!force_quit) {
		cur_cycle = rte_rdtsc();
		diff_cycles = cur_cycle - pre_cycle;

		if (unlikely(diff_cycles >= drain_cycle)) {
			pre_cycle = cur_cycle;
			
			rte_eth_stats_get(snd_port, &stat_e);
			
			pps_tx = (stat_e.opackets - stat_s.opackets) * rte_get_tsc_hz() / diff_cycles;
			bps_tx = (stat_e.obytes - stat_s.obytes) * 8 * rte_get_tsc_hz() / diff_cycles;
			/* Clear screen and move to top left */
			const char clr[] = { 27, '[', '2', 'J', '\0' };
			const char topLeft[] = { 27, '[', '1', ';', '1', 'H','\0' };
			printf("%s%s", clr, topLeft);
		
			printf("\nNIC Statistics ===================================\n");
			printf("Accumulated Statistics from beginning ------------\n");
			printf("  TX-packets:\t\t\t%"PRIu64"\n", stat_e.opackets - stats_start.opackets);
			printf("  TX-bytes:\t\t\t%"PRIu64"\n", stat_e.obytes - stats_start.obytes);
            printf("  TX-errors:\t\t\t%"PRIu64"\n", stat_e.oerrors - stats_start.oerrors);
			printf("Throughput Since Last Display --------------------\n");
			printf("  TX-pps:\t\t\t%"PRIu64"\n", pps_tx);
			if (bps_tx > 1048576) 
				printf("  TX-Mbps:\t\t\t%"PRIu64"\n", bps_tx / 1048576);
			else
				printf("  TX-bps:\t\t\t%"PRIu64"\n", bps_tx);
			
			stat_s.obytes = stat_e.obytes;
			stat_s.opackets = stat_e.opackets;
			printf("==================================================\n\n");
		}
	}
}
#endif

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 */
static void
lcore_main(void)
{
	pthread_attr_t  attr;
	cpu_set_t       cpus;

    /* store stream data read from pcap file */
    printf("\nReading stream data from pcap file...\n");
    nids_run();

	/* Initialization of Sunday algorithm */
	sunday_pre("\r\n\r\n", 4);
	
    printf("\nStart sending packets. [Ctrl+C to quit]\n");
#ifdef SEND_THREAD
	/* send streams concurrently with multi-thread*/
	run_send_threads();
#ifdef STAT_THREAD
	/* start statistics thread */
	pthread_attr_init(&attr);
	CPU_ZERO(&cpus);
	CPU_SET( 0, &cpus);  // affinitize to CPU 0
	pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);

	if (pthread_create(&stat_id, &attr, stat_loop, NULL) != 0 ) {
		printf("Failed to create statistics thread.\n");
		exit(1);
	}
#endif
	/* wait sending thread */
	wait_threads();

#else  //#ifdef SEND_THREAD

#ifdef STAT_THREAD
	/* start statistics thread */
	pthread_attr_init(&attr);
	CPU_ZERO(&cpus);
	CPU_SET(2, &cpus);  // affinitize to CPU 2
	pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);

	if (pthread_create(&stat_id, &attr, stat_loop, NULL) != 0 ) {
		printf("Failed to create statistics thread.\n");
		exit(1);
	}
#endif
	if (mode_run == 1 ){   // Normal stream generation mode
    	/* send streams concurrently */
   	 	send_streams();
	} else {               // Simulating SYN flood
		SYN_flood_simulator();
	}
#endif
}

/* display usage infomation */
static void
print_usage(const char * prgname)
{
    printf("Usage: %s [EAL options] -- [options] [input file]\n"
        "\n\t[options]:\n"
        "\t-h HELP: Display usage infomation\n"
        "\t-i PCAP FILE:\n"
        "\t\tInput packets which contains network trace\n"
        "\t-o INTERFACE:\n"
        "\t\tInterface used for sending packets\n"
        "\t\t(e.g. 1 for port1 with DPDK, default 0)\n"
        "\t-c CONCURRENCY: concurrency of sending streams.(default 10)\n"
        "\t-t THREADS:\n"
        "\t\tNumber of sending threads (default 1, maximum 8)\n"
        "\t-l LENGTH of PAYLOAD: \n"
		"\t\tGive a fixed length of payload for packets with payload\n"
		"\t-T TIME INTERVAL: \n"
		"\t\tTime interval for displaying statistics information.(default 2 for 2s)\n"
        "\t-b BURST: \n"
        "\t\tTransmiting burst while sending with DPDK (default 1, maximum 128)\n"
		"\t-d DEST_PORT: \n"
		"\t\tGive a fixed dest_port for SYN Flooding.\n"
		"\t-r RATE:\n"
		"\t\tRate of fragmented packets.(default 100 for 100%)\n"
		"\t-f FILE NAME: \n"
		"\t\tFile which contains dest IP address and dest MAC address.\n"
        "\t-m RUNNING MODE:\n"
        "\t\t1, Normal Sending mode\n"
        "\t\t2, Simulating SYN flood\n\n",
        prgname);
}

/* parse commandline options */
static int 
get_options(int argc, char *argv[])
{
    int opt = 0;

    while ((opt = getopt(argc, argv, "hi:o:m:b:c:d:f:l:r:t:T:")) != -1) {
        switch(opt) {
            case 'h':
                print_usage(argv[0]);
                exit(0);
            case 'i':
				nids_params.filename = (char *)malloc(strlen(optarg));
                strcpy(nids_params.filename, optarg);
                break;
            case 'o':
                snd_port = atoi(optarg);
              	break;
            case 'c':
              	nb_concur = atoi(optarg);
              	break;
            case 'd':
              	dst_port = atoi(optarg);
				dst_port_fixed = true;
              	break;
            case 'f':
              	strcpy(dst_addr_file, optarg);
				get_dst_from_file = true;
              	break;
            case 'b':
              	burst = (uint16_t)atoi(optarg);
                if (burst < 1 || burst >= MAX_BURST) {
                    rte_exit(EXIT_FAILURE, "\nInvalid burst (1 ~ %d).\n", MAX_BURST);
                }
              	break;
            case 'm':
              	mode_run = atoi(optarg);
                if (mode_run < 1 || mode_run > 2) {
                    rte_exit(EXIT_FAILURE, "\nInvalid running mode (2 modes supported for now).\n");
                }
              	break;
            case 'r':
              	frag_rate = atoi(optarg);
                if (frag_rate < 0 || frag_rate > 100) {
                    rte_exit(EXIT_FAILURE, "\nInvalid fragment rate (0 ~ 100). \n");
                }
              	break;
            case 't':
              	nb_snd_thread = atoi(optarg);
				if (nb_snd_thread < 1 || nb_snd_thread > NUM_SEND_THREAD) {
					rte_exit(EXIT_FAILURE, "\nInvalid number of thread (1 ~ %d).\n", NUM_SEND_THREAD);
				}
              	break;
#ifdef STAT_THREAD
            case 'T':
              	stat_interval = atoi(optarg);
              	break;
#endif
            case 'l':
              	len_cut = atoi(optarg);
				is_len_fixed = true;
				if (len_cut < 0) {
					rte_exit(EXIT_FAILURE, "\nInvalid length of payload.\n");
				}
				if (len_cut > MAX_SEG_SIZE) {
					printf("\nWarning, length of payload exceeds maximum segment size, will be replaced by MSS.\n");
				}
              	break;
	        default:
				print_usage(argv[0]);	
				return -1;
        }   
    }   

    return 1;
}


int 
main (int argc, char *argv[])
{
    int ret;
    int i;
    //struct rte_mempool *mbuf_pool;
    unsigned nb_ports;
    //uint16_t portid;

	ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "\nError with EAL initialization\n");
	}

    argc -= ret;
    argv += ret;
	
    force_quit = false;
	is_len_fixed = false;
#ifdef USE_PDUMP
	/* initialize packet capture framework */
	rte_pdump_init(NULL);
    printf ("pdump server initialized.\n");
#endif
	
	signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

	/* here we can alter libnids params, for instance:
	 * nids_params.n_hosts=256;
	 */
	if(argc < 3) {
		fprintf(stderr, "\nStart-up failed, too few arguments for commandline. \n\n");
		print_usage(argv[0]);
		exit(1);
	}
	
	ret = get_options(argc, argv);
	if (ret < 0 ) {
		fprintf(stderr, "\nFailed to parse options, Invalid arguments.\n");
        exit(1);
	}

	if (mode_run == 2) 
		syn_flood_set = true;
	init_hash_buf();
    nb_stream = 0;
    
    /* number of ports */
	nb_ports = rte_eth_dev_count();
    printf("\nNumber of Network Ports Available:%d\n", nb_ports);
	if (nb_ports <= 0) {
		rte_exit(EXIT_FAILURE, "Error: no ports available\n");
	} else if (snd_port >= nb_ports){
		rte_exit(EXIT_FAILURE, "Error: Specified port %d is invalid.\n", snd_port);
	}

    printf("NUMA info, socket id: %d, socket id for network device: %d\n", rte_socket_id(), rte_eth_dev_socket_id(0));
	/* Creates a new mempool in memory to hold the mbufs. */
    mp = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mp == NULL)
        rte_exit(EXIT_FAILURE, "\nCannot create mbuf pool\n");

    /* Initialize all ports. */
    if (port_init(snd_port, mp) != 0)
        rte_exit(EXIT_FAILURE, "\nCannot init port 0\n");

	if (!nids_init ()) {
		fprintf(stderr,"error, %s\n",nids_errbuf);
		goto outdoor;
	}
	
	rte_eth_stats_get(snd_port, &stats_start);

	nids_register_tcp (tcp_callback);
    /* main stream generation loop */
	lcore_main(); 
	
#ifdef SEND_THREAD
	destroy_threads();
	destroy_data_per_thread();
#endif

#ifdef STAT_THREAD
	pthread_cancel(stat_id);
#endif
	/* sent out or drop rest data remains in hash table,
	 * and free hash table 
	 * */
    dpdk_tx_flush();
    print_final_stat();

outdoor:
    printf("finishing ...\n");

#ifndef SEND_THREAD
	destroy_hash_buf();
#endif

	return 0;
}

