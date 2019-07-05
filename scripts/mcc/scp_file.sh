Press ENTER or type command to continue
#!/bin/bash

nodes=$1
src=$2
dest=$3 
      
for ((i=1; i<=nodes; i++)); do
  ip=$((i+101))
  scp $src root@172.16.163.$ip:$dest <inputfile
done
