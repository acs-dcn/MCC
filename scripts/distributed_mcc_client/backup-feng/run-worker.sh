#./worker -s 172.16.32.250 -l 172.16.163.102 -n 0 --device enp3s0f1 --dest 192.168.240.2 --smp 2
./worker -s 172.16.32.250 -l 172.16.32.128 -n 0 --network-stack mtcp  --dest 192.168.240.2 --smp 2 --log trace
