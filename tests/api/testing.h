/* What the test-only library adds to Stasis's API, for the few tests that need what the library
 * itself hides. */

#ifndef STASIS_TESTS_API_TESTING_H
#define STASIS_TESTS_API_TESTING_H

#include <stddef.h>

/* The number of allocations that Stasis and WAMR have made on the calling thread, reallocations
 * included. */
size_t stasis_testing_allocations(void);

#endif
