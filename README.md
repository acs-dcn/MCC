## MCC (Massive Client Connections)

### Introduction

MCC is a highly scalable and predictable network load generator. It can replicate flow-level network load utilizing BSD-like socket API provided by user-level TCP/IP stack. Thanks to the incredibly fast user-level stack, it can achieve 3.2 million concurrent TCP connections using only a single CPU core. 

### Release Notes

**V 1.0**

User-level-stack-based; Reactor pattern; multi-threaded; Distributor + Workers

### Architecture
 
 ![MCC_architecture](iamges/mcc_architecture.png)

### Prerequisites
Intel DPDK      
cmake3      
boost-devel      
bc      
pciutils      

### Included directories

./    
 |__ apps/ 		Sample applications      
 |__ conf/ 		Sample configuration file       
 |__ http/ 		HTTP parse library       
 |__ mtcp/ 		User-level stack      
 |__ scripts/ 	Scripts used for deployment      
 |__ src/ 		Source files    
 |__ tests/ 		Test examples     

**Introduction to examples**

The test subdirectory contains many tests example, check the code for more details.

+ conn_test: simulate connection setup, running in single-core mode.
+ delay_test: construct a single tcp flow and calculates the delay in ping-pong mode.
+ smp_tests: a multi-threaded work model unit tests.
+ accurate_ts: the accurate I/O test, a timestamp sequence must be provided for this test, you can use possion_gen in the same directory to generate one, or you can write your own.
+ possion_traffic_test: same as accurate_ts but using APP-level timer, a timestamp sequence must be provided

The apps subdirectory contains several generator apps using infgen framework, runtime options can be viewed using -h option, check the code for more details.

+ mcc: massive concurrent connections, simulate a large number of concurrent tcp connections scenerio, modify the payload content to get reasonable response from your server.
+ http_loader: a simple http protocol loader like wrk

### Installation

#### Downloads

```bash
$ ./ install_dependencies.sh
$ git submodule init
$ git submodule update
$ cd mtcp
$ git submodule init
$ git submodule update
```
#### Setup User-level stack 

(You may reference to https://github.com/mtcp-stack/mtcp for details)

```bash
$ cd mtcp/
$ ./build-dpdk.sh
6.3 Compile
$ cd ..
$ ./build.sh
```
There is a build_type option in build.sh to designate build type of the project. And the built executable file is put in directory $PWD/build/$build_type/

#### Run

Take http_loader for example:
```bash
$ cd build/release/apps/http_loader/
$ ./http_loader -c 4200 -d 60 --smp 11 --network-stack mtcp --dest 192.168.3.6
-c [ --conn ] arg (=100)       Total connections
-d [ --duration ] arg (=0)      Duration of test in seconds
--smp arg (=1)        		Number of threads (default: one per CPU)
--network-stack arg (=kernel) 	Select network stack (default: kernel stack)
--dest arg (=192.168.1.1)     	Destination IP
```

### Frequently asked questions
Xxx

### Contacts

wuwenqing@ict.ac.cn   convexcat@outlook.com

### References
> [1] Wu, Wenqing, et al. "MCC: a Predictable and Scalable Massive Client Load Generator."  International Symposium on Benchmarking, Measuring and Optimization. Springer, Cham, 2019.
