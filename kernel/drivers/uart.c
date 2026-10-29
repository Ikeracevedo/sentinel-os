#include "uart.h"
#ifndef SENTINEL_TRANSPORT
#define SENTINEL_TRANSPORT 0
#endif
#if SENTINEL_TRANSPORT
#include "../arch/xtensa/cpu.h"
#include <sentinel/task.h>
#endif

#define UART_FIFO_REG     (*(volatile uint32_t *)0x3ff40000)
/* ESP32: las escrituras al TX FIFO usan la ventana AHB. La ventana APB
 * anterior se conserva para leer RX (UART_FIFO_REG). */
#define UART_TX_FIFO_REG  (*(volatile uint32_t *)0x60000000)
#if SENTINEL_TRANSPORT
/* Reserva conservadora al llenar el FIFO desde la interrupcion. Las perdidas
 * observadas al recibir por USBIP/WSL tambien ocurrieron con TX bloqueante;
 * esta reserva no se considera la solucion de ese fallo. */
#define UART_FIFO_MARGIN     64u
#else
#define UART_FIFO_MARGIN     16u
#endif
#define UART_INT_RAW_REG  (*(volatile uint32_t *)0x3ff40004)
#define UART_STATUS_REG   (*(volatile uint32_t *)0x3ff4001c)
#define UART_CLKDIV_REG   (*(volatile uint32_t *)0x3ff40014)
#define UART_INT_CLR_REG  (*(volatile uint32_t *)0x3ff40010)
#if SENTINEL_TRANSPORT
#define UART_INT_ENA_REG  (*(volatile uint32_t *)0x3ff4000c)
#define UART_CONF1_REG    (*(volatile uint32_t *)0x3ff40024)
/* PRO_CPU UART0 -> interrupcion de CPU 1, nivel 1. Timer usa CPU 6. */
#define PRO_UART_INT_MAP_REG (*(volatile uint32_t *)0x3ff0018c)
#define UART_TX_EMPTY_BIT  (1u << 1)
#define UART_CPU_INT_BIT   (1u << 1)

#define TX_CAPACITY  2048u
#define TX_MASK      (TX_CAPACITY - 1u)
#define LOG_CAPACITY 4096u
#define LOG_MASK     (LOG_CAPACITY - 1u)

static uint8_t g_tx_ring[TX_CAPACITY];
static volatile uint32_t g_tx_head;
static volatile uint32_t g_tx_tail;
static uint8_t g_log_ring[LOG_CAPACITY];
static volatile uint32_t g_log_head;
static volatile uint32_t g_log_tail;
static volatile uint32_t g_log_dropped;
static volatile uint8_t g_frame_mode;
#endif

/* Sin la inicializacion de arbol de relojes que hace ESP-IDF (PLL -> 80MHz
 * APB), el chip en bare-metal puro sigue corriendo directo del cristal
 * de 40MHz. Confirmado en hardware real (HU-E01-05). */
#define UART_APB_CLK_FREQ  40000000u
#define UART_FIFO_LEN       128u
#define UART_TX_DONE_INT_RAW (1u << 14)

static void clock_settle_delay(void) {
    for (volatile uint32_t i = 0; i < 20000u; i++) {
    }
}

void uart_flush(void) {
#if SENTINEL_TRANSPORT
    if (g_frame_mode) {
        while (g_tx_tail != g_tx_head) {
        }
    }
#endif
    while (((UART_STATUS_REG >> 16) & 0xFFu) > 0) {
    }
    while ((UART_INT_RAW_REG & UART_TX_DONE_INT_RAW) == 0) {
    }
    UART_INT_CLR_REG = UART_TX_DONE_INT_RAW;
}

void uart_init(uint32_t baudrate) {
    uart_flush();
    /* ESP32 UART_CLKDIV: entero en bits 19:0 y fraccion /16 en bits 23:20.
     * A 921600, usar solo el entero 43 genera ~930233 baudios (+0,94%).
     * 43 + 6/16 genera ~922190 (+0,06%). */
    uint32_t divider_16 = (UART_APB_CLK_FREQ * 16u + baudrate / 2u) / baudrate;
    UART_CLKDIV_REG = (divider_16 >> 4) | ((divider_16 & 0xFu) << 20);
    clock_settle_delay();
}

/* Bytes entregados al FIFO desde el arranque: permite medir cuantos se pierden
 * entre el chip y el PC comparando contra los que realmente llegan. */
static volatile uint32_t g_tx_count;

int uart_rx_ready(void) {
    return (UART_STATUS_REG & 0xFFu) != 0u;
}

int uart_rx_byte(void) {
    if (!uart_rx_ready()) {
        return -1;
    }
    return (int)(UART_FIFO_REG & 0xFFu);
}

uint32_t uart_tx_count(void) {
    return g_tx_count;
}

