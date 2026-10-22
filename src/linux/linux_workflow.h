/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - the image writing workflow.
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

#pragma once

#include <stdint.h>
#include <stdio.h>

/*
 * The options that drive an image writing operation. They are shared by the
 * command line and the graphical front-ends.
 */
typedef struct {
	const char* image;			/* ISO or disk image to write */
	const char* device;			/* target device, e.g. "sdb" */
	const char* extract_to;		/* extract the image to a directory instead */
	const char* label;			/* volume label */
	const char* fs;				/* target file system */
	const char* partition_style;	/* "gpt" or "mbr" */
	int use_bios;				/* install a BIOS bootloader */
	int use_uefi;				/* install a UEFI bootloader */
	int dry_run;				/* only report what would be done */
	int verbose;
	int list_all;
} linux_options;

/* Resolve a file system name ("fat32", "ntfs", ...) to its FS_* value. */
int linux_parse_fs(const char* name);

/* Locate a drive by name ("sdb" or "/dev/sdb"). Returns -1 if not found. */
int linux_find_drive_index(const char* name);

/* Write an image to a device. Returns TRUE on success. */
int linux_write_image(const linux_options* opts);

/* Extract an image to a directory. Returns TRUE on success. */
int linux_extract_image(const char* image, const char* directory);

/* Exercise the partition table code on a regular file. */
int linux_selftest(const char* path);
int linux_selftest_mbr(const char* path);

/* Populate the device list. */
void linux_refresh_devices(void);

/* Print the list of candidate devices to the given stream. */
void linux_list_devices(FILE* stream, int all);

/*
 * The GTK front-end. Returns non-zero if the graphical interface could be
 * started (or if it was not compiled in, in which case the caller should
 * fall back to the command line).
 */
int linux_ui_main(int argc, char** argv);
int linux_ui_available(void);
