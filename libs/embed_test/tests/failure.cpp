#include "embed_test/embed_test.hpp"
TEST_CASE(intentional_failure) {CHECK_EQ(1,2);CHECK_NEAR(1.0,2.0,0.01);}
EMBED_TEST_MAIN()
