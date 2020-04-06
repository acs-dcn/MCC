#!/bin/bash

systemctl stop firewalld

cd /home/wenqing/httploader

./http_controller --device eno1 -c 8000 -d 1000 -n 1


