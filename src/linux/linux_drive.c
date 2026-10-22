/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - drive and partition management.
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
 * This file implements the Linux equivalent of the Windows drive management
 * code: partition table creation (MBR and GPT), partition enumeration,
 * mounting and unmounting.
 *
 * The MBR boot records themselves are produced by the vendored ms-sys
 * library, which is platform independent.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/fs.h>
#endif

#include "linux_api.h"
#include "ms-sys/inc/br.h"
#include "ms-sys/inc/file.h"
#include "posix/win_compat.h"

#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)
#endif
#ifndef BLKRRPART
#define BLKRRPART _IO(0x12, 95)
#endif
#ifndef BLKSSZGET
#define BLKSSZGET _IOR(0x12, 104, int)
#endif

#define SECTOR_SIZE 512
#define MAX_PARTITION_ENTRIES 128
#define GPT_ENTRY_SIZE 128

RUFUS_DRIVE_INFO SelectedDrive;

const char* const linux_fs_labels[FS_MAX] = {
	"FAT", "FAT32", "NTFS", "UDF", "exFAT", "ReFS", "ext2", "ext3", "ext4"
};

/* ------------------------------------------------------------------------ */
/* On-disk structures                                                        */
/* ------------------------------------------------------------------------ */

typedef struct {
	BYTE BootIndicator;
	BYTE StartCHS[3];
	BYTE Type;
	BYTE EndCHS[3];
	uint32_t StartLBA;
	uint32_t SizeLBA;
} __attribute__((packed)) MBR_PARTITION_ENTRY;

typedef struct {
	BYTE BootCode[440];
	uint32_t DiskSignature;
	WORD CopyProtect;
	MBR_PARTITION_ENTRY Partition[4];
	BYTE Signature[2];	/* 0x55, 0xAA */
} MBR_DISK;

typedef struct {
	uint64_t Signature;
	uint32_t Revision;
	uint32_t HeaderSize;
	uint32_t CRC32;
	uint32_t Reserved;
	uint64_t MyLBA;
	uint64_t AlternateLBA;
	uint64_t FirstUsableLBA;
	uint64_t LastUsableLBA;
	GUID DiskGUID;
	uint64_t PartitionEntryLBA;
	uint32_t NumberOfPartitionEntries;
	uint32_t SizeOfPartitionEntry;
	uint32_t PartitionEntryArrayCRC32;
} GPT_HEADER;

typedef struct {
	GUID PartitionTypeGUID;
	GUID UniquePartitionGUID;
	uint64_t StartingLBA;
	uint64_t EndingLBA;
	uint64_t Attributes;
	uint16_t PartitionName[36];
} GPT_PARTITION_ENTRY;

#define GPT_SIGNATURE 0x5452415020494645ULL	/* "EFI PART" */
#define GPT_REVISION  0x00010000
#define GPT_HEADER_SIZE 92
#define MBR_SIGNATURE 0x55AA

/* ------------------------------------------------------------------------ */
/* GUID helpers                                                              */
/* ------------------------------------------------------------------------ */

static BOOL string_to_guid(const char* str, GUID* guid)
{
	unsigned int a, b, c;
	unsigned int d[8];
	int i;

	if (sscanf(str, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x",
		&a, &b, &c, &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6], &d[7]) != 11)
		return FALSE;
	guid->Data1 = a;
	guid->Data2 = (WORD)b;
	guid->Data3 = (WORD)c;
	for (i = 0; i < 8; i++)
		guid->Data4[i] = (BYTE)d[i];
	return TRUE;
}

/* Generate a random GUID */
static void generate_guid(GUID* guid)
{
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	uint8_t buffer[16];
	if (fd >= 0) {
		ssize_t n = read(fd, buffer, sizeof(buffer));
		close(fd);
		if (n != (ssize_t)sizeof(buffer)) {
			memset(buffer, 0, sizeof(buffer));
		}
	} else {
		memset(buffer, 0, sizeof(buffer));
	}
	/* Set the version (4) and the variant bits, as per RFC 4122 */
	buffer[7] = (buffer[7] & 0x0F) | 0x40;
	buffer[8] = (buffer[8] & 0x3F) | 0x80;
	guid->Data1 = (DWORD)(buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | ((uint32_t)buffer[3] << 24));
	guid->Data2 = (WORD)(buffer[4] | (buffer[5] << 8));
	guid->Data3 = (WORD)(buffer[6] | (buffer[7] << 8));
	memcpy(guid->Data4, buffer + 8, 8);
}

