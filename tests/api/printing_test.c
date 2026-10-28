/* Where WAMR's printing goes when a script traps. WAMR prints the call stack of every trap, which
 * the library discards, and the test-only library sends to the program's output. This program is
 * built against each, and the tests printing.* check what it prints. */

#include <stasis.h>

#include "harness.h"
#include "support.h"
#include "test_traps.h"

TEST(traps) {
    stasis_library *library = create_library();
    stasis_script *script = load_script(library, &test_traps_interface, "traps");
    stasis_instance *instance = start_instance(script);
    ASSERT_INT_EQ(test_traps_trapper_unreachable(instance), STASIS_TRAP);
    destroy_all(instance, script, library);
}

TEST_LIST(TEST_CASE(traps));
