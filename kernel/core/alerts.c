#include <sentinel/alerts.h>
#ifndef ALERTS_HOST
#include "../arch/xtensa/cpu.h"
#else
static uint32_t irq_save(void) { return 0u; }
static void irq_restore(uint32_t state) { (void)state; }
#endif

static alert_t g_alerts[ALERT_CAPACITY];
static uint32_t g_head;
static uint32_t g_count;
static uint32_t g_sequence;

int alerts_record(uint32_t timestamp_ms, alert_severity_t severity,
                  alert_type_t type, uint32_t key, const char *message,
                  uint32_t data0, uint32_t data1, uint32_t data2) {
    if (message == 0) {
        return 0;
    }
    uint32_t state = irq_save();
    for (uint32_t i = 0u; i < g_count; i++) {
        uint32_t index = (g_head + ALERT_CAPACITY - 1u - i) % ALERT_CAPACITY;
        const alert_t *old = &g_alerts[index];
        if (old->type == (uint8_t)type && old->key == key &&
            timestamp_ms - old->timestamp_ms < ALERT_COOLDOWN_MS) {
            irq_restore(state);
            return 0;
        }
    }
    alert_t *entry = &g_alerts[g_head];
    entry->sequence = ++g_sequence;
    entry->timestamp_ms = timestamp_ms;
    entry->key = key;
    entry->severity = (uint8_t)severity;
    entry->type = (uint8_t)type;
    entry->data[0] = data0;
    entry->data[1] = data1;
    entry->data[2] = data2;
    uint32_t n = 0u;
    while (n + 1u < ALERT_MESSAGE_SIZE && message[n] != '\0') {
        entry->message[n] = message[n];
        n++;
    }
    entry->message[n] = '\0';
    g_head = (g_head + 1u) % ALERT_CAPACITY;
    if (g_count < ALERT_CAPACITY) {
        g_count++;
    }
    irq_restore(state);
    return 1;
}

uint32_t alerts_count(void) {
    uint32_t state = irq_save();
    uint32_t count = g_count;
    irq_restore(state);
    return count;
}

int alerts_get(uint32_t position, alert_t *out) {
    if (out == 0) {
        return 0;
    }
    uint32_t state = irq_save();
    if (position >= g_count) {
        irq_restore(state);
        return 0;
    }
    uint32_t index = (g_head + ALERT_CAPACITY - 1u - position) % ALERT_CAPACITY;
    *out = g_alerts[index];
    irq_restore(state);
    return 1;
}

int alerts_next(uint32_t after, alert_t *out) {
    if (out == 0) {
        return 0;
    }
    uint32_t state = irq_save();
    for (uint32_t i = g_count; i > 0u; i--) {
        uint32_t index = (g_head + ALERT_CAPACITY - i) % ALERT_CAPACITY;
        if ((int32_t)(g_alerts[index].sequence - after) > 0) {
            *out = g_alerts[index];
            irq_restore(state);
            return 1;
        }
    }
    irq_restore(state);
    return 0;
}

void alerts_clear(void) {
    uint32_t state = irq_save();
    g_head = 0u;
    g_count = 0u;
    irq_restore(state);
}

const char *alerts_severity_name(uint8_t severity) {
    switch (severity) {
        case ALERT_INFO: return "INFO";
        case ALERT_WARN: return "WARN";
        case ALERT_CRIT: return "CRIT";
        default: return "?";
    }
}

const char *alerts_type_name(uint8_t type) {
    switch (type) {
        case ALERT_DEADLOCK: return "DEADLOCK";
        case ALERT_FRAGMENTATION: return "FRAGMENTACION";
        case ALERT_STARVATION: return "INANICION";
        case ALERT_INVERSION: return "INVERSION";
        case ALERT_JITTER: return "JITTER";
        default: return "?";
    }
}
