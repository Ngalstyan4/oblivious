#include <iostream>
#include <sys/mman.h>
#include <Eigen>
#include <unistd.h>
#include "mem_pattern_trace.h"
// #define MADVISE 0 // << passed in from Makefile

void print_array(int array[], int len) {
	std::cout <<"array: [";
	for (int i = 0; i < len; i++) std::cout << array[i] << ", ";
	std::cout << "]" <<std::endl;
}


bool is_sorted(int array[], int len) {
	for (int i = 1; i < len; i++)
		if (array[i-1] > array[i] ) return false;
	return true;

}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::cout << "Invocation: ./mmult_eigen <seed> <array_length> <network?>" << std::endl;
        exit(-1);
    }
    const int ARRAY_LEN = atoi(argv[2]);
    const int USE_NETSORT = atoi(argv[3]);
    if ((ARRAY_LEN & (ARRAY_LEN - 1)) != 0) {
    	std::cout << "array_length must be a power of 2" << std::endl;
	exit(1);
    }

    int *array;
    {
    srand(atoi(argv[1]));
    syscall(mem_pattern_trace, TRACE_START | TRACE_AUTO);
    array = (int*) malloc(sizeof(int) * ARRAY_LEN);
    for (int i = 0; i < ARRAY_LEN; i++) array[i] = rand();

    // Algorithm taken:  https://en.wikipedia.org/wiki/Batcher_odd%E2%80%93even_mergesort
    // seems to work for arrays which power of 2 length

    // int num_evict_adv = 0;
    // int num_need_adv = 0;
    if (USE_NETSORT) {

	    for ( int p = 1; p < ARRAY_LEN; p <<= 1) {
		    for (int k = p; k >= 1; k >>= 1) {
#if MADVISE
			    int prev_k = k<<1;
			    if (prev_k <= p) {
			    	for (int j = prev_k % p; j <= ARRAY_LEN-1-prev_k; j += 2*prev_k) {
    					//madvise(&array[j], (prev_k-1) * sizeof(int), MADV_DONTNEED);
					//num_evict_adv++;
			    	}
				//std::cout << "evict adv: " << num_evict_adv;
				//num_evict_adv = 0;

			    }
			    for (int j = k % p; j <= ARRAY_LEN-1-k; j += 2*k) {
    				madvise(&array[j], (k-1) * sizeof(int), MADV_WILLNEED);
				//num_need_adv += 1;
			    }
				//std::cout << "need adv: " << num_need_adv << std::endl;
				//num_need_adv = 0;
#endif
			    for (int j = k % p; j <= ARRAY_LEN-1-k; j += 2*k) {
				    for (int i = 0; i <= /*ARRAY_LEN - j - k*/ k-1; i++) {
					    if ( (i+j)/(p*2) == (i+j+k)/(p*2)) {
						    int a1 = array[i+j];
						    int a2 = array[i+j+k];
						    int min = a1<a2 ? a1 : a2;
						    int max = a1>a2 ? a1 : a2;
						    array[i+j]   = min;
						    array[i+j+k] = max;
					    }

					}
				}
			}
		}

    } else {
    	std::sort(array, array+ARRAY_LEN);
    }



    syscall(mem_pattern_trace, TRACE_END);
    }

    if (! is_sorted(array, ARRAY_LEN)) {
	std::cout << "NOT sorted" << std::endl;
	exit(1);
    }
    return 0;
}
