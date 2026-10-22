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

/*
 * This file contains the workflow that is shared by the command line and the
 * graphical front-ends: create the partition table, create and format the
 * partitions, extract the image and install the bootloaders.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "linux_api.h"
#include "linux_headers.h"
#include "linux_workflow.h"
#include "global.h"
#include "ms-sys/inc/br.h"
#include "ms-sys/inc/file.h"
#include "msapi_utf8.h"
#include "posix/win_compat.h"

/* ------------------------------------------------------------------------ */
/* Progress                                                                  */
/* ------------------------------------------------------------------------ */

static void linux_progress(int percent, const char* text)
{
	if (linux_progress_callback != NULL)
		linux_progress_callback(percent, text);
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static BOOL copy_file(const char* source, const char* destination)
{
	FILE* in;
	FILE* out;
	char buffer[65536];
	size_t n;

	in = fopen(source, "rb");
	if (in == NULL) {
		uprintf("Could not open '%s': %s", source, strerror(errno));
		return FALSE;
	}
	out = fopen(destination, "wb");
	if (out == NULL) {
		uprintf("Could not create '%s': %s", destination, strerror(errno));
		fclose(in);
		return FALSE;
	}
	while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
		if (fwrite(buffer, 1, n, out) != n) {
			uprintf("Could not write '%s': %s", destination, strerror(errno));
			fclose(in);
			fclose(out);
			return FALSE;
		}
	}
	fclose(in);
	if (fclose(out) != 0)
		return FALSE;
	return TRUE;
}

static BOOL make_directory(const char* path)
{
	char tmp[PATH_MAX];
	size_t i;

	snprintf(tmp, sizeof(tmp), "%s", path);
	for (i = 1; tmp[i] != 0; i++) {
		if (tmp[i] == '/') {
			tmp[i] = 0;
			mkdir(tmp, 0755);
			tmp[i] = '/';
		}
	}
	if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
		return FALSE;
	return TRUE;
}

/*
 * Wait for the kernel to create the partition device nodes.
 */
static BOOL wait_for_partitions(DWORD drive_index, int expected, int timeout_ms)
{
	int elapsed = 0;
	DWORD i;

	while (elapsed < timeout_ms) {
		int found = 0;
		for (i = 0; i < MAX_PARTITIONS; i++) {
			char path[PATH_MAX];
			if (SelectedDrive.Partition[i].Name[0] == 0)
				continue;
			snprintf(path, sizeof(path), "/dev/%s", SelectedDrive.Partition[i].Name);
			if (access(path, F_OK) == 0)
				found++;
		}
		if (found >= expected)
			return TRUE;
		usleep(100000);
		elapsed += 100;
	}
	return FALSE;
}

/*
 * Determine the name of the partition device nodes that the kernel created.
 */
static void detect_partition_names(DWORD drive_index)
{
	DIR* dir;
	struct dirent* entry;
	const char* base = rufus_drive[drive_index].id;

	dir = opendir("/sys/class/block");
	if (dir == NULL)
		return;
	while ((entry = readdir(dir)) != NULL) {
		char path[PATH_MAX], buffer[64];
		long long start;
		int i;
		if (strncmp(entry->d_name, base, strlen(base)) != 0)
			continue;
		if (strcmp(entry->d_name, base) == 0)
			continue;
		snprintf(path, sizeof(path), "/sys/class/block/%s/start", entry->d_name);
		if (!read_sysfs_value(path, buffer, sizeof(buffer)))
			continue;
		start = strtoll(buffer, NULL, 10);
		for (i = 0; i < MAX_PARTITIONS; i++) {
			if (SelectedDrive.Partition[i].Name[0] != 0)
				continue;
			if ((uint64_t)start * 512 == SelectedDrive.Partition[i].Offset) {
				snprintf(SelectedDrive.Partition[i].Name,
					sizeof(SelectedDrive.Partition[i].Name), "%s", entry->d_name);
				SelectedDrive.Partition[i].Number = i;
				break;
			}
		}
	}
	closedir(dir);
}

/* ------------------------------------------------------------------------ */
/* Bootloader installation                                                   */
/* ------------------------------------------------------------------------ */

