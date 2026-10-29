/* What the test-only library adds to Stasis's API, for the few tests that need what the library
 * itself hides. */

#ifndef STASIS_TESTS_API_TESTING_H
#define STASIS_TESTS_API_TESTING_H

#include <stddef.h>
#include <stdint.h>

/* The number of allocations that Stasis and WAMR have made on the calling thread, reallocations
 * included. */
size_t stasis_testing_allocations(void);

/* The most handles an instance can be given over its life, which the test-only library lowers from
 * every 32-bit value but 0 so that a test can give an instance them all. */
uint32_t stasis_testing_handle_limit(void);

#endif
