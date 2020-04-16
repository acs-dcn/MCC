#!/bin/bash

systemctl stop firewalld

cd /home/wenqing/mcc
ntpdate 172.16.32.128

./master --device eno 1 -e 100 -s 10 -c 1000000 -b 10000 -n 1 -w 8 -i 64 -d 1000


