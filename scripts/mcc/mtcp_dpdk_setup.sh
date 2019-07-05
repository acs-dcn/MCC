#!/bin/bash
      
ip=$1 
      
#yum install -y kernel-devel-`uname -r`
      
cd /root/infgen/mtcp
ifconfig ens1 down
      
./setup_mtcp_dpdk_env.sh /root/infgen/mtcp/dpdk < dpdk-inputfile
     
ifconfig dpdk0 192.168.240.$ip netmask 255.255.240.0 up
