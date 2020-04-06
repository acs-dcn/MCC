#!/bin/zsh
interface=$1
command=$2

for i in {3..250}
do 
  ifconfig $interface:$i 192.168.1.$i $command;
done

