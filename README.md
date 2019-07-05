## Destription

MCC

Distributed Traffic Generator

## Build
Currently the build scripts only support centos 7.0, modify `install_dependencies.sh` and `build.sh` 
to meet your os requirements.

First install the dependencies:
```
$sudo ./install_dependencies.sh
```
Warning: this scripts will try to change current linux kenrel, modify it if you don't want do that.
Then build the project using the scripts:
```
$sudo ./build.sh
```
There is a `build_type` option in `build.sh` to designate build type of the project. And the built 
executable file is put in directory `$PWD/build/$build_type/`

## Introduction to examples
The `test` subdirectory contains many tests example, check the code for more details. 
* conn_test: simulate connection setup, running in single-core mode.
* delay_test: construct a single tcp flow and calculates the delay in ping-pong mode.
* smp_tests: a multi-threaded work model unit tests.
* accurate_ts: the accurate I/O test, a timestamp sequence must be provided for this test, 
you can use possion_gen in the same directory to generate one, or you can write your own.
* possion_traffic_test: same as `accurate_ts` but using APP-level timer, a timestamp sequence
must be provided

The `apps` subdirectory contains several generator apps using infgen framework, runtime options
can be viewed using `-h` option, check the code for more details.
* mcc: massive concurrent connections, simulate a large number of concurrent tcp connections scenerio, 
modify the payload content to get reasonable response from your server.
* distributed_mcc: a distributed version of mcc, supports working in distributed environment
* http_loader: a simple http protocol loader like `wrk`
* distributed_http_loader: distributed version of `http_loader`

