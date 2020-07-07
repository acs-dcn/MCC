#!/bin/bash

systemctl stop firewalld

ntpdate 172.16.32.128

./master --device enp1s0f0 -e 100 -s 10 -c 100000 -b 1000 -n 1 -w 8 -i 64 -t 100000 -d 1000


