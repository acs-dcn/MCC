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
		ssh root@$line 'rm -f /home/mcctest/infgen/mtcp/dpdk-inputfile' &
		scp dpdk-inputfile root@$line:/home/mcctest/infgen/mtcp/ &
		ssh root@$line 'bash -s' $ip1 $ip2 < mtcp_setup.sh &
		id=$(($id+1))
		if [ $id -eq $nodes ]
		then
				break;  
		fi
	done
}

set_ip() 
{
  id=0
  for line in `cat $filename`
  do
    echo "Node $id, setting ip"
    tmp=${line%.*}
    ip1=${tmp##*.}
    tmp=${line##*.}
    tmp=$(($tmp-100))
    ip2=$(($tmp*5))
    ssh root@$line 'ifconfig dpdk0 10.30.'$ip1'.'$ip2' netmask 255.255.0.0 up
'&    id=$(($id+1))
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

# For kernel stack
setup_vip()
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
unset_vip()
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

## env 		: set kernel parameters
## mtcp		: deploy mTCP 
## ip 		: set ip of dpdk0
## sync		: NTP synchronization
## date		: modify region
## set_vip: set virtual IP address when using kernel stack
## unset_vip: unset virtual IP address when using kernel stack
## clean  : delete redundant files

if [ "$cmd" == "help" ]; then
	echo "./mcc_setup.sh <command> <number of nodes> <hosts file>"
	echo "command:"
  echo "env    		: set kernel parameters"
	echo "mtcp		: deploy mTCP" 
	echo "ip 		: set ip of dpdk0"
	echo "sync		: NTP synchronization"
	echo "date		: modify region"
	echo "set_vip		: set virtual IP address when using kernel stack"
	echo "unset_vip	: unset virtual IP address when using kernel stack"
	echo "clean  		: delete redundant files"
elif [ "$cmd" == "env" ]; then
  set_env
elif [ "$cmd" == "mtcp" ]; then
  set_mtcp
elif [ "$cmd" == "ip" ]; then
  set_ip
elif [ "$cmd" == "sync" ]; then
  sync_ts
elif [ "$cmd" == "date" ]; then
  set_date
elif [ "$cmd" == "set_vip" ]; then
  setup_vip
elif [ "$cmd" == "unset_vip" ]; then
  unset_vip
elif [ "$cmd" == "clean" ]; then
  clean
else
  echo "Warning, Unknown option."
fi


