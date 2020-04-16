#include <string.h>
#include "include/string_matcher.h"

int sun_shift[MAX_STRING_LENGTH];


/* *
 * Description: String matching with "Sunday Algorithm"
 *
 * @param sun_shift Shifting distance of each character if mis-matching
 * @param pat 		String needs matching
 * @param len 		Length of 'str'
 * */
void sunday_pre(uint8_t* pat, int len)
{
    int i;
    
    for (i = 0; i < MAX_STRING_LENGTH; i++) {
		sun_shift[i] = len + 1;
	}
 
    for (i = 0; i < len; i++){
        sun_shift[pat[i]] = len - i;
    }
}
 
/* *
 * Description: Sunday Algorithm for string matching
 * 
 * @param target	Target string
 * @param len_t 	Length of target
 * @param pat 		String need matching
 * @param len_p		Length of pat
 *
 * @return 	Index of the next element after matched string in target
 * */
int sunday_match(uint8_t* target, int len_t, uint8_t* pat, int len_p)
{
	int i;
    int pos = 0;
    int j;

//	sunday_pre(pat, len_p);
    while(pos <= len_t - len_p) {
        j = 0;
        while(target[pos+j] == pat[j] && j < len_p) 
			j++;
        if(j >= len_p){
            //return &target[pos];//Succeed in matching
            return (pos + len_p);//Succeed in matching
        }
        else{
			pos += sun_shift[target[pos+len_p]];
		}
    }
    return -1;
}

