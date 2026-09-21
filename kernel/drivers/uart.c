#include "uart.h"

#define UART_FIFO_REG     (*(volatile uint32_t *)0x3ff40000)
#define UART_INT_RAW_REG  (*(volatile uint32_t *)0x3ff40004)
#define UART_STATUS_REG   (*(volatile uint32_t *)0x3ff4001c)
#define UART_CLKDIV_REG   (*(volatile uint32_t *)0x3ff40014)
#define UART_INT_CLR_REG  (*(volatile uint32_t *)0x3ff40010)

/* Sin la inicializacion de arbol de relojes que hace ESP-IDF (PLL -> 80MHz
 * APB), el chip en bare-metal puro sigue corriendo directo del cristal
 * de 40MHz. Usar 80MHz aqui calcula un divisor equivocado y corrompe
 * cada byte transmitido -- confirmado en hardware real. */
#define UART_APB_CLK_FREQ  40000000u
#define UART_FIFO_LEN       128u
#define UART_TX_DONE_INT_RAW (1u << 14)

static void clock_settle_delay(void) {
    for (volatile uint32_t i = 0; i < 20000u; i++) {
    }
}

void uart_init(uint32_t baudrate) {
    /* Esperar a que el FIFO quede vacio Y a que el ultimo byte termine de
     * salir fisicamente por el registro de desplazamiento antes de tocar
     * CLKDIV -- si no, se corrompe la transmision en curso. */
    while (((UART_STATUS_REG >> 16) & 0xFFu) > 0) {
    }
    while ((UART_INT_RAW_REG & UART_TX_DONE_INT_RAW) == 0) {
    }
    UART_INT_CLR_REG = UART_TX_DONE_INT_RAW;

    UART_CLKDIV_REG = UART_APB_CLK_FREQ / baudrate;

    /* Margen para que el generador de baudrate se estabilice tras el
     * cambio de divisor -- mas notorio en saltos grandes (ej. a
     * 921600 baudios, divisor pequeno). Confirmado en hardware real. */
    clock_settle_delay();
}

void uart_putc(char c) {
    while (((UART_STATUS_REG >> 16) & 0xFFu) >= UART_FIFO_LEN) {
    }
    UART_FIFO_REG = (uint32_t)(unsigned char)c;
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

char uart_getc(void) {
    while ((UART_STATUS_REG & 0xFFu) == 0) {
    }
    return (char)(UART_FIFO_REG & 0xFFu);
}
