#!/bin/bash
export LD_LIBRARY_PATH=/usr/local/lib:/usr/lib:/usr/local/lib64:/usr/lib64
echo 10000000 > /proc/sys/fs/nr_open
echo 1 > /proc/sys/net/ipv4/tcp_tw_recycle
echo 1 > /proc/sys/net/ipv4/tcp_tw_reuse
echo 5 > /proc/sys/net/ipv4/tcp_fin_timeout
echo 10000000 > /proc/sys/net/nf_conntrack_max
timedatectl set-ntp true