/* ------------------------------------------------------------------------ */
/* CRC-32 (as used by the GPT)                                               */
/* ------------------------------------------------------------------------ */

static uint32_t gpt_crc32(const void* data, size_t length)
{
	static uint32_t table[256];
	static int initialized = 0;
	const uint8_t* p = (const uint8_t*)data;
	uint32_t crc = 0xFFFFFFFF;

	if (!initialized) {
		uint32_t i, j, c;
		for (i = 0; i < 256; i++) {
			c = i;
			for (j = 0; j < 8; j++)
				c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
			table[i] = c;
		}
		initialized = 1;
	}
	while (length--)
		crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFF;
}

/* ------------------------------------------------------------------------ */
/* Low level helpers                                                         */
/* ------------------------------------------------------------------------ */

static BOOL write_at(HANDLE h, uint64_t offset, const void* data, size_t length)
{
	LARGE_INTEGER li;
	DWORD written = 0;

	li.QuadPart = (LONGLONG)offset;
	if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN))
		return FALSE;
	return WriteFile(h, data, (DWORD)length, &written, NULL) && written == length;
}

static BOOL read_at(HANDLE h, uint64_t offset, void* data, size_t length)
{
	LARGE_INTEGER li;
	DWORD read_bytes = 0;

	li.QuadPart = (LONGLONG)offset;
	if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN))
		return FALSE;
	return ReadFile(h, data, (DWORD)length, &read_bytes, NULL) && read_bytes == length;
}

char* GetPhysicalName(DWORD DriveIndex)
{
	static char path[PATH_MAX];
	if (DriveIndex >= MAX_DRIVES || rufus_drive[DriveIndex].id == NULL)
		return NULL;
	snprintf(path, sizeof(path), "/dev/%s", rufus_drive[DriveIndex].id);
	return path;
}

HANDLE GetPhysicalHandle(DWORD DriveIndex, BOOL bLockDrive, BOOL bWriteAccess, BOOL bWriteShare)
{
	const char* path = GetPhysicalName(DriveIndex);
	int flags;

	(void)bLockDrive;
	(void)bWriteShare;
	if (path == NULL)
		return INVALID_HANDLE_VALUE;
	flags = (bWriteAccess ? O_RDWR : O_RDONLY) | O_CLOEXEC;
	/* O_EXCL makes sure we get exclusive access to the device, which is
	 * what we want when we are about to repartition it. */
	return CreateFileA(path, (bWriteAccess ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ),
		0, NULL, OPEN_EXISTING, 0, NULL);
}

uint64_t GetDriveSize(DWORD DriveIndex)
{
	const char* path = GetPhysicalName(DriveIndex);
	int fd;
	uint64_t size = 0;

	if (path == NULL)
		return 0;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		/* We may not have the privileges to open the device: fall back to
		 * the size that sysfs reports (in 512 byte sectors). */
		char sysfs_path[PATH_MAX];
		char buffer[64];
		if (DriveIndex < MAX_DRIVES && rufus_drive[DriveIndex].id != NULL) {
			snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/block/%s/size",
				rufus_drive[DriveIndex].id);
			if (read_sysfs_value(sysfs_path, buffer, sizeof(buffer)))
				return (uint64_t)strtoull(buffer, NULL, 10) * 512;
		}
		return 0;
	}
	if (ioctl(fd, BLKGETSIZE64, &size) != 0) {
		off_t off = lseek(fd, 0, SEEK_END);
		size = (off > 0) ? (uint64_t)off : 0;
	}
	close(fd);
	return size;
}

BOOL IsMediaPresent(DWORD DriveIndex)
{
	return GetPhysicalName(DriveIndex) != NULL;
}

/*
 * Refresh the kernel's partition table for a device.
 */
