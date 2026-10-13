#include <sentinel/kprintf.h>
#include <stdarg.h>
#include <stdint.h>
#include "../drivers/uart.h"
#include "../arch/xtensa/cpu.h"

/* EXPERIMENTO: 1 = cada kprintf corre con las interrupciones apagadas. Si con 1
 * dejan de perderse bytes, la perdida depende de que el tick caiga a mitad de una
 * impresion (ruta de interrupcion / spill). En produccion debe ser 0: apagar las
 * interrupciones durante milisegundos arruina la latencia. */
#define KPRINTF_IRQ_OFF 0

/* Formatos: %d %u %x %s %c %p %%, con ancho minimo opcional y '-' para alinear a la
 * izquierda (por ejemplo %5u, %-10s). Sin ceros a la izquierda ni precision. */

/* Escribe value en buf (sin '\0') y devuelve cuantos caracteres. buf >= 11 bytes. */
static uint32_t format_unsigned(char *buf, unsigned int value, unsigned int base) {
    char tmp[12];
    uint32_t n = 0;
    if (value == 0) {
        tmp[n++] = '0';
    }
    while (value > 0) {
        unsigned int digit = value % base;
        tmp[n++] = (digit < 10) ? (char)('0' + digit) : (char)('a' + (digit - 10));
        value /= base;
    }
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = tmp[n - 1u - i];
    }
    return n;
}

static void put_padded(const char *s, uint32_t len, uint32_t width, int left) {
    if (!left) {
        for (uint32_t i = len; i < width; i++) {
            uart_putc(' ');
        }
    }
    for (uint32_t i = 0; i < len; i++) {
        uart_putc(s[i]);
    }
    if (left) {
        for (uint32_t i = len; i < width; i++) {
            uart_putc(' ');
        }
    }
}

static uint32_t cstr_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
#if KPRINTF_IRQ_OFF
    uint32_t saved_ps = irq_disable();
#endif

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            uart_putc(*p);
            continue;
        }

        p++;
        int left = 0;
        uint32_t width = 0;
        if (*p == '-') {
            left = 1;
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10u + (uint32_t)(*p - '0');
            p++;
        }

        if (*p == '\0') {
            uart_putc('%');          /* formato truncado: "...%" al final */
            break;
        }
        char num[16];
        switch (*p) {
            case 'd': {
                int v = va_arg(args, int);
                uint32_t n = 0;
                if (v < 0) {
                    num[n++] = '-';
                    n += format_unsigned(num + n, (unsigned int)(-v), 10);
                } else {
                    n = format_unsigned(num, (unsigned int)v, 10);
                }
                put_padded(num, n, width, left);
                break;
            }
            case 'u':
                put_padded(num, format_unsigned(num, va_arg(args, unsigned int), 10), width, left);
                break;
            case 'x':
                put_padded(num, format_unsigned(num, va_arg(args, unsigned int), 16), width, left);
                break;
            case 'p': {
                uart_puts("0x");
                uint32_t n = format_unsigned(num, (unsigned int)(uintptr_t)va_arg(args, void *), 16);
                put_padded(num, n, width, left);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                put_padded(s, cstr_len(s), width, left);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                put_padded(&c, 1, width, left);
                break;
            }
            case '%':
                uart_putc('%');
                break;
            default:
                uart_putc('%');
                uart_putc(*p);
                break;
        }
    }

#if KPRINTF_IRQ_OFF
    irq_restore(saved_ps);
#endif
    va_end(args);
}
