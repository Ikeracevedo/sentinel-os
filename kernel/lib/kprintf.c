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

static void print_unsigned(unsigned int value, unsigned int base) {
    char buf[12];
    int i = 0;

    if (value == 0) {
        uart_putc('0');
        return;
    }

    while (value > 0) {
        unsigned int digit = value % base;
        buf[i++] = (digit < 10) ? (char)('0' + digit) : (char)('a' + (digit - 10));
        value /= base;
    }

    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

static void print_signed(int value) {
    if (value < 0) {
        uart_putc('-');
        print_unsigned((unsigned int)(-value), 10);
    } else {
        print_unsigned((unsigned int)value, 10);
    }
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
        switch (*p) {
            case 'd':
                print_signed(va_arg(args, int));
                break;
            case 'u':
                print_unsigned(va_arg(args, unsigned int), 10);
                break;
            case 'x':
                print_unsigned(va_arg(args, unsigned int), 16);
                break;
            case 'p':
                uart_puts("0x");
                print_unsigned((unsigned int)(uintptr_t)va_arg(args, void *), 16);
                break;
            case 's':
                uart_puts(va_arg(args, const char *));
                break;
            case 'c':
                uart_putc((char)va_arg(args, int));
                break;
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
