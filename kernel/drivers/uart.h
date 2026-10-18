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

/* E08: activo solo en el modo de transporte. Un unico productor (tracer)
 * escribe tramas completas; la interrupcion vacia la cola de TX. */
void uart_frame_mode_enable(void);
void uart_frame_mode_disable(void);
int uart_frame_write(const uint8_t *bytes, uint16_t length);
uint32_t uart_frame_free(void);
uint16_t uart_log_read(uint8_t *out, uint16_t capacity);
uint32_t uart_log_dropped(void);
void uart_tx_isr(void);

#endif
