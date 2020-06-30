#!/bin/bash

systemctl stop firewalld

cd /home/mcctest/wanloader
# modify NTP server IP
ntpdate 172.16.32.128

# modify name of NIC
./wan_controller --device enp1s0f0 -c 1000 -d 20 -g 1 -l 64 -i 5 -t 100000 -n 1


