/*
 * drivers/usb/generic.c - generic driver for USB devices (not interfaces)
 *
 * (C) Copyright 2005 Greg Kroah-Hartman <gregkh@suse.de>
 *
 * based on drivers/usb/usb.c which had the following copyrights:
 *	(C) Copyright Linus Torvalds 1999
 *	(C) Copyright Johannes Erdfelt 1999-2001
 *	(C) Copyright Andreas Gal 1999
 *	(C) Copyright Gregory P. Smith 1999
 *	(C) Copyright Deti Fliegl 1999 (new USB architecture)
 *	(C) Copyright Randy Dunlap 2000
 *	(C) Copyright David Brownell 2000-2004
 *	(C) Copyright Yggdrasil Computing, Inc. 2000
 *		(usb_device_id matching changes by Adam J. Richter)
 *	(C) Copyright Greg Kroah-Hartman 2002-2003
 *
 */

#include <linux/config.h>
#include <linux/usb.h>
#include "usb.h"

static inline const char *plural(int n)
{
	return (n == 1 ? "" : "s");
}

static int choose_configuration(struct usb_device *udev)
{
	int i;
	int num_configs;
	int insufficient_power = 0;
	struct usb_host_config *c, *best;

	best = NULL;
	c = udev->config;
	num_configs = udev->descriptor.bNumConfigurations;
	for (i = 0; i < num_configs; (i++, c++)) {
		struct usb_interface_descriptor	*desc = NULL;

		/* It's possible that a config has no interfaces! */
		if (c->desc.bNumInterfaces > 0)
			desc = &c->intf_cache[0]->altsetting->desc;

		/*
		 * HP's USB bus-powered keyboard has only one configuration
		 * and it claims to be self-powered; other devices may have
		 * similar errors in their descriptors.  If the next test
		 * were allowed to execute, such configurations would always
		 * be rejected and the devices would not work as expected.
		 * In the meantime, we run the risk of selecting a config
		 * that requires external power at a time when that power
		 * isn't available.  It seems to be the lesser of two evils.
		 *
		 * Bugzilla #6448 reports a device that appears to crash
		 * when it receives a GET_DEVICE_STATUS request!  We don't
		 * have any other way to tell whether a device is self-powered,
		 * but since we don't use that information anywhere but here,
		 * the call has been removed.
		 *
		 * Maybe the GET_DEVICE_STATUS call and the test below can
		 * be reinstated when device firmwares become more reliable.
		 * Don't hold your breath.
		 */
#if 0
		/* Rule out self-powered configs for a bus-powered device */
		if (bus_powered && (c->desc.bmAttributes &
					USB_CONFIG_ATT_SELFPOWER))
			continue;
#endif

		/*
		 * The next test may not be as effective as it should be.
		 * Some hubs have errors in their descriptor, claiming
		 * to be self-powered when they are really bus-powered.
		 * We will overestimate the amount of current such hubs
		 * make available for each port.
		 *
		 * This is a fairly benign sort of failure.  It won't
		 * cause us to reject configurations that we should have
		 * accepted.
		 */

		/* Rule out configs that draw too much bus current */
		if (c->desc.bMaxPower * 2 > udev->bus_mA) {
			insufficient_power++;
			continue;
		}

		/* If the first config's first interface is COMM/2/0xff
		 * (MSFT RNDIS), rule it out unless Linux has host-side
		 * RNDIS support. */
		if (i == 0 && desc
				&& desc->bInterfaceClass == USB_CLASS_COMM
				&& desc->bInterfaceSubClass == 2
				&& desc->bInterfaceProtocol == 0xff) {
#ifndef CONFIG_USB_NET_RNDIS_HOST
			continue;
#else
			best = c;
#endif
		}

		/* From the remaining configs, choose the first one whose
		 * first interface is for a non-vendor-specific class.
		 * Reason: Linux is more likely to have a class driver
		 * than a vendor-specific driver. */
		else if (udev->descriptor.bDeviceClass !=
						USB_CLASS_VENDOR_SPEC &&
				(!desc || desc->bInterfaceClass !=
						USB_CLASS_VENDOR_SPEC)) {
			best = c;
			break;
		}

		/* If all the remaining configs are vendor-specific,
		 * choose the first one. */
		else if (!best)
			best = c;
	}

	if (insufficient_power > 0)
		dev_info(&udev->dev, "rejected %d configuration%s "
			"due to insufficient available bus power\n",
			insufficient_power, plural(insufficient_power));

	if (best) {
		i = best->desc.bConfigurationValue;
		dev_info(&udev->dev,
			"configuration #%d chosen from %d choice%s\n",
			i, num_configs, plural(num_configs));
	} else {
		i = -1;
		dev_warn(&udev->dev,
			"no configuration chosen from %d choice%s\n",
			num_configs, plural(num_configs));
	}
	return i;
}

static int generic_probe(struct usb_device *udev)
{
	int err, c;

	/* put device-specific files into sysfs */
	usb_create_sysfs_dev_files(udev);

	/* Choose and set the configuration.  This registers the interfaces
	 * with the driver core and lets interface drivers bind to them.
	 */
	c = choose_configuration(udev);
	if (c >= 0) {
		err = usb_set_configuration(udev, c);
		if (err) {
			dev_err(&udev->dev, "can't set config #%d, error %d\n",
					c, err);
			/* This need not be fatal.  The user can try to
			 * set other configurations. */
		}
	}

	/* USB device state == configured ... usable */
	usb_notify_add_device(udev);

	return 0;
}

static void generic_disconnect(struct usb_device *udev)
{
	usb_notify_remove_device(udev);

	/* if this is only an unbind, not a physical disconnect, then
	 * unconfigure the device */
	if (udev->actconfig)
		usb_set_configuration(udev, 0);

	usb_remove_sysfs_dev_files(udev);
}

#ifdef	CONFIG_PM

static int generic_suspend(struct usb_device *udev, pm_message_t msg)
{
	/* USB devices enter SUSPEND state through their hubs, but can be
	 * marked for FREEZE as soon as their children are already idled.
	 * But those semantics are useless, so we equate the two (sigh).
	 */
	return usb_port_suspend(udev);
}

static int generic_resume(struct usb_device *udev)
{
	return usb_port_resume(udev);
}

#endif	/* CONFIG_PM */

struct usb_device_driver usb_generic_driver = {
	.name =	"usb",
	.probe = generic_probe,
	.disconnect = generic_disconnect,
#ifdef	CONFIG_PM
	.suspend = generic_suspend,
	.resume = generic_resume,
#endif
	.supports_autosuspend = 1,
};