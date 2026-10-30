/* A C script built against the C bindings of shapes.stasis alone, which passes values of each shape
 * through them, as shapes.stasis says. It returns strings of its arguments, which its entry points'
 * exports must copy before they free the arguments, and keeps the pair that `host.give` returns,
 * freeing the one before. */

#include "test_shapes_user.h"

#include <stdio.h>

/* The pair that `host.give` returned last. */
static test_shapes_host_pair given;

test_shapes_host_pair test_shapes_user_spill(test_shapes_host_pair a, test_shapes_host_pair b,
                                             test_shapes_host_pair c, test_shapes_host_pair d,
                                             test_shapes_host_pair e) {
    const test_shapes_host_wrapped taken = test_shapes_host_take(a, b, c, d, e);
    printf("took %a\n", taken.inner.value);
    test_shapes_host_pair_free(&given);
    given = test_shapes_host_give();
    test_shapes_host_pair result = given;
    result.right = e.right;
    return result;
}

TEST_SHAPES_USER_WRAP(test_shapes_host_wrapped value) { return test_shapes_host_tag(value); }

TEST_SHAPES_USER_ECHO(test_shapes_host_pair value) { return value; }
