#!/bin/bash
      
ip1=$1 
ip2=$2
      
#yum install -y kernel-devel-`uname -r`
yum install -y gmp-devel
yum install -y numactl-devel
      
cd /home/wenqing/infgen/mtcp
ifconfig enp3s0f0 down
      
./setup_mtcp_dpdk_env.sh /home/wenqing/infgen/mtcp/dpdk < dpdk-inputfile
     
ifconfig dpdk0 10.30.$ip1.$ip2 netmask 255.255.0.0 up

