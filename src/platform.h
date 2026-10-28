/* What the core needs of the platform beneath it: Stasis's own lock, under which libraries and
 * instances are created, scripts loaded, and each destroyed, so that WAMR's process-wide state and
 * each object's count of children change one at a time. */

#ifndef STASIS_PLATFORM_H
#define STASIS_PLATFORM_H

/* Takes Stasis's lock, waiting while another thread holds it. It isn't recursive. */
void stasis_lock(void);

/* Gives Stasis's lock back. */
void stasis_unlock(void);

#endif