/*
 * Install the Syslinux BIOS bootloader: write the MBR and copy ldlinux.sys
 * to the root of the boot partition.
 */
static BOOL install_syslinux(HANDLE hDisk, const char* mount_point)
{
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;
	char* source;
	char destination[PATH_MAX];
	BOOL r = FALSE;

	fake_fd._handle = (char*)hDisk;
	set_bytes_per_sector(SelectedDrive.SectorSize);

	uprintf("Installing the Syslinux BIOS bootloader...");
	if (!write_syslinux_mbr(fp)) {
		uprintf("Could not write the Syslinux MBR");
		return FALSE;
	}

	source = linux_find_data_file("syslinux/ldlinux_v6.sys");
	if (source == NULL)
		source = linux_find_data_file("ldlinux_v6.sys");
	if (source == NULL) {
		uprintf("Could not find the ldlinux.sys file");
		return FALSE;
	}
	snprintf(destination, sizeof(destination), "%s/ldlinux.sys", mount_point);
	if (!copy_file(source, destination))
		return FALSE;

	/* Write a minimal configuration file */
	{
		FILE* f = fopen("/etc/lsb-release", "r");
		char os_name[64] = "Linux";
		if (f != NULL) {
			if (fgets(os_name, sizeof(os_name), f) == NULL)
				os_name[0] = 0;
			fclose(f);
			/* Strip the "DISTRIB_DESCRIPTION=" prefix */
			{
				char* p = strchr(os_name, '=');
				if (p != NULL)
					memmove(os_name, p + 1, strlen(p + 1) + 1);
			}
		}
		snprintf(destination, sizeof(destination), "%s/syslinux.cfg", mount_point);
		f = fopen(destination, "w");
		if (f != NULL) {
			fprintf(f, "DEFAULT linux\n"
				"PROMPT 0\n"
				"TIMEOUT 50\n"
				"LABEL linux\n"
				"  MENU LABEL %s\n"
				"  KERNEL /boot/%s\n"
				"  APPEND initrd=/boot/initrd.img quiet\n"
				"LABEL linux\n"
				"  MENU LABEL %s (safe)\n"
				"  KERNEL /boot/%s\n"
				"  APPEND initrd=/boot/initrd.img quiet\n",
				os_name, "vmlinuz", os_name, "vmlinuz");
			fclose(f);
		}
	}
	r = TRUE;
	uprintf("Syslinux bootloader installed");
	return r;
}

/*
 * Set up the EFI System Partition: copy the EFI boot files from the image
 * to the ESP, using the same naming convention as Rufus.
 */
static BOOL setup_uefi(const char* iso_path, const char* esp_mount_point)
{
	static const char* arch_names[ARCH_MAX] = {
		"", "BOOTIA32.EFI", "BOOTX64.EFI", "BOOTARM.EFI", "BOOTAA64.EFI",
		"BOOTIA64.EFI", "BOOTRISCV64.EFI", "BOOTLOONGARCH64.EFI", "BOOTEFI.EFI"
	};
	char efi_dir[PATH_MAX];
	uint8_t* buf = NULL;
	size_t size;
	int i;
	BOOL r = FALSE;

	snprintf(efi_dir, sizeof(efi_dir), "%s/EFI/BOOT", esp_mount_point);
	if (!make_directory(efi_dir)) {
		uprintf("Could not create the EFI directory");
		return FALSE;
	}

	/* Copy the EFI boot loaders that were detected while scanning the image */
	for (i = 0; i < (int)ARRAYSIZE(img_report.efi_boot_entry) &&
		img_report.efi_boot_entry[i].path[0] != 0; i++) {
		const char* basename = strrchr(img_report.efi_boot_entry[i].path, '/');
		char destination[PATH_MAX];
		if (basename == NULL)
			basename = img_report.efi_boot_entry[i].path;
		else
			basename++;
		size = ReadISOFileToBuffer(iso_path, img_report.efi_boot_entry[i].path, &buf);
		if (size == 0) {
			uprintf("Could not read '%s' from the image", img_report.efi_boot_entry[i].path);
			continue;
		}
		snprintf(destination, sizeof(destination), "%s/%s", efi_dir, basename);
		{
			FILE* f = fopen(destination, "wb");
			if (f != NULL) {
				fwrite(buf, 1, size, f);
				fclose(f);
				uprintf("Copied '%s' to the EFI System Partition", basename);
			}
		}
		safe_free(buf);
		buf = NULL;
	}

	/* If the image did not contain any EFI bootloader of its own, use the
	 * one that Rufus embeds. */
	if (!IS_EFI_BOOTABLE(img_report)) {
		const char* names[] = { "md5/bootx64.efi", "md5/bootia32.efi", "md5/bootaa64.efi" };
		const char* dests[] = { "BOOTX64.EFI", "BOOTIA32.EFI", "BOOTAA64.EFI" };
		for (i = 0; i < 3; i++) {
			char* source = linux_find_data_file(names[i]);
			char destination[PATH_MAX];
			if (source == NULL)
				continue;
			snprintf(destination, sizeof(destination), "%s/%s", efi_dir, dests[i]);
			if (copy_file(source, destination))
				uprintf("Installed the embedded '%s' bootloader", dests[i]);
		}
	}

	(void)arch_names;
	r = TRUE;
	return r;
}

