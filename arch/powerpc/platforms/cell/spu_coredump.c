/*
 * SPU core dump code
 *
 * (C) Copyright 2006 IBM Corp.
 *
 * Author: Dwayne Grant McConnell <decimal@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/file.h>
#include <linux/module.h>
#include <linux/syscalls.h>

#include <asm/spu.h>

static struct spu_coredump_calls spu_coredump_calls;
static DEFINE_MUTEX(spu_coredump_mutex);

int arch_notes_size(void)
{
	long ret;
	struct module *owner = spu_coredump_calls.owner;

	ret = -ENOSYS;
	mutex_lock(&spu_coredump_mutex);
	if (owner && try_module_get(owner)) {
		ret = spu_coredump_calls.arch_notes_size();
		module_put(owner);
	}
	mutex_unlock(&spu_coredump_mutex);
	return ret;
}

void arch_write_notes(struct file *file)
{
	struct module *owner = spu_coredump_calls.owner;

	mutex_lock(&spu_coredump_mutex);
	if (owner && try_module_get(owner)) {
		spu_coredump_calls.arch_write_notes(file);
		module_put(owner);
	}
	mutex_unlock(&spu_coredump_mutex);
}

int register_arch_coredump_calls(struct spu_coredump_calls *calls)
{
	if (spu_coredump_calls.owner)
		return -EBUSY;

	mutex_lock(&spu_coredump_mutex);
	spu_coredump_calls.arch_notes_size = calls->arch_notes_size;
	spu_coredump_calls.arch_write_notes = calls->arch_write_notes;
	spu_coredump_calls.owner = calls->owner;
	mutex_unlock(&spu_coredump_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(register_arch_coredump_calls);

void unregister_arch_coredump_calls(struct spu_coredump_calls *calls)
{
	BUG_ON(spu_coredump_calls.owner != calls->owner);

	mutex_lock(&spu_coredump_mutex);
	spu_coredump_calls.owner = NULL;
	mutex_unlock(&spu_coredump_mutex);
}
EXPORT_SYMBOL_GPL(unregister_arch_coredump_calls);
