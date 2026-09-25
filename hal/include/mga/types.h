/* types.h - fixed-width types shared by the HAL and its clients.
 * Open Watcom, DJGPP and host gcc all provide <stdint.h>. */
#ifndef MGA_TYPES_H
#define MGA_TYPES_H
#include <stdint.h>
#include <stddef.h>

#if defined(__WATCOMC__) || defined(__GNUC__)
#  define MGA_INLINE static inline
#else
#  define MGA_INLINE static
#endif

#define MGA_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MGA_UNUSED(x) ((void)(x))

#endif
