#ifndef SIMGW_H
#define SIMGW_H

#include <stdbool.h>
#include <stdint.h>

#include "greaseweazle.h"

#include "simdrive.h"

#define SIM_MAX_UNITS	3

/*
 * Greaseweazle device models.
 *
 * The host tools only use sample_freq numerically; the other fields
 * are reported via CMD_GET_INFO for identification.  Adding a model
 * is one table entry in simgw.c.
 */

struct sim_gw_model {
	const char	*name;
	uint8_t		fw_major;
	uint8_t		fw_minor;
	uint8_t		hw_model;
	uint8_t		hw_submodel;
	uint8_t		usb_speed;	/* 1 = full, 2 = high */
	uint8_t		max_cmd;
	uint32_t	sample_freq;
};

/* Emulated Greaseweazle state. */
struct sim_gw {
	const struct sim_gw_model	*model;

	int		bus;		/* BUS_NONE until host sets it */
	int		sel_unit;	/* -1 = none selected */
	int		head;
	int		densel;		/* density select (pin 2) level */
	uint8_t		flux_status;	/* for CMD_GET_FLUX_STATUS */
	struct gw_delay	delays;

	int		fw_cyl[SIM_MAX_UNITS];	/* firmware's notion */
	struct sim_drive *unit[SIM_MAX_UNITS];
};

extern const struct sim_gw_model *sim_gw_model_find(const char *name);

extern void sim_gw_model_list(void);

/* Reset device state to power-on values (drives stay attached). */
extern void sim_gw_reset(struct sim_gw *gw);

/* Currently selected drive, or NULL. */
extern struct sim_drive *sim_gw_sel_drive(struct sim_gw *gw);

#endif
