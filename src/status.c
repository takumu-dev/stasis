#include "stasis.h"

const char *stasis_status_text(stasis_status status) {
    /* No default case, so the compiler warns about a code left out. */
    switch (status) {
    case STASIS_OK:
        return "success";
    case STASIS_TRAP:
        return "the script instance trapped";
    case STASIS_REFUSED_NESTED:
        return "refused: a script instance is already running on this thread";
    case STASIS_REFUSED_RUNNING:
        return "refused: the script instance is running";
    case STASIS_REFUSED_TRAPPED:
        return "refused: the script instance has trapped";
    case STASIS_REFUSED_NOT_STARTED:
        return "refused: the script instance hasn't started";
    case STASIS_REFUSED_STARTED:
        return "refused: the script instance has already started";
    case STASIS_REFUSED_ABSENT:
        return "refused: the script lacks that optional entry point";
    case STASIS_REFUSED_NOT_SNAPSHOTTABLE:
        return "refused: the script isn't snapshottable";
    case STASIS_REFUSED_IN_USE:
        return "refused: still in use";
    case STASIS_REFUSED_NOT_RUNNING:
        return "refused: the script instance isn't running on this thread";
    case STASIS_INVALID_MODULE:
        return "not a valid WebAssembly module";
    case STASIS_WRONG_INTERFACE:
        return "the script was built for another interface";
    case STASIS_INCOMPATIBLE_VERSION:
        return "the script was built for an incompatible version of the interface";
    case STASIS_INTERFACE_MISMATCH:
        return "the script doesn't match the interface";
    case STASIS_INVALID_SCRIPT:
        return "the module breaks Stasis's rules for scripts";
    case STASIS_WRONG_SCRIPT:
        return "the snapshot is of another script";
    case STASIS_SNAPSHOT_TOO_NEW:
        return "the snapshot's format is newer than this release of Stasis";
    case STASIS_INVALID_SNAPSHOT:
        return "the snapshot is malformed";
    case STASIS_INVALID_BINDINGS:
        return "the host objects given don't match the snapshot's live handles";
    case STASIS_OUT_OF_MEMORY:
        return "out of memory";
    case STASIS_INVALID_ARGUMENT:
        return "invalid argument";
    case STASIS_BUFFER_TOO_SMALL:
        return "buffer too small";
    case STASIS_STATUS_MAX_ENUM:
        break;
    }
    return "unknown status";
}
