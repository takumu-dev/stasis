/* Compiled with -fshort-enums, which makes each enum as small as its values allow. A host's
 * foreign-function binding takes stasis_status to be 32 bits, whatever flags the host builds with.
 */

#include <stasis.h>

#include <stdint.h>

_Static_assert(sizeof(stasis_status) == sizeof(int32_t), "stasis_status must be 32 bits");
