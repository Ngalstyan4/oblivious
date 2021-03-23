#include <iostream>
#include "modelsimpl.h"
#include <torch/torch.h>

#include "../mem_pattern_trace.h"
namespace vision
{
namespace models
{
// build a neural network similar to how you would do it with Pytorch

struct Model : torch::nn::Module {

	// Constructor
	Model()
	{
		auto num_classes = 1000;

		features = torch::nn::Sequential(
			torch::nn::Conv2d(torch::nn::Conv2dOptions(3, 64, 11)
						  .stride(4)
						  .padding(2)),
			torch::nn::Functional(modelsimpl::relu_),
			torch::nn::Functional(modelsimpl::max_pool2d, 3, 2),
			torch::nn::Conv2d(torch::nn::Conv2dOptions(64, 192, 5)
						  .padding(2)),
			torch::nn::Functional(modelsimpl::relu_),
			torch::nn::Functional(modelsimpl::max_pool2d, 3, 2),
			torch::nn::Conv2d(torch::nn::Conv2dOptions(192, 384, 3)
						  .padding(1)),
			torch::nn::Functional(modelsimpl::relu_),
			torch::nn::Conv2d(torch::nn::Conv2dOptions(384, 256, 3)
						  .padding(1)),
			torch::nn::Functional(modelsimpl::relu_),
			torch::nn::Conv2d(torch::nn::Conv2dOptions(256, 256, 3)
						  .padding(1)),
			torch::nn::Functional(modelsimpl::relu_),
			torch::nn::Functional(modelsimpl::max_pool2d, 3, 2));
		// construct and register your layers
		classifier = torch::nn::Sequential(
			torch::nn::Dropout(),
			torch::nn::Linear(256 * 6 * 6, 4096),
			torch::nn::Functional(torch::relu),
			torch::nn::Dropout(), torch::nn::Linear(4096, 4096),
			torch::nn::Functional(torch::relu),
			torch::nn::Linear(4096, num_classes));

		register_module("features", features);
		register_module("classifier", classifier);
	}

	// the forward operation (how data will flow from layer to layer)
	torch::Tensor forward(torch::Tensor x)
	{
		// let's pass relu
		x = features->forward(x);
		x = torch::adaptive_avg_pool2d(x, { 6, 6 });
		x = x.view({ x.size(0), -1 });
		x = classifier->forward(x);

		// return the output
		return x;
	}

	torch::nn::Sequential features{ nullptr }, classifier{ nullptr },
		out{ nullptr };
};
} // namespace models
} // namespace vision

using namespace vision::models;
int main()
{
	srand(42);

	syscall(mem_pattern_trace, TRACE_START | TRACE_AUTO);
	Model model;

	auto in = torch::rand({ 4, 3, 1024, 768 });

	auto begin = std::chrono::high_resolution_clock::now();
	for (auto i = 0; i < 20; i++) {
		std::cout << "iter " << i << std::endl;
		auto out = model.forward(in);
	}

	auto end = std::chrono::high_resolution_clock::now();
	std::cout << std::chrono::duration_cast<std::chrono::nanoseconds>(
			     end -
			     begin).count() /
			     1000000
		  << "ms" << std::endl;
	syscall(mem_pattern_trace, TRACE_END);
}

