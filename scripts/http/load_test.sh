#!/bin/bash

ip=$1
id=$2
server=$3

export LD_LIBRARY_PATH=/usr/local/lib:/usr/lib:/usr/local/lib64:/usr/lib64
cd /root/infgen/build/apps/distributed_loader_http 
nohup ./http_worker -s 172.16.163.101 -l $ip -n $id --device ens1 \
  --dest $server --smp 9 --log error &
wall "Loader started" 