/* ------------------------------------------------------------------------ */
/* The main workflow                                                         */
/* ------------------------------------------------------------------------ */

int linux_parse_fs(const char* name)
{
	int i;
	for (i = 0; i < FS_MAX; i++) {
		if (strcasecmp(name, FileSystemLabel[i]) == 0)
			return i;
	}
	if (strcasecmp(name, "fat") == 0)
		return FS_FAT16;
	if (strcasecmp(name, "fat16") == 0)
		return FS_FAT16;
	if (strcasecmp(name, "fat32") == 0)
		return FS_FAT32;
	if (strcasecmp(name, "ntfs") == 0)
		return FS_NTFS;
	if (strcasecmp(name, "exfat") == 0)
		return FS_EXFAT;
	if (strcasecmp(name, "udf") == 0)
		return FS_UDF;
	if (strcasecmp(name, "ext2") == 0)
		return FS_EXT2;
	if (strcasecmp(name, "ext3") == 0)
		return FS_EXT3;
	if (strcasecmp(name, "ext4") == 0)
		return FS_EXT4;
	return -1;
}

int linux_find_drive_index(const char* name)
{
	int i;
	const char* base = name;

	/* Accept both "sdb" and "/dev/sdb" */
	if (strncmp(name, "/dev/", 5) == 0)
		base = name + 5;
	for (i = 0; i < (int)MAX_DRIVES; i++) {
		if (rufus_drive[i].id == NULL)
			continue;
		if (strcmp(rufus_drive[i].id, base) == 0)
			return i;
	}
	return -1;
}

void linux_list_devices(FILE* stream, BOOL all)
{
	DWORD i;
	fprintf(stream, "Available devices:\n");
	fprintf(stream, "%-4s %-12s %-10s %-28s %s\n", "IDX", "DEVICE", "SIZE", "MODEL", "TYPE");
	for (i = 0; i < MAX_DRIVES; i++) {
		usb_device_props props;
		char size[32];
		char model[128];
		char sysfs_path[PATH_MAX];
		if (rufus_drive[i].id == NULL)
			continue;
		memset(&props, 0, sizeof(props));
		GetUSBProperties(rufus_drive[i].id, &props);
		if (!all && !props.is_USB)
			continue;
		snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/block/%s/device/model", rufus_drive[i].id);
		model[0] = 0;
		read_sysfs_value(sysfs_path, model, sizeof(model));
		snprintf(size, sizeof(size), "%.2f GB", rufus_drive[i].size / 1073741824.0);
		fprintf(stream, "%-4u %-12s %-10s %-28s %s\n", i, rufus_drive[i].id, size,
			model[0] ? model : "(unknown)", props.is_USB ? "USB" : "internal");
	}
}

