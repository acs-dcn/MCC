#!/bin/bash

nodes=$1
cmd=$2
filename="$3"

set_env() 
{
	id=0
	for line in `cat $filename`
	do 
		echo "node $id setting environment variables"
		ssh root@$line 'bash -s' < setup_env.sh &
		id=$(($id+1))
		if [ $id -eq $nodes ] 
		then
				break;  
		fi
	done
}

set_mtcp() 
{
	id=0
	for line in `cat $filename`
	do 
		echo "node $id deploying DPDK"
    tmp=${line%.*}
    ip1=${tmp##*.}
    tmp=${line##*.}
    tmp=$(($tmp-100))
    ip2=$(($tmp*5))
		scp dpdk-inputfile root@$line:/home/jinxu/infgen/mtcp/ &
		ssh root@$line 'bash -s' $ip1 $ip2 < mtcp_setup.sh &
		id=$(($id+1))
		if [ $id -eq $nodes ]
		then
				break;  
		fi
	done
}

set_date() 
{
	id=0
	for line in `cat $filename`
	do 
		echo "node $id setting date"
		ssh root@$line 'mv /etc/localtime /etc/localtime.bak && ln -s /usr/share/zoneinfo/Asia/Shanghai  /etc/localtime' &
		id=$(($id+1))
		if [ $id -eq $nodes ] 
		then
				break;  
		fi
	done
}

sync_ts()
{
	id=0
  echo "Synchronizing..."
	for line in `cat $filename`
  do
    ssh root@$line 'yum install -y ntp && ntpdate 11.11.12.18' &
		echo "Loader $id finished"
		id=$(($id+1))
		if [ $id -eq $nodes ] 
		then
				break;  
		fi
  done
}


clean()
{
	id=0
  echo "Cleaning..."
	for line in `cat $filename`
  do
    ssh root@$line 'cd /home/mcctest/mcc && rm -f core.* log_* && cd /home/mcctest/httploader && rm -f core.* log_* && cd /home/mcctest/wanloader && rm -f core.* log_*' &
		echo "Loader $id finished"
		id=$(($id+1))
		if [ $id -eq $nodes ] 
		then
				break;  
		fi
  done
}

if [ "$cmd" == "env" ]; then
  set_env
elif [ "$cmd" == "mtcp" ]; then
  set_mtcp
elif [ "$cmd" == "sync" ]; then
  sync_ts
elif [ "$cmd" == "date" ]; then
  set_date
elif [ "$cmd" == "clean" ]; then
  clean
else
  echo "Warning, Unknown option."
fi


