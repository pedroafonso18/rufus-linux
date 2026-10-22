/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - device enumeration.
 * Copyright © 2011-2026 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Device enumeration on Linux is based on sysfs:
 *
 *   /sys/class/block/<name>            -> the block device itself
 *   /sys/class/block/<name>/size        -> size in 512 byte sectors
 *   /sys/class/block/<name>/removable   -> "0" or "1"
 *   /sys/class/block/<name>/device/...  -> the parent device (USB, SCSI, ...)
 *
 * The USB information (VID, PID, speed) is obtained by walking up the sysfs
 * device tree until a USB device node is found.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "linux_api.h"
#include "posix/win_compat.h"

RUFUS_DRIVE rufus_drive[MAX_DRIVES];
BOOL enable_HDDs = FALSE;
BOOL enable_VHDs = FALSE;
BOOL use_fake_units = FALSE;
BOOL list_non_usb_removable_drives = FALSE;

/* Read a sysfs attribute into a buffer. Returns TRUE on success. */
static BOOL read_sysfs_attribute(const char* path, char* buffer, size_t size)
{
	int fd;
	ssize_t n;

	if (size == 0)
		return FALSE;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return FALSE;
	n = read(fd, buffer, size - 1);
	close(fd);
	if (n < 0)
		return FALSE;
	buffer[n] = 0;
	/* Strip the trailing newline that sysfs attributes end with */
	while (n > 0 && (buffer[n - 1] == '\n' || buffer[n - 1] == '\r'))
		buffer[--n] = 0;
	return TRUE;
}

/* Public wrapper, used by the other Linux translation units */
BOOL read_sysfs_value(const char* path, char* buffer, size_t size)
{
	return read_sysfs_attribute(path, buffer, size);
}

static BOOL read_sysfs_int(const char* path, long long* value)
{
	char buffer[64];
	if (!read_sysfs_attribute(path, buffer, sizeof(buffer)))
		return FALSE;
	*value = strtoll(buffer, NULL, 10);
	return TRUE;
}

/*
 * Walk up the sysfs device tree, looking for a USB device. When one is found,
 * its vendor ID, product ID and speed are returned.
 */
static BOOL find_usb_device(const char* sysfs_path, usb_device_props* props)
{
	char path[PATH_MAX];
	char buffer[128];
	int depth;

	props->is_USB = FALSE;
	props->vid = 0;
	props->pid = 0;
	props->current_speed = USB_SPEED_UNKNOWN;
	props->max_speed = USB_SPEED_UNKNOWN;

	/* The USB device tree lives under /sys/bus/usb/devices */
	snprintf(path, sizeof(path), "%s", sysfs_path);
	for (depth = 0; depth < 12; depth++) {
		char* slash;
		snprintf(buffer, sizeof(buffer), "%s/idVendor", path);
		if (read_sysfs_attribute(buffer, buffer, sizeof(buffer))) {
			props->is_USB = TRUE;
			props->vid = (int32_t)strtol(buffer, NULL, 16);
			snprintf(buffer, sizeof(buffer), "%s/idProduct", path);
			if (!read_sysfs_attribute(buffer, buffer, sizeof(buffer)))
				buffer[0] = 0;
			props->pid = (int32_t)strtol(buffer, NULL, 16);
			/* The 'speed' attribute is expressed in Mbit/s */
			snprintf(buffer, sizeof(buffer), "%s/speed", path);
			if (read_sysfs_attribute(buffer, buffer, sizeof(buffer))) {
				long long speed = strtoll(buffer, NULL, 10);
				switch (speed) {
				case 1: case 2: props->current_speed = USB_SPEED_LOW_1_5M; break;
				case 12: props->current_speed = USB_SPEED_FULL_12M; break;
				case 480: props->current_speed = USB_SPEED_HIGH_480M; break;
				case 5000: props->current_speed = USB_SPEED_SUPER_5G; break;
				case 10000: props->current_speed = USB_SPEED_SUPER_10G; break;
				case 20000: props->current_speed = USB_SPEED_SUPER_20G; break;
				case 40000: props->current_speed = USB_SPEED_SUPER_40G; break;
				case 80000: props->current_speed = USB_SPEED_SUPER_80G; break;
				default: props->current_speed = USB_SPEED_UNKNOWN; break;
				}
				props->max_speed = props->current_speed;
			}
			/* The maximum speed is exposed by the parent hub */
			return TRUE;
		}
		/* Move up one level */
		slash = strrchr(path, '/');
		if (slash == NULL || slash == path)
			break;
		*slash = 0;
	}
	return FALSE;
}

