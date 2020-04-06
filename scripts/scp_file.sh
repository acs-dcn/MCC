#!/bin/bash

src=$1
dest=$2 
filename="$3"
      
for ip in `cat $filename`; do
  scp -r $src root@$ip:$dest
done
