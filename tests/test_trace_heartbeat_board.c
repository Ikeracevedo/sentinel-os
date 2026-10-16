#include <sentinel/kprintf.h>
#include <sentinel/task.h>
#include <sentinel/trace.h>
#include <stdint.h>

#include "../kernel/arch/xtensa/cpu.h"
#include "../kernel/core/time.h"

#define STOP_MS 320u

static void heartbeat_board_task(void *arg) {
    (void)arg;
    trace_event_t event;
    while (trace_pop(&event) == 1) {
    }

    /* trace off silencia las sondas, pero ADR-008 exige seguir latiendo. */
    trace_set_enabled(0);
    uint32_t filter_ps = irq_disable();
    uint32_t filter_before = trace_emitted();
    trace_probe_emit(EV_ALLOC, 0u, 1u, 1u);
    uint32_t filter_ok = (trace_emitted() == filter_before);
    irq_restore(filter_ps);

    uint64_t idle_before = task_get(0u)->cpu_cycles;
    task_sleep(350u);
    uint64_t idle_after = task_get(0u)->cpu_cycles;

    uint32_t normal_count = 0u;
    uint32_t interval_errors = 0u;
    uint32_t last_tick = 0u;
    while (trace_pop(&event) == 1) {
        if (event.type != EV_HEARTBEAT) {
            continue;
        }
        if (event.tid != 0u || event.arg1 != 0u) {
            interval_errors++;
        }
        if (normal_count != 0u && event.arg0 - last_tick != 100u) {
            interval_errors++;
        }
        last_tick = event.arg0;
        normal_count++;
    }

    /* Forzar una perdida y comprobar que un latido posterior la informa. */
    while (trace_count() < TRACE_BUFFER_CAPACITY) {
        trace_emit(EV_INJECT, 0u, 0u, 0u);
    }
    task_sleep(110u);
    uint32_t dropped_before = trace_dropped();
    while (trace_pop(&event) == 1) {
    }
    task_sleep(110u);
    uint32_t reports_drop = 0u;
    while (trace_pop(&event) == 1) {
        if (event.type == EV_HEARTBEAT &&
            event.arg1 >= dropped_before && event.arg1 > 0u) {
            reports_drop = 1u;
        }
    }

    /* Fallo inyectado solo en esta imagen de prueba: sin ISR no hay latidos. */
    uint32_t saved_ps = irq_disable();
    uint32_t before = trace_emitted();
    uint32_t stop_start = cpu_cycles();
    while ((uint32_t)(cpu_cycles() - stop_start) <
           (CPU_FREQ_HZ / 1000u) * STOP_MS) {
    }
    uint32_t stopped_ms = (uint32_t)(cpu_cycles() - stop_start) /
                          (CPU_FREQ_HZ / 1000u);
    uint32_t during_stop = trace_emitted() - before;
    uint32_t tick_before_resume = uptime_ms();
    irq_restore(saved_ps);

    /* Espera acotada: un timer que no se rearme no debe colgar el propio test. */
    uint32_t resume_start = cpu_cycles();
    while (uptime_ms() == tick_before_resume &&
           (uint32_t)(cpu_cycles() - resume_start) < CPU_FREQ_HZ / 10u) {
    }
    uint32_t resumed = (uptime_ms() != tick_before_resume);
    uint32_t observe_start = cpu_cycles();
    while ((uint32_t)(cpu_cycles() - observe_start) <
           (CPU_FREQ_HZ / 1000u) * 120u) {
    }

    uint32_t after_resume = 0u;
    while (trace_pop(&event) == 1) {
        if (event.type == EV_HEARTBEAT) {
            after_resume++;
        }
    }

    uint32_t idle_ran = (idle_after > idle_before);
    uint32_t detector_dead = (stopped_ms >= 300u && during_stop == 0u);
    uint32_t dropped = trace_dropped();
    kprintf("TRACE_HEARTBEAT: normales=%u filtro=%u idle=%u errores_intervalo=%u perdidos_forzados=%u reporte_perdidos=%u silencio_ms=%u durante_silencio=%u detector_dead=%u reanudo=%u despues=%u perdidos=%u\r\n",
            normal_count, filter_ok, idle_ran, interval_errors,
            dropped_before, reports_drop, stopped_ms,
            during_stop, detector_dead,
            resumed, after_resume, dropped);
    kprintf("TRACE_HEARTBEAT_DONE %s\r\n",
            (normal_count >= 3u && filter_ok != 0u && idle_ran != 0u &&
             interval_errors == 0u &&
             dropped_before > 0u && reports_drop != 0u &&
             detector_dead != 0u && resumed != 0u && after_resume > 0u &&
             dropped >= dropped_before) ? "PASS" : "FAIL");
}

/* No se crea shell: durante task_sleep solo quedan idle y el muestreador. */
int32_t __wrap_shell_start(void) {
    if (task_create("trace-heart", heartbeat_board_task, 0, 0u, 0u) < 0) {
        kprintf("TRACE_HEARTBEAT_DONE FAIL: no se pudo crear la tarea\r\n");
    }
    return 0;
}
