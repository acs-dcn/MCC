#!/bin/bash

./wan_worker -s 172.16.32.189 -l 172.16.32.128 -n 0 --network-stack mtcp --dest 10.30.3.6 --smp 2 --ips 200 -v 1 -i 5
