#ifndef SENTINEL_SLICE_PARSE_H
#define SENTINEL_SLICE_PARSE_H

#include <stdint.h>

/* Parser puro para el quantum: comprueba el limite antes de multiplicar para
 * que una entrada enorme nunca pueda desbordar y parecer valida. */
static inline int slice_parse_ms(const char *text, uint32_t *ms) {
    if (text == 0 || ms == 0 || *text == '\0') {
        return -1;
    }
    uint32_t value = 0u;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        uint32_t digit = (uint32_t)(*p - '0');
        if (value > (10000u - digit) / 10u) {
            return -1;
        }
        value = value * 10u + digit;
    }
    if (value == 0u) {
        return -1;
    }
    *ms = value;
    return 0;
}

#endif