BOOL RefreshPartitionTable(HANDLE hDisk)
{
	int fd = win_handle_to_fd(hDisk);
	if (fd < 0)
		return FALSE;
	if (ioctl(fd, BLKRRPART, NULL) != 0) {
		/* Some kernels require the device to be re-read */
		if (ioctl(fd, BLKRRPART, NULL) != 0)
			return FALSE;
	}
	return TRUE;
}

/*
 * Determine the partition style of a disk.
 */
static int detect_partition_style(HANDLE hDisk, uint64_t disk_size)
{
	uint8_t sector[SECTOR_SIZE];
	uint64_t gpt_lba;

	if (!read_at(hDisk, SECTOR_SIZE, sector, sizeof(sector)))
		return PARTITION_STYLE_MBR;
	if (memcmp(sector, "EFI PART", 8) == 0)
		return PARTITION_STYLE_GPT;
	/* The backup GPT header is located at the end of the disk */
	gpt_lba = disk_size / SECTOR_SIZE - 1;
	if (gpt_lba > 0 && read_at(hDisk, gpt_lba * SECTOR_SIZE, sector, sizeof(sector)) &&
		memcmp(sector, "EFI PART", 8) == 0)
		return PARTITION_STYLE_GPT;
	return PARTITION_STYLE_MBR;
}

/*
 * Write an MBR partition table.
 */
static BOOL write_mbr(HANDLE hDisk, const LINUX_PARTITION* partitions, int nPartitions)
{
	MBR_DISK* mbr = (MBR_DISK*)calloc(1, sizeof(MBR_DISK));
	BOOL r = FALSE;
	int i;

	if (mbr == NULL)
		return FALSE;

	/* Use the Rufus MBR code, which contains the standard boot code as well
	 * as the Rufus partition table markers. Note that this only writes the
	 * first 440 bytes (the boot code) plus the boot mark, so that our own
	 * partition table is preserved. */
	{
		FAKE_FD fake_fd = { 0 };
		FILE* fp = (FILE*)&fake_fd;
		fake_fd._handle = (char*)hDisk;
		set_bytes_per_sector(SECTOR_SIZE);
		if (!write_rufus_mbr(fp))
			goto out;
	}

	/* Fill in the partition table */
	for (i = 0; i < 4 && i < nPartitions; i++) {
		const LINUX_PARTITION* p = &partitions[i];
		mbr->Partition[i].BootIndicator = (i == 0) ? 0x80 : 0x00;
		mbr->Partition[i].Type = (p->FSType == FS_NTFS) ? 0x07 :
			(p->FSType == FS_FAT16) ? 0x06 : 0x0C;
		mbr->Partition[i].StartLBA = (uint32_t)(p->Offset / SECTOR_SIZE);
		mbr->Partition[i].SizeLBA = (uint32_t)(p->Size / SECTOR_SIZE);
	}

	/* Only write the partition table itself (offset 0x1BE, 64 bytes) so as
	 * not to overwrite the boot code we just wrote. */
	if (!write_at(hDisk, 0x1BE, mbr->Partition, sizeof(mbr->Partition)))
		goto out;

	r = TRUE;
out:
	free(mbr);
	return r;
}

/*
 * Write a GPT partition table.
 */
