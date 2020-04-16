#ifndef __CHECKSUM_H__
#define __CHECKSUM_H__

#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <linux/if_ether.h>

/* flags value at payload first byte: 
 * - SYNC : client need to send acknowledgment packets.
 * - ACK : this is ack packet from client, check the ack_no
 *         if necessary to know ack for which seq_no packet.
 * - CRC : client side has CRC or others error happen 
 */
#define SYNC 0x1
#define ACK 0x2
#define ERR 0x4

#define HEADER_LEN      (sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct tcphdr))
#define PAYLOAD_OFFSET  HEADER_LEN
#define TCPHDR_OFFSET   (sizeof(struct ethhdr) + sizeof(struct iphdr))
#define IPHDR_OFFSET    sizeof(struct ethhdr)

/* ethernet headers are always exactly 14 bytes [1] */
#define SIZE_ETHERNET 14



// calculate the checksum of the given buf, providing sum 
// as the initial value
static inline uint16_t checksum(uint16_t *buf, int nbytes, uint32_t sum)
{
    int i;

    for (i = 0; i < nbytes / 2; i++)
        sum += buf[i];
 
    sum = (sum >> 16) + (sum & 0xffff);
    sum = sum + (sum >> 16);

    if (nbytes % 2)
        sum += ((uint8_t *)buf)[nbytes-1];

    return (uint16_t)~sum;
}

static inline uint16_t tcp_checksum(struct iphdr *ip, struct tcphdr *tcp)
{
    uint16_t tmp = tcp->check;
    tcp->check = 0;

    uint16_t reserv_proto = ip->protocol;
    uint16_t tcp_len = ntohs(ip->tot_len) - ip->ihl * 4;

    uint32_t sum = ip->saddr + ip->daddr + htons(reserv_proto) + htons(tcp_len);
    uint16_t cksum = checksum((uint16_t *)tcp, (int)tcp_len, sum);

    tcp->check = tmp;

    return cksum;
}

static inline uint16_t ip_checksum(struct iphdr *hdr)
{
    uint16_t tmp = hdr->check;
    hdr->check = 0;
    uint16_t sum = checksum((uint16_t *)hdr, hdr->ihl * 4, 0);
    hdr->check = tmp;

    return sum;
}

#endif
