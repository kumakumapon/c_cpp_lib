#include <iostream>
#include "sample_math/math_utils.hpp"

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Hello from cli_demo!" << std::endl;
    std::cout << "========================================" << std::endl;

    int a = 10;
    int b = 25;

    std::cout << "a = " << a << ", b = " << b << std::endl;
    std::cout << "sample_math::add(a, b)      = " << sample_math::add(a, b) << std::endl;
    std::cout << "sample_math::multiply(a, b) = " << sample_math::multiply(a, b) << std::endl;

    return 0;
}