static BOOL write_gpt(HANDLE hDisk, uint64_t disk_size, const LINUX_PARTITION* partitions, int nPartitions)
{
	GPT_HEADER header;
	GPT_HEADER backup;
	GPT_PARTITION_ENTRY* entries;
	uint64_t total_sectors = disk_size / SECTOR_SIZE;
	uint64_t entries_lba = 2;
	uint64_t entries_sectors = (MAX_PARTITION_ENTRIES * GPT_ENTRY_SIZE) / SECTOR_SIZE;
	uint64_t first_usable = entries_lba + entries_sectors;
	uint64_t last_usable = total_sectors - entries_sectors - 2;
	uint64_t backup_header_lba = total_sectors - 1;
	uint64_t backup_entries_lba = backup_header_lba - entries_sectors;
	int i;
	BOOL r = FALSE;

	entries = (GPT_PARTITION_ENTRY*)calloc(MAX_PARTITION_ENTRIES, sizeof(GPT_PARTITION_ENTRY));
	if (entries == NULL)
		return FALSE;

	for (i = 0; i < nPartitions && i < MAX_PARTITION_ENTRIES; i++) {
		const LINUX_PARTITION* p = &partitions[i];
		GUID type;
		BOOL is_esp = (i == 0 && p->FSType == FS_FAT32);
		const char* type_str = is_esp ? "C12A7328-F81F-11D2-BA4B-00A0C93EC93B" :
			(p->FSType == FS_NTFS) ? "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7" :
			(p->FSType == FS_FAT16) ? "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7" :
			(p->FSType == FS_EXFAT) ? "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7" :
			(p->FSType == FS_EXT2 || p->FSType == FS_EXT3 || p->FSType == FS_EXT4) ?
				"0FC63DAF-8483-4772-8E79-3D69D8477DE4" :
				"0FC63DAF-8483-4772-8E79-3D69D8477DE4";
		string_to_guid(type_str, &type);
		entries[i].PartitionTypeGUID = type;
		generate_guid(&entries[i].UniquePartitionGUID);
		entries[i].StartingLBA = p->Offset / SECTOR_SIZE;
		entries[i].EndingLBA = (p->Offset + p->Size) / SECTOR_SIZE - 1;
		/* The partition must not extend past the last usable LBA */
		if (entries[i].EndingLBA > last_usable)
			entries[i].EndingLBA = last_usable;
		entries[i].Attributes = 0;
	}

	/* Primary header */
	memset(&header, 0, sizeof(header));
	generate_guid(&header.DiskGUID);
	header.Signature = GPT_SIGNATURE;
	header.Revision = GPT_REVISION;
	header.HeaderSize = GPT_HEADER_SIZE;
	header.MyLBA = 1;
	header.AlternateLBA = backup_header_lba;
	header.FirstUsableLBA = first_usable;
	header.LastUsableLBA = last_usable;
	header.DiskGUID = header.DiskGUID;
	header.PartitionEntryLBA = entries_lba;
	header.NumberOfPartitionEntries = MAX_PARTITION_ENTRIES;
	header.SizeOfPartitionEntry = GPT_ENTRY_SIZE;
	header.PartitionEntryArrayCRC32 = gpt_crc32(entries, MAX_PARTITION_ENTRIES * GPT_ENTRY_SIZE);
	header.CRC32 = gpt_crc32(&header, GPT_HEADER_SIZE);

	/* Backup header */
	memcpy(&backup, &header, sizeof(backup));
	backup.MyLBA = backup_header_lba;
	backup.AlternateLBA = 1;
	backup.PartitionEntryLBA = backup_entries_lba;
	backup.CRC32 = 0;
	backup.CRC32 = gpt_crc32(&backup, GPT_HEADER_SIZE);

	/* Protective MBR */
	{
		MBR_DISK mbr;
		memset(&mbr, 0, sizeof(mbr));
		mbr.Partition[0].BootIndicator = 0;
		mbr.Partition[0].Type = 0xEE;
		mbr.Partition[0].StartLBA = 1;
		mbr.Partition[0].SizeLBA = (uint32_t)(total_sectors - 1);
		mbr.Signature[0] = 0x55;
		mbr.Signature[1] = 0xAA;
		if (!write_at(hDisk, 0, &mbr, sizeof(mbr)))
			goto out;
	}

	if (!write_at(hDisk, entries_lba * SECTOR_SIZE, entries, MAX_PARTITION_ENTRIES * GPT_ENTRY_SIZE))
		goto out;
	if (!write_at(hDisk, SECTOR_SIZE, &header, GPT_HEADER_SIZE))
		goto out;
	if (!write_at(hDisk, backup_entries_lba * SECTOR_SIZE, entries, MAX_PARTITION_ENTRIES * GPT_ENTRY_SIZE))
		goto out;
	if (!write_at(hDisk, backup_header_lba * SECTOR_SIZE, &backup, GPT_HEADER_SIZE))
		goto out;

	r = TRUE;
out:
	free(entries);
	return r;
}