/* Retrieve the device description of a block device */
static void get_device_strings(const char* sysfs_path, char* vendor, size_t vendor_size,
	char* model, size_t model_size, char* serial, size_t serial_size)
{
	char path[PATH_MAX];

	vendor[0] = 0;
	model[0] = 0;
	serial[0] = 0;

	snprintf(path, sizeof(path), "%s/device/vendor", sysfs_path);
	read_sysfs_attribute(path, vendor, vendor_size);
	snprintf(path, sizeof(path), "%s/device/model", sysfs_path);
	read_sysfs_attribute(path, model, model_size);
	snprintf(path, sizeof(path), "%s/device/serial", sysfs_path);
	read_sysfs_attribute(path, serial, serial_size);

	/* NVMe and virtio devices expose their model differently */
	if (model[0] == 0) {
		snprintf(path, sizeof(path), "%s/device/../device/model", sysfs_path);
		read_sysfs_attribute(path, model, model_size);
	}
	if (model[0] == 0) {
		snprintf(path, sizeof(path), "%s/device/../model", sysfs_path);
		read_sysfs_attribute(path, model, model_size);
	}
	if (vendor[0] == 0) {
		snprintf(path, sizeof(path), "%s/device/../vendor", sysfs_path);
		read_sysfs_attribute(path, vendor, vendor_size);
	}
	/* Trim leading/trailing spaces */
	{
		char* s;
		for (s = model; *s == ' '; s++)
			;
		if (s != model)
			memmove(model, s, strlen(s) + 1);
		for (s = model + strlen(model); s > model && s[-1] == ' '; s--)
			s[-1] = 0;
		for (s = vendor; *s == ' '; s++)
			;
		if (s != vendor)
			memmove(vendor, s, strlen(s) + 1);
		for (s = vendor + strlen(vendor); s > vendor && s[-1] == ' '; s--)
			s[-1] = 0;
	}
}

/*
 * Determine whether a block device is removable.
 */
BOOL IsRemovableDevice(const char* device_path)
{
	char sysfs_path[PATH_MAX];
	char buffer[16];
	const char* name;

	if (device_path == NULL)
		return FALSE;
	name = strrchr(device_path, '/');
	name = (name != NULL) ? name + 1 : device_path;

	snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/block/%s/removable", name);
	if (read_sysfs_attribute(sysfs_path, buffer, sizeof(buffer)))
		return (strtoll(buffer, NULL, 10) != 0);
	/* Some devices (e.g. USB card readers) only expose the "removable"
	 * attribute on the SCSI device. Walk up the tree to look for it. */
	snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/block/%s", name);
	for (int i = 0; i < 6; i++) {
		char* slash;
		snprintf(sysfs_path + strlen(sysfs_path), sizeof(sysfs_path) - strlen(sysfs_path),
			"/../removable");
		snprintf(buffer, sizeof(buffer), "%s/removable", sysfs_path);
		if (read_sysfs_attribute(buffer, buffer, sizeof(buffer))) {
			if (strtoll(buffer, NULL, 10) != 0)
				return TRUE;
		}
		slash = strrchr(sysfs_path, '/');
		if (slash == NULL)
			break;
		*slash = 0;
	}
	return FALSE;
}

/*
 * Enumerate the block devices that are candidates for a Rufus operation.
 */
