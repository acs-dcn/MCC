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
#include "nids.h"

#define int_ntoa(x)	inet_ntoa(*((struct in_addr *)&x))

struct tuple4 tp4;

/* struct tuple4 contains addresses and port numbers of the TCP connections
 * the following auxiliary function produces a string looking like
 * 10.0.0.1,1024,10.0.0.2,23
 */
char *
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
	char buf[1024];
	strcpy (buf, adres (a_tcp->addr)); // we put conn params into buf
	if (a_tcp->nids_state == NIDS_JUST_EST)
	{
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
		/* if we don't increase this value, we won't be notified 
         * of urgent data arrival
		 */
		a_tcp->client.collect_urg++; 
#endif
		fprintf (stderr, "%s established\n", buf);
		return;
	}
	if (a_tcp->nids_state == NIDS_CLOSE)
	{
		/* connection has been closed normally */
		fprintf (stderr, "%s closing\n", buf);
		return;
	}
	if (a_tcp->nids_state == NIDS_RESET)
	{
		/* connection has been closed by RST */
		fprintf (stderr, "%s reset\n", buf);
		return;
	}

	if (a_tcp->nids_state == NIDS_DATA)
	{
		/* new data has arrived; gotta determine in what direction
		 * and if it's urgent or not
		 */
		struct half_stream *hlf;

		if (a_tcp->server.count_new_urg)
		{
			/* new byte of urgent data has arrived*/ 
			strcat(buf,"(urgent->)");
			buf[strlen(buf)+1]=0;
			buf[strlen(buf)]=a_tcp->server.urgdata;
			write(1,buf,strlen(buf));
			return;
		}
		/* We don't have to check if urgent data to client has arrived,
		 * because we haven't increased a_tcp->client.collect_urg variable.
		 * So, we have some normal data to take care of.
		 */
		if (a_tcp->client.count_new) {
			/* new data for client */
			/* from now on, we will deal with hlf var
   			 * which will point to client side of conn symbolic direction 
			 * of data
             */
			hlf = &a_tcp->client; 
			strcat (buf, "(<-)"); 
		}
		else {
			hlf = &a_tcp->server; // analogical
			strcat (buf, "(->)");
		}
		/* we print the connection parameters
		 * (saddr, daddr, sport, dport) accompanied
		 * by data flow direction (-> or <-)
		 */
		fprintf(stderr,"%s",buf); 
		write(2,hlf->data,hlf->count_new); // we print the newly arrived data
 	}
	return ;
}

/*
 * display usage infomation
 **/
void
print_usage(const char * prgname)
{
    printf("Usage: %s [options] [input file]\n"
            "\t-h help: display usage infomation\n"
            "\t-i PCAP FILE:\n"
            "\t\tget input packets from file\n\n",
            prgname);
}

/* parse commandline arguments 
 * */
int 
get_options(int argc, char *argv[])
{
    int opt = 0;
    int tmp;
//  char **argvopt = argv;

    while ((opt = getopt(argc, argv, "hi:")) != -1) {
        switch(opt) {
            case 'h':
                print_usage(argv[0]);
                exit(0);
            case 'i':
                strcpy(nids_params.filename, optarg);
                break;
//          default:
//              return 0;
        }   
    }   

    return 1;
}


int 
main (int argc, char *argv[])
{
	/* here we can alter libnids params, for instance:
	 * nids_params.n_hosts=256;
	 */
	if(argc < 3) {
		fprintf(stderr, "start failed, too few arguments for commandline. \n\n");
		print_usage(argv[0]);
	}
	get_options(argc, argv);

	if (!nids_init ()) {
		fprintf(stderr,"%s\n",nids_errbuf);
		exit(1);
	}
	nids_register_tcp (tcp_callback);
	nids_run ();
	return 0;
}

