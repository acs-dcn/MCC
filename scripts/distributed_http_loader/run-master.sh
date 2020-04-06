#!/bin/bash

systemctl stop firewalld

cd /home/wenqing/httploader
ntpdate 172.16.32.128

./http_controller --device eno1 -c 8000 -d 1000 -n 1


