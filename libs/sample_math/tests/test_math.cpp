#include <iostream>
#include <cassert>
#include "sample_math/math_utils.hpp"

int main() {
    std::cout << "[Test] Running sample_math tests..." << std::endl;

    assert(sample_math::add(2, 3) == 5);
    assert(sample_math::add(-1, 1) == 0);
    assert(sample_math::multiply(3, 4) == 12);
    assert(sample_math::multiply(-2, 5) == -10);

    std::cout << "[Test] All sample_math tests passed!" << std::endl;
    return 0;
}