int linux_write_image(const linux_options* opts)
{
	HANDLE hDisk = INVALID_HANDLE_VALUE;
	LINUX_PARTITION partitions[MAX_PARTITIONS];
	char* mount_point = NULL;
	char* esp_mount_point = NULL;
	uint64_t disk_size;
	uint64_t esp_size = 0;
	uint64_t data_offset;
	int nPartitions = 0;
	int i;
	BOOL r = FALSE;

	/* Report what would be done, without touching the device */
	if (opts->dry_run) {
		uint64_t disk_size = GetDriveSize(linux_find_drive_index(opts->device));
		uprintf("Target device: %s (%s)", GetPhysicalName(linux_find_drive_index(opts->device)),
			SizeToHumanReadable(disk_size, FALSE, FALSE));
		SelectedDrive.DiskSize = disk_size;
		SelectedDrive.SectorSize = 512;
		SelectedDrive.DeviceNumber = (DWORD)linux_find_drive_index(opts->device);
		SelectedDrive.PartitionStyle = (strcmp(opts->partition_style, "gpt") == 0) ?
			PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;
		for (i = 0; i < 2; i++) {
			uint64_t offset = (i == 0) ? 1 * MB : 0;
			uint64_t psize = 0;
			if (opts->use_uefi) {
				offset = (i == 0) ? 1 * MB : 0;
				psize = (i == 0) ? 512 * MB : 0;
			}
			(void)offset; (void)psize;
		}
		{
			uint64_t esp_size = 512 * MB;
			uint64_t data_offset;
			if (opts->use_uefi) {
				if (esp_size > disk_size / 4)
					esp_size = disk_size / 4;
				esp_size = (esp_size / MB) * MB;
				/* Note: SizeToHumanReadable() returns a static buffer, so
				 * the values have to be used one at a time. */
				uprintf("Would create an EFI System Partition: offset=1 MB size=%s",
					SizeToHumanReadable(esp_size, FALSE, FALSE));
				data_offset = ((1 * MB + esp_size + MB - 1) / MB) * MB;
				uprintf("Would create a data partition: offset=%s size=%s fs=%s",
					SizeToHumanReadable(data_offset, FALSE, FALSE),
					SizeToHumanReadable(disk_size - data_offset, FALSE, FALSE),
					opts->fs);
			} else {
				uprintf("Would create a data partition: offset=1 MB size=%s fs=%s",
					SizeToHumanReadable(disk_size - 1 * MB, FALSE, FALSE),
					opts->fs);
			}
		}
		return TRUE;
	}

	/* Open the physical device */
	hDisk = GetPhysicalHandle(linux_find_drive_index(opts->device), FALSE, TRUE, FALSE);
	if (hDisk == INVALID_HANDLE_VALUE) {
		uprintf("Could not open the device '%s'", opts->device);
		return FALSE;
	}

	disk_size = GetDriveSize(linux_find_drive_index(opts->device));
	if (disk_size == 0) {
		uprintf("Could not determine the size of the device");
		goto out;
	}
	SelectedDrive.DiskSize = disk_size;
	SelectedDrive.SectorSize = 512;
	SelectedDrive.DeviceNumber = linux_find_drive_index(opts->device);
	SelectedDrive.PartitionStyle = (strcmp(opts->partition_style, "gpt") == 0) ?
		PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;

	uprintf("Target device: %s (%s)", GetPhysicalName(SelectedDrive.DeviceNumber),
		SizeToHumanReadable(disk_size, FALSE, FALSE));
	uprintf("Existing partition table: %s",
		(linux_detect_partition_style(SelectedDrive.DeviceNumber) == PARTITION_STYLE_GPT) ?
		"GPT" : "MBR");

	/* Compute the partition layout.
	 * For UEFI boot, we create an EFI System Partition followed by a data
	 * partition that will hold the contents of the image. */
	memset(partitions, 0, sizeof(partitions));
	if (opts->use_uefi) {
		esp_size = 512 * MB;
		if (esp_size > disk_size / 4)
			esp_size = disk_size / 4;
		/* Align the ESP on a 1 MB boundary */
		esp_size = (esp_size / MB) * MB;
		partitions[0].Offset = 1 * MB;
		partitions[0].Size = esp_size;
		partitions[0].FSType = FS_FAT32;
		snprintf(partitions[0].Name, sizeof(partitions[0].Name), "%s1", rufus_drive[SelectedDrive.DeviceNumber].id);
		data_offset = partitions[0].Offset + partitions[0].Size;
		/* Align the data partition on a 1 MB boundary as well */
		data_offset = ((data_offset + MB - 1) / MB) * MB;
		partitions[1].Offset = data_offset;
		partitions[1].Size = disk_size - data_offset;
		partitions[1].FSType = linux_parse_fs(opts->fs);
		snprintf(partitions[1].Name, sizeof(partitions[1].Name), "%s2", rufus_drive[SelectedDrive.DeviceNumber].id);
		nPartitions = 2;
	} else {
		partitions[0].Offset = 1 * MB;
		partitions[0].Size = disk_size - (1 * MB);
		partitions[0].FSType = linux_parse_fs(opts->fs);
		snprintf(partitions[0].Name, sizeof(partitions[0].Name), "%s1", rufus_drive[SelectedDrive.DeviceNumber].id);
		nPartitions = 1;
	}

	if (opts->dry_run) {
		for (i = 0; i < nPartitions; i++) {
			uprintf("Would create partition %d: offset=%s size=%s fs=%s",
				i + 1, SizeToHumanReadable(partitions[i].Offset, FALSE, FALSE),
				SizeToHumanReadable(partitions[i].Size, FALSE, FALSE),
				linux_fs_name(partitions[i].FSType));
		}
		r = TRUE;
		goto out;
	}

	/* Write the partition table */
	SelectedDrive.nPartitions = nPartitions;
	memcpy(SelectedDrive.Partition, partitions, sizeof(partitions));
	uprintf("Writing the %s partition table...",
		SelectedDrive.PartitionStyle == PARTITION_STYLE_GPT ? "GPT" : "MBR");
	if (!WritePartitionTable(hDisk, SelectedDrive.PartitionStyle, partitions, nPartitions))
		goto out;

	/* Wait for the kernel to pick up the new partition table */
	usleep(500000);
	RefreshPartitionTable(hDisk);
	detect_partition_names(SelectedDrive.DeviceNumber);
	if (!wait_for_partitions(SelectedDrive.DeviceNumber, nPartitions, 10000)) {
		uprintf("WARNING: The partition device nodes did not appear in time");
	}

	/* Format the partitions */
	for (i = 0; i < nPartitions; i++) {
		const char* label = (opts->label != NULL) ? opts->label :
			(i == 0 && opts->use_uefi) ? "RUFUSEFI" : "RUFUS";
		SelectedDrive.Partition[i].Offset = partitions[i].Offset;
		SelectedDrive.Partition[i].Size = partitions[i].Size;
		if (!FormatPartition(SelectedDrive.DeviceNumber, partitions[i].Offset,
			0, (USHORT)partitions[i].FSType, label, 0))
			goto out;
	}

	/* The kernel may have cached the old partition sizes: refresh them */
	RefreshPartitionTable(hDisk);

	/* Mount the data partition */
	mount_point = GetLogicalName(SelectedDrive.DeviceNumber,
		opts->use_uefi ? partitions[1].Offset : partitions[0].Offset, TRUE, FALSE);
	if (mount_point == NULL) {
		uprintf("Could not mount the data partition");
		goto out;
	}
	uprintf("Mounted the data partition on %s", mount_point);

	if (opts->use_uefi) {
		esp_mount_point = GetLogicalName(SelectedDrive.DeviceNumber, partitions[0].Offset, TRUE, FALSE);
		if (esp_mount_point == NULL) {
			uprintf("Could not mount the EFI System Partition");
			goto out;
		}
		uprintf("Mounted the EFI System Partition on %s", esp_mount_point);
	}

	/* Scan the image, then extract it */
	uprintf("Analysing the image...");
	if (!ExtractISO(opts->image, mount_point, TRUE)) {
		uprintf("ERROR: Could not analyse the image");
		ErrorStatus = RUFUS_ERROR(ERROR_ISO_SCAN);
		goto out;
	}
	uprintf("Extracting the image...");
	linux_progress(0, "Extracting the image...");
	if (!ExtractISO(opts->image, mount_point, FALSE)) {
		uprintf("ERROR: Could not extract the image");
		ErrorStatus = RUFUS_ERROR(ERROR_ISO_EXTRACT);
		goto out;
	}

	/* Set up the EFI boot files */
	if (opts->use_uefi) {
		if (!setup_uefi(opts->image, esp_mount_point))
			goto out;
	}

	/* Install the BIOS bootloader */
	if (opts->use_bios) {
		const char* boot_partition = opts->use_uefi ? esp_mount_point : mount_point;
		if (!install_syslinux(hDisk, boot_partition))
			goto out;
	}

	uprintf("Done!");
	r = TRUE;
out:
	if (mount_point != NULL)
		free(mount_point);
	if (esp_mount_point != NULL)
		free(esp_mount_point);
	if (hDisk != INVALID_HANDLE_VALUE)
		CloseHandle(hDisk);
	return r;
}

