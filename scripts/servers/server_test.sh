#!/bin/bash

line=$1
id=$2

echo 10000000 > /proc/sys/fs/nr_open
ulimit -n 5000000


cd /home/mcctest/qserver/
nohup ./echo_server  > /dev/null 2>&1


### For mTCP echo server
#cd /home/mcctest/server/

#tmp=${line##*.}
#ifconfig dpdk0 10.30.3.$tmp netmask 255.255.0.0 up
#ifconfig dpdk0 10.30.3.$tmp netmask 255.255.0.0 up

#nohup ./echo_server -c 1 > /dev/null 2>&1

wall "Server $id ..."
