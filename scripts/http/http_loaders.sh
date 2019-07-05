#!/bin/bash

nodes=$1
cmd=$2

start_test() 
{
  for ((i=1; i<=nodes; i++)); do
    ip=$((i+100))
    if (("$i" >= 9)); then
        ip=$((i+108))
    fi
    id=$((i-1))
    server_ip=$((id/2+25))
    echo "start node $i: to server 192.168.233.$server_ip"
    ssh root@172.16.163.$ip 'bash -s' 172.16.163.$ip $id 192.168.233.$server_ip < load_test.sh &
  done
}


stop_test()
{
  echo "killing all loaders..."
  for ((i=1; i<=nodes; i++)); do
    id=$((i-1))
    ip=$((i+101))
    ssh root@172.16.163.$ip 'pkill http_worker' &
  done
}

if [ "$cmd" == "stop" ]; then
  stop_test
else
  start_test
fi


