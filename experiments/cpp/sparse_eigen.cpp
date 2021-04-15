#include <iostream>
#include <Eigen>
#include <unistd.h>
//#include "utils.hpp"
#include "mem_pattern_trace.h"

void random_sparse_matrix(Eigen::SparseMatrix<double>& matrix, int num_rows, int num_cols)
{
    std::vector<Eigen::Triplet<double>> triplets;
    for (int i = 0; i != num_rows; i++) {
        for (int j = 0; j != num_cols; j++) {
            int num = rand() % 128;
            if (num <= 12) {
                triplets.push_back(Eigen::Triplet<double>(i, j, num / 12.0));
            }
        }
    }
    matrix.setFromTriplets(triplets.begin(), triplets.end());
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        std::cout << "Invocation: ./sparse_eigen <seed> <matrix_dim> <trace>" << std::endl;
        exit(-1);
    }
    const int MATRIX_DIM = atoi(argv[2]);
    {
    srand(atoi(argv[1]));
    std::cout << "starting!" << std::endl;
    syscall(mem_pattern_trace, TRACE_START | TRACE_AUTO);

    Eigen::SparseMatrix<double> a(MATRIX_DIM, MATRIX_DIM);
    Eigen::SparseMatrix<double> b(MATRIX_DIM, MATRIX_DIM);

    random_sparse_matrix(a, MATRIX_DIM, MATRIX_DIM);
    random_sparse_matrix(b, MATRIX_DIM, MATRIX_DIM);

    std::cout << a.nonZeros() << " " << b.nonZeros() << std::endl;

    // Multiplying matrix a and b and storing in array mult
    Eigen::SparseMatrix<double> result;
    result = a * b;
    std::cout << result.sum() << std::endl;
    std::cout << "done, waiting for kernel cleanup" << std::endl;
    }
    syscall(mem_pattern_trace, TRACE_END);
    return 0;
}