/*
 * Extract an image to a directory.
 */
int linux_extract_image(const char* image, const char* directory)
{
	if (!make_directory(directory))
		return FALSE;
	uprintf("Analysing '%s'...", image);
	if (!ExtractISO(image, directory, TRUE)) {
		uprintf("ERROR: Could not analyse '%s'", image);
		return FALSE;
	}
	uprintf("Extracting '%s' to '%s'...", image, directory);
	if (!ExtractISO(image, directory, FALSE)) {
		uprintf("ERROR: Could not extract '%s'", image);
		return FALSE;
	}
	uprintf("Done!");
	return TRUE;
}

/*
 * A self test for the partition table code. Since the partition table is
 * written through a plain file handle, we can exercise it on a regular file.
 */
int linux_selftest(const char* path)
{
	HANDLE h;
	LINUX_PARTITION partitions[2];
	uint64_t size = 64 * MB;
	BOOL r = FALSE;

	memset(partitions, 0, sizeof(partitions));
	partitions[0].Offset = 1 * MB;
	partitions[0].Size = 16 * MB;
	partitions[0].FSType = FS_FAT32;
	partitions[1].Offset = 17 * MB;
	partitions[1].Size = size - 17 * MB;
	partitions[1].FSType = FS_NTFS;

	h = CreateFileU(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		uprintf("Could not create '%s': %s", path, WindowsErrorString());
		return FALSE;
	}
	/* Make sure the file is the expected size */
	{
		LARGE_INTEGER li;
		li.QuadPart = (LONGLONG)size;
		if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
			uprintf("Could not size the file");
			CloseHandle(h);
			return FALSE;
		}
	}
	SelectedDrive.DiskSize = size;
	SelectedDrive.SectorSize = 512;

	uprintf("Writing a GPT partition table to '%s'...", path);
	if (!WritePartitionTable(h, PARTITION_STYLE_GPT, partitions, 2))
		goto out;
	r = TRUE;
