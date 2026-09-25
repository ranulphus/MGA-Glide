/* glbind.h - fill a glapi_t from an OVL loaded with leload. */
#ifndef GLBIND_H
#define GLBIND_H
#include "glapi.h"
#include "leload.h"

extern glapi_t gl;

/* Load 'path' and bind every Glide export. Returns the module or NULL;
 * *missing receives the number of unresolved names. */
le_module *glbind_load(const char *path, int *missing, char *err, size_t errlen);
#endif
