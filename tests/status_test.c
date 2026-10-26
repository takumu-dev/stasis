/* The status codes of the public header, and stasis_status_text. */

#include <stasis.h>

#include "harness.h"

#include <limits.h>
#include <stddef.h>

/* Every code, with its number, which never changes, and its description. */
static const struct {
    stasis_status status;
    int number;
    const char *text;
} codes[] = {
    {STASIS_OK, 0, "success"},
    {STASIS_TRAP, 1, "the script instance trapped"},
    {STASIS_REFUSED_NESTED, 2, "refused: a script instance is already running on this thread"},
    {STASIS_REFUSED_RUNNING, 3, "refused: the script instance is running"},
    {STASIS_REFUSED_TRAPPED, 4, "refused: the script instance has trapped"},
    {STASIS_REFUSED_NOT_STARTED, 5, "refused: the script instance hasn't started"},
    {STASIS_REFUSED_STARTED, 6, "refused: the script instance has already started"},
    {STASIS_REFUSED_ABSENT, 7, "refused: the script lacks that optional entry point"},
    {STASIS_REFUSED_NOT_SNAPSHOTTABLE, 8, "refused: the script isn't snapshottable"},
    {STASIS_REFUSED_IN_USE, 9, "refused: still in use"},
    {STASIS_REFUSED_NOT_RUNNING, 10, "refused: the script instance isn't running on this thread"},
    {STASIS_INVALID_MODULE, 11, "not a valid WebAssembly module"},
    {STASIS_WRONG_INTERFACE, 12, "the script was built for another interface"},
    {STASIS_INCOMPATIBLE_VERSION, 13,
     "the script was built for an incompatible version of the interface"},
    {STASIS_INTERFACE_MISMATCH, 14, "the script doesn't match the interface"},
    {STASIS_INVALID_SCRIPT, 15, "the module breaks Stasis's rules for scripts"},
    {STASIS_WRONG_SCRIPT, 16, "the snapshot is of another script"},
    {STASIS_SNAPSHOT_TOO_NEW, 17, "the snapshot's format is newer than this release of Stasis"},
    {STASIS_INVALID_SNAPSHOT, 18, "the snapshot is malformed"},
    {STASIS_INVALID_BINDINGS, 19, "the host objects given don't match the snapshot's live handles"},
    {STASIS_OUT_OF_MEMORY, 20, "out of memory"},
    {STASIS_INVALID_ARGUMENT, 21, "invalid argument"},
    {STASIS_BUFFER_TOO_SMALL, 22, "buffer too small"},
};

enum { CODE_COUNT = sizeof codes / sizeof codes[0] };

TEST(every_code_keeps_its_number) {
    for (size_t i = 0; i < CODE_COUNT; i++) {
        ASSERT_INT_EQ(codes[i].status, codes[i].number);
    }
}

TEST(every_code_has_its_description) {
    for (size_t i = 0; i < CODE_COUNT; i++) {
        ASSERT_STR_EQ(stasis_status_text(codes[i].status), codes[i].text);
    }
}

/* Any other value, including the number the next code will take, is unknown. */
TEST(other_values_are_unknown) {
    const int others[] = {CODE_COUNT, CODE_COUNT + 1, 1000, INT_MAX, -1, INT_MIN};
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
        /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): out of range on purpose */
        ASSERT_STR_EQ(stasis_status_text((stasis_status)others[i]), "unknown status");
    }
}

TEST_LIST(TEST_CASE(every_code_keeps_its_number), TEST_CASE(every_code_has_its_description),
          TEST_CASE(other_values_are_unknown));
