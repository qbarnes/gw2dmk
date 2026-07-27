/*
 * Greaseweazle USB scan backend for macOS using IOKit.
 */

#include "gwscan_impl.h"

#if defined(__APPLE__)

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>

#include "greaseweazle.h"


/*
 * The USB device node owns idVendor/idProduct/serial, but the service
 * we match on is the IOSerialBSDClient several levels below it in the
 * IOService plane.  Searching parents recursively avoids assuming a
 * fixed depth, which varies with the USB controller in use.
 *
 * These property names are spelled as literals rather than using the
 * kUSB* macros so this file needs only IOKitLib.h and IOSerialKeys.h;
 * the macros live in headers whose contents move between SDKs.
 */

#define GW_PROP_VID	CFSTR("idVendor")
#define GW_PROP_PID	CFSTR("idProduct")
#define GW_PROP_SERIAL	CFSTR("USB Serial Number")


/*
 * Read a numeric property from the service's ancestry into "val".
 * Returns 0 on success, -1 if absent or not a number.
 */

static int
usb_prop_num(io_object_t svc, CFStringRef key, long *val)
{
	CFTypeRef	ref = IORegistryEntrySearchCFProperty(svc,
				kIOServicePlane, key, NULL,
				kIORegistryIterateRecursively |
					kIORegistryIterateParents);

	if (!ref)
		return -1;

	int	ret = -1;

	if (CFGetTypeID(ref) == CFNumberGetTypeID() &&
	    CFNumberGetValue(ref, kCFNumberLongType, val))
		ret = 0;

	CFRelease(ref);

	return ret;
}


/*
 * Read a string property into "buf".  If "parents" is true the
 * service's ancestry is searched, otherwise only the service itself.
 * Returns 0 on success, -1 if absent, not a string, or too long.
 */

static int
prop_str(io_object_t svc, CFStringRef key, bool parents,
	 char *buf, size_t buflen)
{
	CFTypeRef	ref = parents ?
		IORegistryEntrySearchCFProperty(svc, kIOServicePlane, key,
			NULL, kIORegistryIterateRecursively |
				kIORegistryIterateParents) :
		IORegistryEntryCreateCFProperty(svc, key, NULL, 0);

	if (!ref)
		return -1;

	int	ret = -1;

	if (CFGetTypeID(ref) == CFStringGetTypeID() &&
	    CFStringGetCString(ref, buf, buflen, kCFStringEncodingUTF8))
		ret = 0;

	CFRelease(ref);

	return ret;
}


int
gw_scan_backend(struct gw_scan_dev **devs)
{
	CFMutableDictionaryRef	match =
		IOServiceMatching(kIOSerialBSDServiceValue);

	if (!match) {
		errno = ENOMEM;
		return GW_SCAN_ERROR;
	}

	CFDictionarySetValue(match, CFSTR(kIOSerialBSDTypeKey),
			     CFSTR(kIOSerialBSDAllTypes));

	io_iterator_t	iter;

	/*
	 * MACH_PORT_NULL selects the default port.  Spelling it this
	 * way sidesteps both kIOMasterPortDefault (deprecated in 12.0,
	 * and -Wdeprecated-declarations is fatal here) and
	 * kIOMainPortDefault (12.0 and newer only, which would raise
	 * our deployment target).
	 *
	 * This consumes "match" whether it succeeds or fails.
	 */

	if (IOServiceGetMatchingServices(MACH_PORT_NULL, match, &iter) !=
	    KERN_SUCCESS) {
		errno = EIO;
		return GW_SCAN_ERROR;
	}

	int	cnt = 0;

	for (io_object_t svc; (svc = IOIteratorNext(iter));
	     IOObjectRelease(svc)) {
		long	vid, pid;

		if (usb_prop_num(svc, GW_PROP_VID, &vid) ||
		    vid != GREASEWEAZLE_VID)
			continue;

		if (usb_prop_num(svc, GW_PROP_PID, &pid) ||
		    pid != GREASEWEAZLE_PID)
			continue;

		char	devname[256];

		/*
		 * Take the callout node (/dev/cu.*) and never the
		 * dial-in node (/dev/tty.*): opening the latter blocks
		 * waiting for carrier detect.
		 */

		if (prop_str(svc, CFSTR(kIOCalloutDeviceKey), false,
			     devname, sizeof devname))
			continue;

		char	serial[256];

		if (prop_str(svc, GW_PROP_SERIAL, true,
			     serial, sizeof serial))
			serial[0] = '\0';

		cnt = gw_scan_append(devs, cnt, devname, serial);

		if (cnt == GW_SCAN_ERROR) {
			IOObjectRelease(svc);
			break;
		}
	}

	IOObjectRelease(iter);

	return cnt;
}

#endif