BOOL GetDevices(DWORD devnum)
{
	DIR* dir;
	struct dirent* entry;
	DWORD index = 0;
	BOOL found = FALSE;

	(void)devnum;
	ClearDrives();

	dir = opendir("/sys/class/block");
	if (dir == NULL) {
		uprintf("Could not open /sys/class/block: %s", strerror(errno));
		return FALSE;
	}

	while ((entry = readdir(dir)) != NULL) {
		char sysfs_path[PATH_MAX];
		char device_path[PATH_MAX];
		char vendor[64], model[128], serial[64];
		char buffer[64];
		long long sectors = 0;
		long long removable = 0;
		usb_device_props props;
		RUFUS_DRIVE* drive;
		BOOL is_usb;

		/* Skip the "." and ".." entries, as well as partitions and devices
		 * that are not backed by a real disk. */
		if (entry->d_name[0] == '.')
			continue;
		if (strchr(entry->d_name, ':') != NULL)	/* device mapper */
			continue;
		if (strncmp(entry->d_name, "loop", 4) == 0)	/* loopback device */
			continue;
		if (strncmp(entry->d_name, "ram", 3) == 0)	/* ramdisk */
			continue;
		if (strncmp(entry->d_name, "zram", 4) == 0)
			continue;
		if (strncmp(entry->d_name, "sr", 2) == 0)	/* CD-ROM */
			continue;
		if (strncmp(entry->d_name, "fd", 2) == 0)	/* floppy */
			continue;

		snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/block/%s", entry->d_name);

		/* Only consider devices that have a 'size' attribute and are not
		 * partitions themselves. */
		snprintf(buffer, sizeof(buffer), "/sys/class/block/%s/partition", entry->d_name);
		if (access(buffer, F_OK) == 0)
			continue;
		snprintf(buffer, sizeof(buffer), "/sys/class/block/%s/size", entry->d_name);
		if (!read_sysfs_int(buffer, &sectors))
			continue;
		if (sectors <= 0)
			continue;

		snprintf(device_path, sizeof(device_path), "/dev/%s", entry->d_name);
		if (access(device_path, F_OK) != 0)
			continue;

		memset(&props, 0, sizeof(props));
		is_usb = find_usb_device(sysfs_path, &props);
		snprintf(buffer, sizeof(buffer), "/sys/class/block/%s/removable", entry->d_name);
		read_sysfs_int(buffer, &removable);
		props.is_Removable = (BOOLEAN)(removable != 0 || IsRemovableDevice(device_path));

		get_device_strings(sysfs_path, vendor, sizeof(vendor), model, sizeof(model),
			serial, sizeof(serial));

		/* Filter out the devices that the user is not interested in */
		if (!props.is_Removable && !is_usb) {
			if (!enable_HDDs)
				continue;
		}
		if (!props.is_Removable && is_usb && !enable_HDDs) {
			/* USB attached disks that are not flagged as removable are
			 * usually external HDDs: only list them on request. */
			continue;
		}

		if (index >= MAX_DRIVES)
			break;

		drive = &rufus_drive[index];
		memset(drive, 0, sizeof(RUFUS_DRIVE));
		drive->index = index;
		drive->id = strdup(entry->d_name);
		drive->size = (uint64_t)sectors * 512;
		drive->port = index;
		drive->name = strdup(model[0] ? model : entry->d_name);
		if (vendor[0] && model[0])
			drive->display_name = NULL;
		else
			drive->display_name = NULL;
		drive->hub = strdup(is_usb ? "USB" : "");
		drive->label = NULL;
		found = TRUE;
		index++;
	}
	closedir(dir);
	return found;
}

void ClearDrives(void)
{
	DWORD i;
	for (i = 0; i < MAX_DRIVES; i++) {
		safe_free(rufus_drive[i].id);
		safe_free(rufus_drive[i].name);
		safe_free(rufus_drive[i].display_name);
		safe_free(rufus_drive[i].label);
		safe_free(rufus_drive[i].hub);
		rufus_drive[i].id = NULL;
		rufus_drive[i].name = NULL;
		rufus_drive[i].display_name = NULL;
		rufus_drive[i].label = NULL;
		rufus_drive[i].hub = NULL;
	}
}

BOOL GetUSBProperties(const char* device_path, usb_device_props* props)
{
	char sysfs_path[PATH_MAX];
	const char* name;

	if (device_path == NULL || props == NULL)
		return FALSE;
	name = strrchr(device_path, '/');
	name = (name != NULL) ? name + 1 : device_path;
	snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/block/%s", name);
	return find_usb_device(sysfs_path, props);
}
