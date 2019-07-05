#!/bin/bash

ip=$1
id=$2
dest=$3

export LD_LIBRARY_PATH=/usr/local/lib:/usr/lib:/usr/local/lib64:/usr/lib64
ulimit -n 5000000
cd /root/infgen/scripts
./setup_env.sh
ulimit -n 5000000
#cd /root/infgen/build/apps/distributed_mcc
nohup ./worker -s 172.16.163.101 -l $ip -n $id --device ens1 \
  --dest $dest --smp 2 --log error &
wall "Loader started" 
