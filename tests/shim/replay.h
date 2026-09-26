/* replay.h - GLPLAY internals shared with the generated dispatcher. */
#ifndef REPLAY_H
#define REPLAY_H
#include "glbind.h"
#include "api_ids.h"
#include <string.h>

extern uint8_t rp_scratch[];            /* output buffers of replayed calls */

float rp_f(uint32_t v);                 /* a recorded float argument */

/* Make recorded call 'op' with scalar args A and blob pointers B; the
 * return value (if any) goes to *ret. -1 for an unknown op. */
int rp_dispatch(int op, const uint32_t *A, void *const *B, uint32_t *ret);
#endif
