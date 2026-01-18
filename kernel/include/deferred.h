#ifndef DEFERRED_H
#define DEFERRED_H

#include <cldtypes.h>

typedef void (*deferred_fn_t)(void *arg);

// Schedule a function to run outside interrupt context.
// Returns 0 on success, non-zero if queue full.
int deferred_schedule(deferred_fn_t fn, void *arg);

// Process one pending deferred task, if any.
void deferred_process_one(void);

// Process all pending tasks.
void deferred_process_all(void);

// Whether there are tasks pending.
int deferred_has_pending(void);

#endif // DEFERRED_H