out:
	CloseHandle(h);
	return r;
}

/*
 * The same, but for an MBR partition table.
 */
int linux_selftest_mbr(const char* path)
{
	HANDLE h;
	LINUX_PARTITION partitions[2];
	uint64_t size = 64 * MB;
	BOOL r = FALSE;

	memset(partitions, 0, sizeof(partitions));
	partitions[0].Offset = 1 * MB;
	partitions[0].Size = 16 * MB;
	partitions[0].FSType = FS_FAT32;
	partitions[1].Offset = 17 * MB;
	partitions[1].Size = size - 17 * MB;
	partitions[1].FSType = FS_NTFS;

	h = CreateFileU(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		uprintf("Could not create '%s': %s", path, WindowsErrorString());
		return FALSE;
	}
	{
		LARGE_INTEGER li;
		li.QuadPart = (LONGLONG)size;
		if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) || !SetEndOfFile(h)) {
			uprintf("Could not size the file");
			CloseHandle(h);
			return FALSE;
		}
	}
	SelectedDrive.DiskSize = size;
	SelectedDrive.SectorSize = 512;

	uprintf("Writing an MBR partition table to '%s'...", path);
	if (!WritePartitionTable(h, PARTITION_STYLE_MBR, partitions, 2))
		goto out;
	r = TRUE;
out:
	CloseHandle(h);
	return r;
}

/*
 * Refresh the list of candidate devices.
 */
void linux_refresh_devices(void)
{
	GetDevices(0);
}
