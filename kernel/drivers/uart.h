#ifndef SENTINEL_UART_H
#define SENTINEL_UART_H

#include <stdint.h>

void uart_init(uint32_t baudrate);
void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc(void);
void uart_flush(void);
uint32_t uart_tx_count(void);   /* bytes enviados desde el arranque */
int  uart_rx_ready(void);       /* hay un byte esperando en el FIFO de recepcion */
int  uart_rx_byte(void);        /* lo lee (sin bloquear); -1 si no hay */

#endif
