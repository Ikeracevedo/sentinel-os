#include <sentinel/kprintf.h>
#include <stdarg.h>
#include <stdint.h>
#include "../drivers/uart.h"

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

    va_end(args);
}
