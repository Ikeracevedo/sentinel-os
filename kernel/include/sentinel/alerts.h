#ifndef SENTINEL_ALERTS_H
#define SENTINEL_ALERTS_H

#include <stdint.h>

#define ALERT_CAPACITY 32u
#define ALERT_MESSAGE_SIZE 48u
#define ALERT_COOLDOWN_MS 10000u

typedef enum {
    ALERT_INFO = 0,
    ALERT_WARN = 1,
    ALERT_CRIT = 2
} alert_severity_t;

typedef enum {
    ALERT_DEADLOCK = 1,
    ALERT_FRAGMENTATION = 2,
    ALERT_STARVATION = 3,
    ALERT_INVERSION = 4,
    ALERT_JITTER = 5
} alert_type_t;

typedef struct {
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint32_t key;
    uint32_t data[3];
    uint8_t severity;
    uint8_t type;
    char message[ALERT_MESSAGE_SIZE];
} alert_t;

/* Devuelve 1 si guarda la alerta; 0 si la misma clave esta en antirrebote. */
int alerts_record(uint32_t timestamp_ms, alert_severity_t severity,
                  alert_type_t type, uint32_t key, const char *message,
                  uint32_t data0, uint32_t data1, uint32_t data2);
uint32_t alerts_count(void);
/* Posicion 0 = alerta mas reciente. */
int alerts_get(uint32_t position, alert_t *out);
/* Primera alerta con secuencia posterior a after; util para el transporte. */
int alerts_next(uint32_t after, alert_t *out);
void alerts_clear(void);
const char *alerts_severity_name(uint8_t severity);
const char *alerts_type_name(uint8_t type);

#endif
