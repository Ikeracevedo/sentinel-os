#ifndef SENTINEL_CONTEXT_H
#define SENTINEL_CONTEXT_H

#include <stdint.h>
#include <stddef.h>

/* Bits de PS (esp-idf xtensa/corebits.h) */
#define PS_EXCM          0x00000010u
#define PS_UM            0x00000020u
#define PS_WOE           0x00040000u
#define PS_CALLINC(n)    ((uint32_t)((n) & 3u) << 16)

#define CTX_BASE_SAVE_AREA  16u
#define CTX_FRAME_SIZE      96u

typedef struct {
    uint32_t unused0;   /*  +0 */
    uint32_t a0;        /*  +4 */
    uint32_t a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15; /* +8..+60 */
    uint32_t ps;        /* +64 */
    uint32_t epc1;      /* +68: PC al que vuelve rfe */
    uint32_t pad[2];    /* +72, +76 */
} ctx_frame_t;

_Static_assert(sizeof(ctx_frame_t) == 80, "el assembly asume un marco de 80 bytes");
_Static_assert(offsetof(ctx_frame_t, a0) == 4, "a0 en +4");
_Static_assert(offsetof(ctx_frame_t, a2) == 8, "a2 en +8");
_Static_assert(offsetof(ctx_frame_t, a15) == 60, "a15 en +60");
_Static_assert(offsetof(ctx_frame_t, ps) == 64, "ps en +64");
_Static_assert(offsetof(ctx_frame_t, epc1) == 68, "epc1 en +68");
_Static_assert(sizeof(ctx_frame_t) + CTX_BASE_SAVE_AREA == CTX_FRAME_SIZE, "marco + area base");

#endif