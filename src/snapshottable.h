/* Whether a script is snapshottable: whether a snapshot can hold everything an instance of it
 * changes, which is its one linear memory and its mutable globals. */

#ifndef STASIS_SNAPSHOTTABLE_H
#define STASIS_SNAPSHOTTABLE_H

#include "message.h"
#include "module.h"

/* Adds a line to `reason` for each thing that keeps the module from being snapshottable, and
 * nothing when it is. The module is one that has passed the load check. */
void stasis_explain_unsnapshottable(const struct module *module, struct message *reason);

#endif
