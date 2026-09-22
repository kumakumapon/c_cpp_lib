#include <iostream>
#include "embed_test/embed_test.hpp"
#include "sample_math/math_utils.hpp"

TEST_CASE(existing_suite) {

    std::cout << "# [Test] Running sample_math tests..." << std::endl;

    CHECK(sample_math::add(2, 3) == 5);
    CHECK(sample_math::add(-1, 1) == 0);
    CHECK(sample_math::multiply(3, 4) == 12);
    CHECK(sample_math::multiply(-2, 5) == -10);

    std::cout << "# [Test] All sample_math tests passed!" << std::endl;
    
}
EMBED_TEST_MAIN()
