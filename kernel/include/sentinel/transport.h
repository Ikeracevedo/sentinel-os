#ifndef SENTINEL_TRANSPORT_H
#define SENTINEL_TRANSPORT_H

#include <stdint.h>

/* Activar tras timer_init() y sched_init(), antes de iniciar el shell. */
int transport_start(void);
uint32_t transport_frames_dropped(void);
uint32_t transport_events_dropped(void);

#endif
