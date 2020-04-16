#!/bin/bash

./http_worker -s 172.16.32.189 -l 172.16.32.128 -n 4 --network-stack mtcp --dest 10.30.3.6 --smp 2
