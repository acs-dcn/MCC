#!/bin/bash

ip=$1
id=$2
server=$3

echo 10000000 > /proc/sys/fs/nr_open
ulimit -n 5000000

cd /home/mcctest/httploader/
ifconfig dpdk0 10.30.$(($id+3)).7 netmask 255.255.0.0 up
ifconfig dpdk0 10.30.$(($id+3)).7 netmask 255.255.0.0 up

nohup ./http_worker -s 172.16.32.128 -l $ip -n $id --network-stack mtcp --dest $server --smp 5 --log error > /dev/null 2>&1

wall "Loader $id ..." 
