#!/bin/bash
echo 10000000 > /proc/sys/fs/nr_open
echo 1 > /proc/sys/net/ipv4/tcp_tw_recycle
echo 1 > /proc/sys/net/ipv4/tcp_tw_reuse
echo 5 > /proc/sys/net/ipv4/tcp_fin_timeout
echo 10000000 > /proc/sys/net/nf_conntrack_max
