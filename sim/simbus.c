#include "greaseweazle.h"

#include "simbus.h"


int
sim_bus_max_units(int bus)
{
	switch (bus) {
	case BUS_IBMPC:
		return 2;
	case BUS_SHUGART:
		return 3;
	default:
		return 0;
	}
}


char
sim_bus_unit_char(int bus, int unit)
{
	return (bus == BUS_SHUGART) ? '0' + unit : 'a' + unit;
}


const char *
sim_bus_name(int bus)
{
	switch (bus) {
	case BUS_IBMPC:
		return "IBM PC";
	case BUS_SHUGART:
		return "Shugart";
	case BUS_APPLE2:
		return "Apple II";
	default:
		return "none";
	}
}
