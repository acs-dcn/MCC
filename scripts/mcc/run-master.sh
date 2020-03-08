#!/bin/bash

systemctl stop firewalld
./master --device enp3s0f1 -e 100 -s 100 -c 1000000 -b 10000 -n 8 -w 3 -d 1000


