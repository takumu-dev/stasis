/* A test program whose cases all pass, for the harness's checks of a whole run. */

#include "harness.h"

TEST(first) { ASSERT_INT_EQ(1, 1); }

TEST(second) { ASSERT_INT_EQ(2, 2); }

TEST_LIST(TEST_CASE(first), TEST_CASE(second));
