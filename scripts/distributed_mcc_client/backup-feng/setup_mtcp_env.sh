#!/bin/bash

nodes=$1

for ((i=1; i<=nodes; i++)); do
  ip=$((i+101))
  echo node$((i+1)) deploying DPDK
  ssh root@172.16.163.$ip 'bash -s' $((i+100)) < mtcp_setup.sh &
done
