/* Prueba E08 en placa: primera transmision de tramas, sin shell concurrente.
 * Se compila solo con FRAME_BOARD_TEST=1. UART es bloqueante aqui a proposito:
 * primero validamos bytes, CRC y resync; la cola/ISR se implementa despues. */
#include <sentinel/frame.h>
#include <sentinel/task.h>
#include <sentinel/trace.h>
#include <stdint.h>
#include "../kernel/drivers/uart.h"

#ifndef FRAME_BOARD_STRESS
#define FRAME_BOARD_STRESS 0
#endif
#ifndef FRAME_BOARD_DRAIN_CHUNK
#define FRAME_BOARD_DRAIN_CHUNK 0u
#endif
#ifndef FRAME_BOARD_STRESS_SLEEP_MS
#define FRAME_BOARD_STRESS_SLEEP_MS 10u
#endif

static void send_frame(uint8_t kind, uint16_t seq,
                       const uint8_t *payload, uint16_t length) {
    uint8_t wire[FRAME_MAX_BYTES];
    uint16_t bytes = frame_encode(wire, sizeof(wire), kind, seq, payload, length);
    for (uint16_t i = 0; i < bytes; i++) {
        uart_putc((char)wire[i]);
#if FRAME_BOARD_DRAIN_CHUNK > 0
        /* Diagnostico: fuerza una pausa despues de cada bloque de bytes. */
        if ((i + 1u) % FRAME_BOARD_DRAIN_CHUNK == 0u || i + 1u == bytes) {
            uart_flush();
        }
#endif
    }
}

static void frame_board_task(void *arg) {
    (void)arg;
#if FRAME_BOARD_STRESS
    static const uint8_t hello[] = {1u, 'E','0','8','-','s','t','r','e','s','s'};
    static const uint8_t log[] = "E08: flujo bloqueante de referencia\r\n";
    uint16_t frame_seq = 0u;
    uint32_t sample = 0u;
    send_frame(FRAME_HELLO, frame_seq++, hello, sizeof(hello));
    send_frame(FRAME_LOG, frame_seq++, log, sizeof(log) - 1u);
    for (;;) {
        trace_event_t events[10];
        for (uint32_t i = 0; i < 10u; i++) {
            events[i].cycles = sample * 400000u + i;
            events[i].type = EV_SWITCH_OUT;
            events[i].tid = 1u;
            events[i].seq = (uint16_t)(sample * 10u + i);
            events[i].arg0 = sample;
            events[i].arg1 = i;
        }
        send_frame(FRAME_EVENTS, frame_seq++, (const uint8_t *)events,
                   sizeof(events));
        sample++;
        task_sleep(FRAME_BOARD_STRESS_SLEEP_MS);
    }
#else
    static const uint8_t hello[] = {1u, 'E','0','8','-','b','o','a','r','d'};
    static const uint8_t log[] = "E08: trama valida y SEQ wrap OK\r\n";
    for (;;) {
        /* Repetir permite abrir bridge.py despues del flasheo sin depender
         * de acertar una ventana de un segundo ni pulsar RESET. */
        task_sleep(2000u);
        send_frame(FRAME_HELLO, 65534u, hello, sizeof(hello));

        trace_event_t events[16];
        uint16_t count = 0;
        while (count < 16u && trace_pop(&events[count])) {
            count++;
        }
        if (count != 0u) {
            send_frame(FRAME_EVENTS, 65535u, (const uint8_t *)events,
                       (uint16_t)(count * sizeof(trace_event_t)));
        } else {
            send_frame(FRAME_HEARTBEAT, 65535u, 0, 0u);
        }

        send_frame(FRAME_LOG, 0u, log, sizeof(log) - 1u);
        for (uint16_t seq = 1u; seq <= 5u; seq++) {
            task_sleep(1000u);
            send_frame(FRAME_HEARTBEAT, seq, 0, 0u);
        }
        uart_flush();
    }
#endif
}

void frame_board_start(void) {
    (void)task_create("frame-test", frame_board_task, 0, 6u, 0u);
}
