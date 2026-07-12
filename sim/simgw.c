#include <stdio.h>
#include <string.h>

#include "simgw.h"


static const struct sim_gw_model gw_models[] = {
	/* name    fw     hw model  usb  max_cmd    sample_freq */
	{ "f7",	   1, 5,  7, 0,	    2,	 CMD_RESET, 72000000 },
	{ "v4.1",  1, 5,  4, 1,	    1,	 CMD_RESET, 72000000 },
};

#define N_GW_MODELS	(sizeof(gw_models) / sizeof(gw_models[0]))


const struct sim_gw_model *
sim_gw_model_find(const char *name)
{
	for (size_t i = 0; i < N_GW_MODELS; ++i) {
		if (!strcmp(name, gw_models[i].name))
			return &gw_models[i];
	}

	return NULL;
}


void
sim_gw_model_list(void)
{
	for (size_t i = 0; i < N_GW_MODELS; ++i)
		printf("  %s\n", gw_models[i].name);
}


void
sim_gw_reset(struct sim_gw *gw)
{
	gw->bus		= BUS_NONE;
	gw->sel_unit	= -1;
	gw->head	= 0;
	gw->densel	= 0;
	gw->flux_status	= ACK_OKAY;

	/* Defaults per the Greaseweazle firmware. */
	gw->delays = (struct gw_delay){
		.select_delay	= 10,		/* us */
		.step_delay	= 3000,		/* us */
		.seek_settle	= 15,		/* ms */
		.motor_delay	= 750,		/* ms */
		.auto_off	= 10000		/* ms */
	};

	for (int u = 0; u < SIM_MAX_UNITS; ++u) {
		gw->fw_cyl[u] = -1;		/* unknown until homed */

		if (gw->unit[u])
			sim_drive_motor(gw->unit[u], false);
	}
}


struct sim_drive *
sim_gw_sel_drive(struct sim_gw *gw)
{
	if (gw->sel_unit < 0 || gw->sel_unit >= SIM_MAX_UNITS)
		return NULL;

	return gw->unit[gw->sel_unit];
}
