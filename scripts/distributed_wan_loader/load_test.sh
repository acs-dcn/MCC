#!/bin/bash

ip=$1
id=$2
server=$3

export LD_LIBRARY_PATH=/usr/local/lib:/usr/lib:/usr/local/lib64:/usr/lib64

echo 10000000 > /proc/sys/fs/nr_open
ulimit -n 5000000

#echo 1 > /proc/sys/net/ipv4/tcp_tw_recycle
#echo 1 > /proc/sys/net/ipv4/tcp_tw_reuse
#echo 5 > /proc/sys/net/ipv4/tcp_fin_timeout
#echo 10000000 > /proc/sys/net/nf_conntrack_max

cd /home/wenqing/httploader/
ifconfig dpdk0 10.30.$(($id+3)).7 netmask 255.255.0.0 up
ifconfig dpdk0 10.30.$(($id+3)).7 netmask 255.255.0.0 up

nohup ./wan_worker -s 172.16.32.189 -l $ip -n $id --network-stack mtcp --dest $server --smp 2 --log error > /dev/null 2>&1

wall "Loader $id ..." 
