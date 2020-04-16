#ifndef __STRING_MATCHER_H__
#define __STRING_MATCHER_H__

#include <stdint.h>

#define MAX_STRING_LENGTH 	1024
#define MAX_RATE_BOUND		100

#define FRAGMENT_PKT		1
#define COMPLETE_PKT		2

extern int sun_shift[MAX_STRING_LENGTH];

void sunday_pre(uint8_t* pat, int len);
int sunday_match(uint8_t* target, int len_t, uint8_t* pat, int len_p);


#endif //#ifndef __STRING_MATCHER_H__
