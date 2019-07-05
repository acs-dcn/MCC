#!/bin/bash

for i in {116..132}; do
    ip=$((i-115))
    ssh root@172.16.163.$i 'ifconfig ens1 192.168.240.$ip'
done
