#include <iostream>
#include <Eigen>
#include <unistd.h>
#include <string>
#include "mem_pattern_trace.h"


int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::cout << "Invocation: ./mmult_eigen <seed> <matrix_dim> <mat|vec|dot>" << std::endl;
        exit(-1);
    }
    const int MATRIX_DIM = atoi(argv[2]);
    std::string OPERATION = argv[3];

    {
    srand(atoi(argv[1]));
    std::cout << "starting!" << std::endl;
    syscall(mem_pattern_trace, TRACE_START | TRACE_AUTO);

    Eigen::MatrixXd a, b;
    if (OPERATION.compare("mat") == 0) {
	    a = Eigen::MatrixXd::Random(MATRIX_DIM, MATRIX_DIM);
	    b = Eigen::MatrixXd::Random(MATRIX_DIM, MATRIX_DIM);
    } else if (OPERATION.compare("vec") == 0) {
	    a = Eigen::MatrixXd::Random(MATRIX_DIM, MATRIX_DIM);
	    b = Eigen::MatrixXd::Random(MATRIX_DIM, 1);
    } else if (OPERATION.compare("dot") == 0) {
	    a = Eigen::MatrixXd::Random(1, MATRIX_DIM);
	    b = Eigen::MatrixXd::Random(MATRIX_DIM, 1);
    } else {
	    std::cerr << "wrong arguments" << std::endl;
	    std::cout << "Invocation: ./mmult_eigen <seed> <matrix_dim> <mat|vec|dot>" << std::endl;
	    exit(-1);
    }

    // Multiplying matrix a and b and storing in array mult
    Eigen::MatrixXd result;
    result = a * b;
    std::cout << result.sum() << std::endl;
    std::cout << "done, waiting for kernel cleanup" << std::endl;
    }
    syscall(mem_pattern_trace, TRACE_END);
    return 0;
}
