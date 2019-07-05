#!/bin/bash

ip_start=$1
ip_end=$2
ip_block=$3
command=$4

for ((i=1; i<=50; i++)); do 
  ip=$((ip_start+i))
  ifconfig ens1:$i 192.168.$ip_block.$ip/20 $command;
done

