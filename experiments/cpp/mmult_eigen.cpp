#include <iostream>
#include <Eigen>
#include <unistd.h>
#include "mem_pattern_trace.h"


int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::cout << "Invocation: ./mmult_eigen <seed> <matrix_dim>" << std::endl;
        exit(-1);
    }
    const int MATRIX_DIM = atoi(argv[2]);
    {
    srand(atoi(argv[1]));
    std::cout << "starting!" << std::endl;
    syscall(mem_pattern_trace, TRACE_START | TRACE_AUTO);

    Eigen::MatrixXd a, b;
    a = Eigen::MatrixXd::Random(MATRIX_DIM, MATRIX_DIM);
    b = Eigen::MatrixXd::Random(MATRIX_DIM, MATRIX_DIM);

    // Multiplying matrix a and b and storing in array mult
    Eigen::MatrixXd result;
    result = a * b;
    std::cout << result.sum() << std::endl;
    std::cout << "done, waiting for kernel cleanup" << std::endl;
    }
    syscall(mem_pattern_trace, TRACE_END);
    return 0;
}