/*
 * Detect the partition table style of a device.
 */
int linux_detect_partition_style(DWORD DriveIndex)
{
	HANDLE hDisk = GetPhysicalHandle(DriveIndex, FALSE, FALSE, FALSE);
	int style = PARTITION_STYLE_MBR;

	if (hDisk == INVALID_HANDLE_VALUE)
		return style;
	style = detect_partition_style(hDisk, GetDriveSize(DriveIndex));
	CloseHandle(hDisk);
	return style;
}

/*
 * Create a partition table on the selected drive.
 */
BOOL WritePartitionTable(HANDLE hDisk, int style, const LINUX_PARTITION* partitions, int nPartitions)
{
	uint64_t disk_size = SelectedDrive.DiskSize;
	BOOL r = FALSE;

	if (style == PARTITION_STYLE_GPT)
		r = write_gpt(hDisk, disk_size, partitions, nPartitions);
	else
		r = write_mbr(hDisk, partitions, nPartitions);

	if (!r) {
		uprintf("Could not write the partition table: %s", WindowsErrorString());
		return FALSE;
	}
	RefreshPartitionTable(hDisk);
	return TRUE;
}

/*
 * Analyze the MBR of a disk.
 */
BOOL AnalyzeMBR(HANDLE hPhysicalDrive, const char* TargetName, BOOL bSilent)
{
	uint8_t sector[SECTOR_SIZE];
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;

	(void)TargetName;
	(void)bSilent;
	fake_fd._handle = (char*)hPhysicalDrive;
	set_bytes_per_sector(SECTOR_SIZE);
	if (!read_at(hPhysicalDrive, 0, sector, sizeof(sector)))
		return FALSE;
	if (is_rufus_mbr(fp) || is_syslinux_mbr(fp) || is_grub2_mbr(fp) ||
		is_grub4dos_mbr(fp) || is_dos_mbr(fp) || is_zero_mbr(fp))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* Mounting                                                                  */
/* ------------------------------------------------------------------------ */

static char* get_mount_point(DWORD DriveIndex, uint64_t PartitionOffset)
{
	char path[PATH_MAX];
	const char* name;
	DWORD i;
	static char mount_point[PATH_MAX];

	name = (DriveIndex < MAX_DRIVES && rufus_drive[DriveIndex].id != NULL) ?
		rufus_drive[DriveIndex].id : "disk";
	snprintf(path, sizeof(path), "/run/rufus/%s", name);
	if (mkdir(path, 0755) < 0 && errno != EEXIST)
		return NULL;

	for (i = 0; i < MAX_PARTITIONS; i++) {
		snprintf(mount_point, sizeof(mount_point), "%s/p%u", path, i);
		if (mkdir(mount_point, 0755) < 0 && errno != EEXIST)
			return NULL;
		return mount_point;
	}
	return NULL;
}

/*
 * Mount a partition and return its mount point.
 */
char* GetLogicalName(DWORD DriveIndex, uint64_t PartitionOffset, BOOL bWriteAccess, BOOL bSilent)
{
	const char* device = GetPhysicalName(DriveIndex);
	char partition[PATH_MAX];
	char* mount_point;
	FILE* f;
	int number = -1;

	(void)bSilent;
	if (device == NULL)
		return NULL;

	/* Determine the partition number for the requested offset */
	snprintf(partition, sizeof(partition), "%s", device);
	{
		/* Scan the sysfs partition list to find the partition whose start
		 * sector matches the requested offset. */
		DIR* dir = opendir("/sys/class/block");
		struct dirent* entry;
		if (dir != NULL) {
			while ((entry = readdir(dir)) != NULL) {
				char path[PATH_MAX], buffer[64];
				long long start = 0;
				if (strncmp(entry->d_name, rufus_drive[DriveIndex].id, strlen(rufus_drive[DriveIndex].id)) != 0)
					continue;
				snprintf(path, sizeof(path), "/sys/class/block/%s/start", entry->d_name);
				if (!read_sysfs_value(path, buffer, sizeof(buffer)))
					continue;
				start = strtoll(buffer, NULL, 10);
				if ((uint64_t)start * SECTOR_SIZE == PartitionOffset) {
					number = atoi(entry->d_name + strlen(rufus_drive[DriveIndex].id));
					break;
				}
			}
			closedir(dir);
		}
	}
	if (number < 0)
		return NULL;

	snprintf(partition, sizeof(partition), "%s%d", device, number);

	mount_point = get_mount_point(DriveIndex, PartitionOffset);
	if (mount_point == NULL)
		return NULL;

	/* Determine the file system type of the partition */
	f = fopen("/proc/mounts", "r");
	if (f != NULL) {
		char line[1024];
		while (fgets(line, sizeof(line), f) != NULL) {
			char src[512], dst[512], type[64];
			if (sscanf(line, "%511s %511s %63s", src, dst, type) != 3)
				continue;
			if (strcmp(src, partition) == 0) {
				fclose(f);
				return strdup(dst);
			}
		}
		fclose(f);
	}

	/* Not mounted yet: try to mount it */
	if (mount(partition, mount_point, NULL, bWriteAccess ? 0 : MS_RDONLY, NULL) != 0) {
		uprintf("Could not mount %s: %s", partition, strerror(errno));
		return NULL;
	}
	return strdup(mount_point);
}

BOOL UnmountVolume(HANDLE hDrive)
{
	(void)hDrive;
	return TRUE;
}

/*
 * Retrieve the label of the volume that is mounted on a drive.
 */
BOOL GetDriveLabel(DWORD DriveIndex, char* letters, char** label, BOOL bSilent)
{
	char* mount_point;
	char buffer[256];

	(void)bSilent;
	if (letters != NULL)
		letters[0] = 0;
	if (label != NULL)
		*label = NULL;

	mount_point = GetLogicalName(DriveIndex, 0, FALSE, TRUE);
	if (mount_point == NULL)
		return FALSE;
	{
		char path[PATH_MAX];
		FILE* f;
		snprintf(path, sizeof(path), "%s/../info", mount_point);
		f = fopen(path, "r");
		if (f != NULL) {
			while (fgets(buffer, sizeof(buffer), f) != NULL) {
				char* colon = strchr(buffer, ':');
				if (colon == NULL)
					continue;
				*colon = 0;
				if (strcmp(buffer, "LABEL") == 0) {
					char* value = colon + 1;
					while (*value == ' ')
						value++;
					value[strcspn(value, "\r\n")] = 0;
					if (label != NULL)
						*label = strdup(value);
					break;
				}
			}
			fclose(f);
		}
	}
	free(mount_point);
	return (label != NULL && *label != NULL);
}

/*
 * Retrieve the file system of the first partition of a drive.
 */
BOOL GetDrivePartitionData(DWORD DriveIndex, char* FileSystemName, DWORD FileSystemNameSize, BOOL bSilent)
{
	char* mount_point;
	FILE* f;

	(void)bSilent;
	if (FileSystemName != NULL && FileSystemNameSize > 0)
		FileSystemName[0] = 0;

	mount_point = GetLogicalName(DriveIndex, 0, FALSE, TRUE);
	if (mount_point == NULL)
		return FALSE;
	f = fopen("/proc/mounts", "r");
	if (f != NULL) {
		char line[1024];
		char src[512], dst[512], type[64];
		while (fgets(line, sizeof(line), f) != NULL) {
			if (sscanf(line, "%511s %511s %63s", src, dst, type) != 3)
				continue;
			if (strcmp(src, mount_point) == 0) {
				if (FileSystemName != NULL)
					snprintf(FileSystemName, FileSystemNameSize, "%s", type);
				fclose(f);
				free(mount_point);
				return TRUE;
			}
		}
		fclose(f);
	}
	free(mount_point);
	return FALSE;
}

char* GetFsName(int fstype, uint64_t cluster_size)
{
	(void)cluster_size;
	if (fstype < 0 || fstype >= FS_MAX)
		return (char*)"Unknown";
	return (char*)linux_fs_labels[fstype];
}

const char* linux_fs_name(int fstype)
{
	if (fstype < 0 || fstype >= FS_MAX)
		return "Unknown";
	return linux_fs_labels[fstype];
}
