#!/bin/bash

nodes=$1
cmd=$2
filename="$3"

start_test() 
{
	id=0
	for line in `cat $filename`
	do 
		echo "Start node $id to server"
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
	id=0
	ip_start=50
	ip_end=150
	ip_block=3
	for line in `cat $filename`
  do
    ssh root@$line 'bash -s'  $ip_start $ip_end $ip_block up < ip_setup.sh &
		echo "Loader $id set."
		id=$(($id+1))
		if [ $id -eq $nodes ] 
		then
				break;  
		fi
  done
}

# For kernel stack
unset_ip()
{
  echo "Setting virtual ip..."
	id=0
	ip_start=50
	ip_end=150
	ip_block=3
	for line in `cat $filename`
  do
    ssh root@$line 'bash -s'  $ip_start $ip_end $ip_block down < ip_setup.sh &
		echo "Loader $id set."
		id=$(($id+1))
		if [ $id -eq $nodes ] 
		then
				break;  
		fi
  done
}

if [ "$cmd" == "stop" ]; then
  stop_test
elif [ "$cmd" == "start" ]; then
  start_test
elif [ "$cmd" == "set_ip" ]; then
  setup_ip
elif [ "$cmd" == "unset_ip" ]; then
  unset_ip
else
  echo "Warning, Unknown option."
fi


