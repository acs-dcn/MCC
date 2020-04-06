#!/bin/bash

nodes=$1
filename="$2"

id=0
for line in `cat $filename`; do
  echo "node$id deploying DPDK"
  ssh root@$line 'bash -s' $((id/2+3)) $((id%2*100 + 7))< dpdk_setup.sh &
	id=$((id+1))
done
