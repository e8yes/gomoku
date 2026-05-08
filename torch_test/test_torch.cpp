#include <torch/torch.h>
#include <iostream>

int main() {
    if (torch::cuda::is_available()) {
        std::cout << "CUDA is available! Training on GPU." << std::endl;
    } else {
        std::cout << "CUDA not found. Training on CPU." << std::endl;
    }
    return 0;
}