void uart_putc(char c) {
#if SENTINEL_TRANSPORT
    if (g_frame_mode) {
        /* El shell puede producir `help` completo antes de que el tracer
         * vuelva a ejecutarse. Perder texto de una consola es peor que
         * retrasar brevemente su impresion: en contexto de tarea cedemos la
         * CPU un tick para que el tracer drene la cola. Nunca esperamos
         * dentro de una IRQ o de una seccion critica, donde bloquear
         * comprometeria el scheduler. */
        for (;;) {
            uint32_t ps = irq_disable();
            uint32_t head = g_log_head;
            if (head - g_log_tail < LOG_CAPACITY) {
                g_log_ring[head & LOG_MASK] = (uint8_t)c;
                __atomic_thread_fence(__ATOMIC_RELEASE);
                g_log_head = head + 1u;
                irq_restore(ps);
                return;
            }
            irq_restore(ps);
            if ((ps & 0x0Fu) != 0u) {
                g_log_dropped++;
                return;
            }
            /* No hacer busy-wait: shell (p4) tendria prioridad sobre tracer
             * (p6) y le impediria liberar espacio. Al dormir queda BLOCKED y
             * el planificador puede ejecutar al consumidor de la cola. */
            task_sleep(1u);
        }
    }
#endif
    while (((UART_STATUS_REG >> 16) & 0xFFu) >= (UART_FIFO_LEN - UART_FIFO_MARGIN)) {
    }
    UART_TX_FIFO_REG = (uint32_t)(unsigned char)c;
    g_tx_count++;
}

#if SENTINEL_TRANSPORT
void uart_frame_mode_enable(void) {
    uart_flush();                 /* el texto de arranque termina antes de HELLO */
    uint32_t ps = irq_disable();
    g_tx_head = g_tx_tail = 0u;
    g_log_head = g_log_tail = 0u;
    g_log_dropped = 0u;
    /* TXFIFO_EMPTY_THRHD son los bits 14:8 de UART_CONF1. */
    UART_CONF1_REG = (UART_CONF1_REG & ~(0x7Fu << 8)) | (32u << 8);
    UART_INT_ENA_REG = 0u;  /* RX sigue por polling; no heredar IRQ del ROM */
    UART_INT_CLR_REG = UART_TX_EMPTY_BIT;
    PRO_UART_INT_MAP_REG = 1u;
    uint32_t enabled;
    __asm__ __volatile__("rsr %0, INTENABLE" : "=r"(enabled));
    enabled |= UART_CPU_INT_BIT;
    __asm__ __volatile__("wsr %0, INTENABLE; rsync" :: "r"(enabled));
    g_frame_mode = 1u;
    irq_restore(ps);
}

void uart_frame_mode_disable(void) {
    uint32_t ps = irq_disable();
    UART_INT_ENA_REG = 0u;
    g_frame_mode = 0u;
    irq_restore(ps);
}

uint32_t uart_frame_free(void) {
    return TX_CAPACITY - (g_tx_head - g_tx_tail);
}

int uart_frame_write(const uint8_t *bytes, uint16_t length) {
    if (!g_frame_mode || bytes == 0 || length == 0u ||
        length > TX_CAPACITY || uart_frame_free() < length) {
        return 0;
    }
    /* Tracer es el unico productor. ISR solo ve g_tx_head tras el release. */
    uint32_t head = g_tx_head;
    for (uint16_t i = 0; i < length; i++) {
        g_tx_ring[(head + i) & TX_MASK] = bytes[i];
    }
    uint32_t ps = irq_disable();
    __atomic_thread_fence(__ATOMIC_RELEASE);
    g_tx_head = head + length;
    UART_INT_CLR_REG = UART_TX_EMPTY_BIT;
    UART_INT_ENA_REG |= UART_TX_EMPTY_BIT;
    irq_restore(ps);
    return 1;
}

uint16_t uart_log_read(uint8_t *out, uint16_t capacity) {
    if (out == 0) {
        return 0;
    }
    uint16_t n = 0;
    while (n < capacity && g_log_tail != g_log_head) {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        out[n++] = g_log_ring[g_log_tail & LOG_MASK];
        g_log_tail++;
    }
    return n;
}

uint32_t uart_log_dropped(void) {
    return g_log_dropped;
}

uint32_t uart_log_pending(void) {
    return g_log_head - g_log_tail;
}

void uart_tx_isr(void) {
    if ((UART_INT_RAW_REG & UART_TX_EMPTY_BIT) == 0u) {
        return;
    }
    UART_INT_CLR_REG = UART_TX_EMPTY_BIT;
    uint32_t count = (UART_STATUS_REG >> 16) & 0xFFu;
    while (g_tx_tail != g_tx_head && count < UART_FIFO_LEN - UART_FIFO_MARGIN) {
        UART_TX_FIFO_REG = g_tx_ring[g_tx_tail & TX_MASK];
        g_tx_tail++;
        g_tx_count++;
        count++;
    }
    if (g_tx_tail == g_tx_head) {
        UART_INT_ENA_REG &= ~UART_TX_EMPTY_BIT;
    }
}
#endif

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
