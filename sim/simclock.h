#ifndef SIMCLOCK_H
#define SIMCLOCK_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Simulator timing helpers.
 *
 * All simulated latencies (seek, motor spin-up, rotational delays)
 * funnel through sim_sleep_ns() so that "fast" mode can skip them
 * while keeping all tick arithmetic identical.
 */

extern bool sim_fast;

extern uint64_t sim_now_ns(void);

extern void sim_sleep_ns(uint64_t ns);

extern void sim_sleep_us(uint64_t us);

extern void sim_sleep_ms(uint64_t ms);

extern void sim_sleep_ticks(uint64_t ticks, uint32_t freq);

#endif
