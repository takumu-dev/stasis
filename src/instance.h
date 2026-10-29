/* What the core's sources share of instance.c: creating an instance, which a restore does too,
 * before it puts the snapshot's state into it. */

#ifndef STASIS_INSTANCE_H
#define STASIS_INSTANCE_H

#include <stasis.h>

/* Creates an instance of `script` with `user_data`, as stasis_instance_create does once it has
 * checked its arguments, and sets `instance` to it. Returns STASIS_OK, or STASIS_OUT_OF_MEMORY,
 * which leaves `instance` alone. */
stasis_status stasis_instance_new(stasis_script *script, void *user_data,
                                  stasis_instance **instance);

#endif
