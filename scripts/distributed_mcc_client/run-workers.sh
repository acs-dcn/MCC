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
    ssh root@$line 'pkill -9 worker' &
		echo "Loader $id finished"
		id=$(($id+1))
		if [ $id -eq $nodes ] 
		then
				break;  
		fi
  done
}

# For kernel stack
setup_ip()
{
  echo "Setting virtual ip..."
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

# For kernel stack
unset_ip()
{
  echo "Setting virtual ip..."
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
  echo "Warning, Unknown option."
fi


