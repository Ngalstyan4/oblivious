#include <iostream>
#include <Eigen>
#include <unistd.h>
#include <sys/mman.h>
#include "mem_pattern_trace.h"

const int PAGE_SIZE = 4096;

int main(int argc, char **argv)
{
	if (argc < 4) {
		std::cout << "Invocation: ./mmap_random_rw <seed> <num_pages> "
			     "<num_ops> <w|r>"
			  << std::endl;
		exit(-1);
	}

	const int SEED = atoi(argv[1]);
	const int NUM_PAGES = atoi(argv[2]);
	const int NUM_OPS = atoi(argv[3]);
	const bool WRITE = *argv[4] == 'w';
	srand(SEED);
	std::cout << "starting!" << std::endl;

	syscall(mem_pattern_trace, TRACE_START | TRACE_AUTO);
	char *result;
	{
		result = (char *)mmap(NULL, NUM_PAGES * PAGE_SIZE,
				      PROT_READ | PROT_WRITE,
				      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		//for (int i = 0; i < NUM_PAGES; i++)
		//	result[i * PAGE_SIZE] = 1;
		std::cout << "wrote on all pages" << std::endl;
		char dummy = '\0';
		for (int i = 0; i < NUM_OPS; i++) {
			int ind = rand() % (NUM_PAGES * PAGE_SIZE);
			if (WRITE)
				result[ind] = 0x7f;
			else
				dummy ^= result[ind];
				//for (int j = 0; j < 1000; j++); //<<-- adding this helps the synchronization
		}
		std::cout << "done!" << std::endl;
	}
	munmap(result, NUM_PAGES * PAGE_SIZE);
	syscall(mem_pattern_trace, TRACE_END);

	return 0;
}
