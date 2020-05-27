#!/bin/bash

systemctl stop firewalld

cd /home/mcctest/httploader
ntpdate 172.16.32.128

./http_controller --device enp1s0f0 -c 100 -d 50 -t 100000 -n 1


