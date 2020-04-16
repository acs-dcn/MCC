#ifdef __cplusplus
extern "C"
{
#endif
#include "gen_api.h"
#ifdef __cplusplus
}
#endif

#include <iostream>
#include <string>
using namespace std;

int main(int argc, char** argv) {

	/// Initialize stream generator
	gen_init(argc, argv);

	/// Run main thread
	gen_run();  //nids_run() -> send_streams
	
	/// Release resources
	gen_destroy();

	return 0;
}
