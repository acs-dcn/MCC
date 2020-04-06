#!/bin/bash

nodes=$1
cmd=$2

start_test() 
{
  for ((i=1; i<=nodes; i++)); do
    id=$((i-1))
    ip=$((i+101))
    server_node=$((id/2))
    server_ip=$((server_node+1))
    echo "start node $((i+1)): to server 192.168.240.$server_ip"
    ssh root@172.16.163.$ip 'bash -s' 172.16.163.$ip $id 192.168.240.$server_ip < load_test.sh &
  done
}


stop_test()
{
  echo "killing all loaders..."
  for ((i=1; i<=nodes; i++)); do
    id=$((i-1))
    ip=$((i+101))
    ssh root@172.16.163.$ip 'pkill -9 worker' &
  done
}

setup_ip()
{
  echo "setting virtual ip..."
  ip_reserved=20
  for ((i=1; i<=nodes; i++)); do
    block=$((i/4))
    offset=$((i%4))
    ip_start=$((offset*50+ip_reserved))
    ip_end=$(((offset+1)*50+ip_reserved))
    ip_block=$((block+241))
    admin_ip=$((i+101))
    echo "node $((i+1)): 192.168.$ip_block.($ip_start, $ip_end)"
    ssh root@172.16.163.$admin_ip 'bash -s'  $ip_start $ip_end $ip_block up < ip_setup.sh &
  done
}

unset_ip()
{
  echo "setting virtual ip..."
  ip_reserved=20
  for ((i=1; i<=nodes; i++)); do
    block=$((i/4))
    offset=$((i%4))
    ip_start=$((offset*50+ip_reserved))
    ip_end=$(((offset+1)*50+ip_reserved))
    ip_block=$((block+241))
    admin_ip=$((i+101))
    echo "node $i: 192.168.$ip_block.($ip_start, $ip_end)"
    ssh root@172.16.163.$admin_ip 'bash -s'  $ip_start $ip_end $ip_block down < ip_setup.sh &
  done
}

if [ "$cmd" == "stop" ]; then
  stop_test
elif [ "$cmd" == "set" ]; then
  setup_ip
elif [ "$cmd" == "start" ]; then
  start_test
elif [ "$cmd" == "unset" ]; then
  unset_ip
else
  echo "unknown option"
fi


