#!/bin/bash

ip_start=$1
ip_end=$2
ip_block=$3
command=$4

for ((i=$ip_start; i<=$ip_end; i++)); do 
  ifconfig enp1s0f0:$i 192.168.$ip_block.$i/20 $command;
done

