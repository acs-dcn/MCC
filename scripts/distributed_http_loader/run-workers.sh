#!/bin/bash

nodes=$1
cmd=$2
filename="$3"

start_test() 
{
  id=0
  for line in `cat $filename`
  do  
    echo "Start node $id to server 10.30.3.6"
    ssh root@$line 'bash -s' $line $id 10.30.3.6 < load_test.sh &
		id=$(($id+1))    
		if [ $id -eq $nodes ] 
		then
				break;  
		fi
  done
}


stop_test()
{
	id=0
  echo "Killing all loaders..."
  for line in `cat $filename`
  do  
    ssh root@$line 'pkill -9 http_worker' &
		echo "Loader $id finished"
		id=$(($id+1))
		if [ $id -eq $nodes ] 
		then
				break;  
		fi
  done
}

if [ "$cmd" == "stop" ]; then
  stop_test
else
  start_test
fi


