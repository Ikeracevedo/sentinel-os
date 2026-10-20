#include <assert.h>
#include <sentinel/alerts.h>

static void test_cooldown(void) {
    alert_t alert;
    assert(alerts_record(100u, ALERT_WARN, ALERT_JITTER, 0u,
                         "Jitter", 12u, 1u, 23u) == 1);
    assert(alerts_record(10099u, ALERT_WARN, ALERT_JITTER, 0u,
                         "Jitter", 15u, 1u, 24u) == 0);
    assert(alerts_record(10100u, ALERT_WARN, ALERT_JITTER, 0u,
                         "Jitter", 15u, 1u, 24u) == 1);
    assert(alerts_count() == 2u);
    assert(alerts_get(0u, &alert) == 1);
    assert(alert.timestamp_ms == 10100u && alert.data[0] == 15u);
    assert(alerts_get(2u, &alert) == 0);
    alerts_clear();
    assert(alerts_count() == 0u);
}

static void test_ring_and_order(void) {
    alert_t alert;
    for (uint32_t i = 0u; i < ALERT_CAPACITY + 3u; i++) {
        assert(alerts_record(i * 10u, ALERT_CRIT, ALERT_DEADLOCK, i,
                             "Ciclo", i, 2u, 0u) == 1);
    }
    assert(alerts_count() == ALERT_CAPACITY);
    assert(alerts_get(0u, &alert) == 1 && alert.data[0] == 34u);
    assert(alerts_get(31u, &alert) == 1 && alert.data[0] == 3u);
    uint32_t previous = alert.sequence - 1u;
    assert(alerts_next(previous, &alert) == 1 && alert.data[0] == 3u);
    assert(alerts_next(alert.sequence, &alert) == 1 && alert.data[0] == 4u);
    assert(alerts_next(alert.sequence + 100u, &alert) == 0);
    alerts_clear();
}

static void test_key_and_message(void) {
    alert_t alert;
    assert(alerts_record(1u, ALERT_WARN, ALERT_STARVATION, 1u,
                         "Tarea uno", 1u, 501u, 6u) == 1);
    assert(alerts_record(2u, ALERT_WARN, ALERT_STARVATION, 2u,
                         "Tarea dos", 2u, 502u, 6u) == 1);
    assert(alerts_get(0u, &alert) == 1 && alert.key == 2u);
    assert(alert.message[0] == 'T' && alert.message[9] == '\0');
    assert(alerts_record(3u, ALERT_WARN, ALERT_JITTER, 2u,
                         "Otro tipo", 10u, 1u, 0u) == 1);
    alerts_clear();
}

int main(void) {
    test_cooldown();
    test_ring_and_order();
    test_key_and_message();
    return 0;
}
