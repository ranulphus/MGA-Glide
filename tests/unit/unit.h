/* unit.h - minimal host unit-test harness. */
#ifndef MGA_UNIT_H
#define MGA_UNIT_H
#include <stdio.h>
#include <string.h>

extern int unit_failures, unit_checks;

#define CHECK(cond) do { unit_checks++; if (!(cond)) { unit_failures++; \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); unit_checks++; \
    if (_a != _b) { unit_failures++; fprintf(stderr, "%s:%d: %s == %s failed: %lld vs %lld\n", \
    __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); unit_checks++; \
    if (strcmp(_a, _b)) { unit_failures++; fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", \
    __FILE__, __LINE__, _a, _b); } } while (0)

int unit_main(void);
#endif
