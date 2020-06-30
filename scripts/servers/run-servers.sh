#!/bin/bash

nodes=$1
cmd=$2
filename="$3"

start_servers() 
{
  id=0
  for line in `cat $filename`
  do  
    echo "launch server $id"	
	ssh root@$line 'bash -s' $line $id < server_test.sh &
    id=$(($id+1))
    if [ $id -eq $nodes ] 
    then
        break;  
    fi  
  done
}

stop_servers() 
{
  id=0
  for line in `cat $filename`
  do  
    echo "close server $id"
    ssh root@$line 'pkill -9 echo_server &' &
    id=$(($id+1))
    if [ $id -eq $nodes ] 
    then
        break;  
    fi  
  done
}

if [ "$cmd" == "start" ]; then
  start_servers
elif [ "$cmd" == "stop" ]; then
  stop_servers
else
  echo "Warning, Unknown option."
fi
