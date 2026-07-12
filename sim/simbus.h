#ifndef SIMBUS_H
#define SIMBUS_H

/*
 * Floppy bus semantics.
 *
 * IBM PC bus: two units ("a" and "b" via the twisted cable).
 * Shugart bus: three units ("0", "1", "2").
 *
 * The host tool picks the bus type at runtime (CMD_SET_BUS_TYPE);
 * drives are physically attached to unit positions regardless, and
 * the bus decides how many positions are addressable.
 */

/* Addressable units on a bus type, or 0 if the bus is unknown. */
extern int sim_bus_max_units(int bus);

/* Display name for a unit on a bus ('a'/'b' vs '0'..'2'). */
extern char sim_bus_unit_char(int bus, int unit);

extern const char *sim_bus_name(int bus);

#endif
