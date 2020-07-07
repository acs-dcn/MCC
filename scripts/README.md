# Usage

1. mcc-setup.sh

	$ ./mcc-setup.sh <N> <command> <hosts file>
	(<command> can be set to "help" to get usage information)

2. scp_file.sh
 	
	$ ./scp_file.sh <source file> <dest directory> <hosts file>

3. run-workers.sh
	
	$ ./run-workers.sh <N> <command> <hosts file>
	(<command>: "start" for launching workers; "stop" for killing worker processes)

4. run-servers.sh
	
	$ ./run-servers.sh <N> <command> <hosts file>
	(<command>: "start" for launching servers; "stop" for killing server processes)

To use scripts, you may need to modify following configuration.

## common/

 1. Directories in mcc-setup.sh 
 2. Directoried in mtcp_setup.sh
 3. Configuration in dpdk-inputfile

## servers/
 1. Directories in server_test.sh
 2. Command line used for launching server


## distributed_mcc_client/
 
 1. IP address given to dpdk0 in load_test.sh
 2. Directory in load_test.sh
 3. IP address of NTP server

## distributed_http_loader/
 
 1. IP address given to dpdk0 in load_test.sh
 2. Directory in load_test.sh
 3. IP address of NTP server

## distributed_wan_loader/
 
 1. IP address given to dpdk0 in load_test.sh
 2. Directory in load_test.sh
 3. IP address of NTP server


