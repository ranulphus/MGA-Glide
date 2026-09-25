/* glbind.c - bind the Glide function table to an OVL via leload. */
#include "glbind.h"
#include <stdio.h>
#include <stdlib.h>

glapi_t gl;

static void *lb_alloc(size_t n) { return calloc(1, n); }
static void lb_free(void *p) { free(p); }

static void *lb_read(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    void *buf;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static const le_io lb_io = { lb_alloc, lb_free, lb_read, NULL };

le_module *glbind_load(const char *path, int *missing, char *err, size_t errlen)
{
    le_module *m = le_load(&lb_io, path, err, errlen);
    void **slot = (void **)&gl;
    int i;
    *missing = 0;
    if (!m)
        return NULL;
    for (i = 0; i < GLAPI_COUNT; i++) {
        slot[i] = le_proc(m, glapi_decorated[i]);
        if (!slot[i])
            (*missing)++;
    }
    return m;
}
