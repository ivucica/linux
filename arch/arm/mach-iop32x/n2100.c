/*
 * arch/arm/mach-iop32x/n2100.c
 *
 * Board support code for the Thecus N2100 platform.
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 * Copyright 2003 (c) MontaVista, Software, Inc.
 * Copyright (C) 2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/pci.h>
#include <asm/mach/time.h>
#include <asm/mach-types.h>
#include <asm/page.h>
#include <asm/pgtable.h>

/*
 * N2100 timer tick configuration.
 */
static void __init n2100_timer_init(void)
{
	/* 33.000 MHz crystal.  */
	iop3xx_init_time(198000000);
}

static struct sys_timer n2100_timer = {
	.init		= n2100_timer_init,
	.offset		= iop3xx_gettimeoffset,
};


/*
 * N2100 I/O.
 */
static struct map_desc n2100_io_desc[] __initdata = {
	{	/* on-board devices */
		.virtual	= N2100_UART,
		.pfn		= __phys_to_pfn(N2100_UART),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	},
};

void __init n2100_map_io(void)
{
	iop3xx_map_io();
	iotable_init(n2100_io_desc, ARRAY_SIZE(n2100_io_desc));
}


/*
 * N2100 PCI.
 */
static inline int __init
n2100_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq;

	if (PCI_SLOT(dev->devfn) == 1) {
		/* RTL8110SB #1 */
		irq = IRQ_IOP32X_XINT0;
	} else if (PCI_SLOT(dev->devfn) == 2) {
		/* RTL8110SB #2 */
		irq = IRQ_IOP32X_XINT3;
	} else if (PCI_SLOT(dev->devfn) == 3) {
		/* Sil3512 */
		irq = IRQ_IOP32X_XINT2;
	} else if (PCI_SLOT(dev->devfn) == 4 && pin == 1) {
		/* VT6212 INTA */
		irq = IRQ_IOP32X_XINT1;
	} else if (PCI_SLOT(dev->devfn) == 4 && pin == 2) {
		/* VT6212 INTB */
		irq = IRQ_IOP32X_XINT0;
	} else if (PCI_SLOT(dev->devfn) == 4 && pin == 3) {
		/* VT6212 INTC */
		irq = IRQ_IOP32X_XINT2;
	} else if (PCI_SLOT(dev->devfn) == 5) {
		/* Mini-PCI slot */
		irq = IRQ_IOP32X_XINT3;
	} else {
		printk(KERN_ERR "n2100_pci_map_irq() called for unknown "
			"device PCI:%d:%d:%d\n", dev->bus->number,
			PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		irq = -1;
	}

	return irq;
}

static struct hw_pci n2100_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.nr_controllers = 1,
	.setup		= iop3xx_pci_setup,
	.preinit	= iop3xx_pci_preinit,
	.scan		= iop3xx_pci_scan_bus,
	.map_irq	= n2100_pci_map_irq,
};

static int __init n2100_pci_init(void)
{
	if (machine_is_n2100())
		pci_common_init(&n2100_pci);

	return 0;
}

subsys_initcall(n2100_pci_init);


/*
 * N2100 machine initialisation.
 */
static struct physmap_flash_data n2100_flash_data = {
	.width		= 2,
};

static struct resource n2100_flash_resource = {
	.start		= 0xf0000000,
	.end		= 0xf0ffffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device n2100_flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &n2100_flash_data,
	},
	.num_resources	= 1,
	.resource	= &n2100_flash_resource,
};


static struct plat_serial8250_port n2100_serial_port[] = {
	{
		.mapbase	= N2100_UART,
		.membase	= (char *)N2100_UART,
		.irq		= 0,
		.flags		= UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= 1843200,
	},
	{ },
};

static struct resource n2100_uart_resource = {
	.start		= N2100_UART,
	.end		= N2100_UART + 7,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device n2100_serial_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
		.platform_data		= n2100_serial_port,
	},
	.num_resources	= 1,
	.resource	= &n2100_uart_resource,
};


/*
 * Pull PCA9532 GPIO #8 low to power off the machine.
 */
static void n2100_power_off(void)
{
	local_irq_disable();

	/* Start condition, I2C address of PCA9532, write transaction.  */
	*IOP3XX_IDBR0 = 0xc0;
	*IOP3XX_ICR0 = 0xe9;
	mdelay(1);

	/* Write address 0x08.  */
	*IOP3XX_IDBR0 = 0x08;
	*IOP3XX_ICR0 = 0xe8;
	mdelay(1);

	/* Write data 0x01, stop condition.  */
	*IOP3XX_IDBR0 = 0x01;
	*IOP3XX_ICR0 = 0xea;

	while (1)
		;
}


static struct timer_list power_button_poll_timer;

static void power_button_poll(unsigned long dummy)
{
	if (gpio_line_get(N2100_POWER_BUTTON) == 0) {
		ctrl_alt_del();
		return;
	}

	power_button_poll_timer.expires = jiffies + (HZ / 10);
	add_timer(&power_button_poll_timer);
}


static void __init n2100_init_machine(void)
{
	platform_device_register(&iop3xx_i2c0_device);
	platform_device_register(&n2100_flash_device);
	platform_device_register(&n2100_serial_device);

	pm_power_off = n2100_power_off;

	init_timer(&power_button_poll_timer);
	power_button_poll_timer.function = power_button_poll;
	power_button_poll_timer.expires = jiffies + (HZ / 10);
	add_timer(&power_button_poll_timer);
}

MACHINE_START(N2100, "Thecus N2100")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.phys_io	= N2100_UART,
	.io_pg_offst	= ((N2100_UART) >> 18) & 0xfffc,
	.boot_params	= 0xa0000100,
	.map_io		= n2100_map_io,
	.init_irq	= iop32x_init_irq,
	.timer		= &n2100_timer,
	.init_machine	= n2100_init_machine,
MACHINE_END
