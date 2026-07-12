#include <time.h>

#include "simclock.h"


bool	sim_fast = false;


uint64_t
sim_now_ns(void)
{
	struct timespec	ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}


void
sim_sleep_ns(uint64_t ns)
{
	if (sim_fast || ns == 0)
		return;

	struct timespec	ts = {
		.tv_sec  = ns / 1000000000ull,
		.tv_nsec = ns % 1000000000ull
	};

	while (nanosleep(&ts, &ts) == -1)
		continue;
}


void
sim_sleep_us(uint64_t us)
{
	sim_sleep_ns(us * 1000ull);
}


void
sim_sleep_ms(uint64_t ms)
{
	sim_sleep_ns(ms * 1000000ull);
}


void
sim_sleep_ticks(uint64_t ticks, uint32_t freq)
{
	sim_sleep_ns((uint64_t)(ticks * (1000000000.0 / freq)));
}
