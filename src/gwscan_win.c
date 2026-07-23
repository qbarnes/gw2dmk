/*
 * Greaseweazle USB scan backend for Microsoft Windows using SetupAPI.
 */

#include "gwscan_impl.h"

#if defined(WIN64) || defined(WIN32)

#include <windows.h>
#include <initguid.h>	/* must precede devguid.h to instantiate GUIDs */
#include <devguid.h>
#include <setupapi.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "greaseweazle.h"


/*
 * Case-insensitive substring search (strcasestr() is not in MinGW).
 */

static const char *
strcasestr_(const char *haystack, const char *needle)
{
	size_t	nlen = strlen(needle);

	for (const char *p = haystack; *p; ++p) {
		if (strncasecmp(p, needle, nlen) == 0)
			return p;
	}

	return NULL;
}


/*
 * Does any string in the REG_MULTI_SZ hardware ID list contain
 * the Greaseweazle VID/PID?
 */

static bool
hwids_match(const char *hwids, size_t hwids_len)
{
	char	vidpid[32];

	snprintf(vidpid, sizeof vidpid, "VID_%04X&PID_%04X",
		 GREASEWEAZLE_VID, GREASEWEAZLE_PID);

	const char	*p   = hwids;
	const char	*end = hwids + hwids_len;

	while (p < end && *p) {
		if (strcasestr_(p, vidpid))
			return true;
		p += strlen(p) + 1;
	}

	return false;
}


int
gw_scan_backend(struct gw_scan_dev **devs)
{
	HDEVINFO	di;

	/*
	 * Enumerate the Ports setup class restricted to the "USB"
	 * enumerator, which guarantees device instance IDs of the
	 * form "USB\VID_xxxx&PID_xxxx\<serial>" and excludes LPT
	 * and motherboard serial ports.
	 */
	di = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, "USB", NULL,
				  DIGCF_PRESENT);

	if (di == INVALID_HANDLE_VALUE) {
		errno = EIO;
		return GW_SCAN_ERROR;
	}

	int		cnt = 0;
	SP_DEVINFO_DATA	dd = { .cbSize = sizeof dd };

	for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); ++i) {
		char	hwids[512];
		DWORD	hwids_len = 0;

		if (!SetupDiGetDeviceRegistryPropertyA(di, &dd,
		    SPDRP_HARDWAREID, NULL, (PBYTE)hwids, sizeof hwids,
		    &hwids_len))
			continue;

		if (!hwids_match(hwids, hwids_len))
			continue;

		char	instid[256];

		if (!SetupDiGetDeviceInstanceIdA(di, &dd, instid,
		    sizeof instid, NULL))
			continue;

		/*
		 * The USB serial number is the instance ID's last
		 * component, unless the device lacks one, in which
		 * case Windows synthesizes an ID containing '&'.
		 */
		const char	*bs     = strrchr(instid, '\\');
		const char	*serial = bs ? bs + 1 : "";

		if (strchr(serial, '&'))
			serial = "";

		HKEY	key = SetupDiOpenDevRegKey(di, &dd, DICS_FLAG_GLOBAL,
						   0, DIREG_DEV, KEY_READ);

		if (key == INVALID_HANDLE_VALUE)
			continue;

		char	port[64];
		DWORD	port_len  = sizeof port - 1;
		DWORD	type      = 0;
		LSTATUS	rqret;

		rqret = RegQueryValueExA(key, "PortName", NULL, &type,
					 (LPBYTE)port, &port_len);

		RegCloseKey(key);

		if (rqret != ERROR_SUCCESS || type != REG_SZ)
			continue;

		port[port_len] = '\0';

		if (strncmp(port, "COM", 3) != 0 || !isdigit(port[3]))
			continue;

		char	devname[80];

		snprintf(devname, sizeof devname, "\\\\.\\%s", port);

		cnt = gw_scan_append(devs, cnt, devname, serial);

		if (cnt == GW_SCAN_ERROR)
			break;
	}

	SetupDiDestroyDeviceInfoList(di);

	return cnt;
}

#endif
