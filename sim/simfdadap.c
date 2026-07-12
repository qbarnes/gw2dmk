#include <stdio.h>

#include "simfdadap.h"


bool
sim_fdadap_attach_ok(const struct sim_drive *drv, char *errbuf,
		     int errbuflen)
{
	if (drv->type->needs_fdadap && !drv->fdadap) {
		snprintf(errbuf, errbuflen,
			 "%s drives require an FDADAP adapter",
			 drv->type->name);
		return false;
	}

	if (!drv->type->needs_fdadap && drv->fdadap) {
		snprintf(errbuf, errbuflen,
			 "FDADAP adapter only accepts 8\" drives");
		return false;
	}

	return true;
}


bool
sim_fdadap_tg43(const struct sim_drive *drv)
{
	return drv->cyl > 43;
}
