#ifndef SENTINEL_UART_H
#define SENTINEL_UART_H

#include <stdint.h>

void uart_init(uint32_t baudrate);
void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc(void);
void uart_flush(void);

#endif
