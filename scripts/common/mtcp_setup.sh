#!/bin/bash
      
ip1=$1 
ip2=$2

cd /home/mcctest/infgen/mtcp

# git submodule init
# git submodule update
      
yum install -y gmp-devel
yum install -y numactl-devel

export RTE_SDK=`echo $PWD`/dpdk
export RTE_TARGET=x86_64-native-linuxapp-gcc
      
ifconfig enp3s0f0 down
./setup_mtcp_dpdk_env.sh /home/mcctest/infgen/mtcp/dpdk < dpdk-inputfile
     
ifconfig dpdk0 10.30.$ip1.$ip2 netmask 255.255.0.0 up

autoreconf -ivf
./configure --with-dpdk-lib=$RTE_SDK/$RTE_TARGET
make


