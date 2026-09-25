/* leload against the retail OVLs (local fixtures) and our own build.
 * Pointer-sized fixup values are only meaningful on a 32-bit target; here
 * we check parsing, fixup application counts and export resolution. */
#include "unit.h"
#include "leload.h"
#include "glapi.h"
#include <stdio.h>
#include <stdlib.h>

static void *a(size_t n) { return calloc(1, n); }
static void f(void *p) { free(p); }
static void *rd(const char *path, size_t *size)
{
    FILE *fp = fopen(path, "rb");
    long n;
    void *b;
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    b = malloc((size_t)n);
    if (fread(b, 1, (size_t)n, fp) != (size_t)n) { free(b); b = NULL; }
    fclose(fp);
    *size = (size_t)n;
    return b;
}
static const le_io io = { a, f, rd, NULL };

static void check_ovl(const char *path, int want_fixups, int need_all)
{
    char err[128] = "";
    le_module *m = le_load(&io, path, err, sizeof err);
    int i, missing = 0;
    if (!m) {
        fprintf(stderr, "skip/err %s: %s\n", path, err);
        CHECK(!need_all);
        return;
    }
    CHECK_STR(le_module_name(m), "glide2x");
    CHECK(le_module_flags(m) & 0x8000);
    if (want_fixups)
        CHECK_EQ(le_fixup_count(m), want_fixups);
    for (i = 0; i < GLAPI_COUNT; i++)
        if (!le_proc(m, glapi_decorated[i]))
            missing++;
    printf("%s: objects=%d fixups=%d missing=%d\n", path, le_object_count(m), le_fixup_count(m), missing);
    if (need_all)
        CHECK_EQ(missing, 0);
    else
        CHECK(missing <= 2);        /* SR lacks the two guFb* region calls */
    CHECK(le_proc(m, "_NOSUCHNAME@0") == NULL);
    le_unload(m);
}

int unit_main(void)
{
    char p[512];
    const char *fx = getenv("FIXTURES_DIR");
    check_ovl("build/ow/GLIDE2X.OVL", 0, 1);
    if (!fx)
        fx = "fixtures-missing";
    snprintf(p, sizeof p, "%s/ovl/gta.ovl", fx);
    check_ovl(p, 3669, 0);
    snprintf(p, sizeof p, "%s/ovl/sr.ovl", fx);
    check_ovl(p, 0, 0);
    return 0;
}
