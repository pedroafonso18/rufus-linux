/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - platform specific API.
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
#include "rufus.h"

/* Partition styles (same values as the Windows PARTITION_STYLE_* constants) */
#define PARTITION_STYLE_MBR 0
#define PARTITION_STYLE_GPT 1

/* The file system types themselves (FS_*) are declared in rufus.h */
extern const char* const linux_fs_labels[FS_MAX];

/* USB speed codes (mirrors the Windows ones) */
#define USB_SPEED_UNKNOWN 0
#define USB_SPEED_LOW_1_5M 1
#define USB_SPEED_FULL_12M 2
#define USB_SPEED_HIGH_480M 3
#define USB_SPEED_SUPER_5G 4
#define USB_SPEED_SUPER_10G 5
#define USB_SPEED_SUPER_20G 6
#define USB_SPEED_SUPER_40G 7
#define USB_SPEED_SUPER_80G 8
#define USB_SPEED_MAX 9

/* List of the properties we are interested in */
typedef struct usb_device_props {
	int32_t vid;
	int32_t pid;
	uint32_t current_speed;
	uint32_t max_speed;
	uint32_t port;
	BOOLEAN is_USB;
	BOOLEAN is_SCSI;
	BOOLEAN is_CARD;
	BOOLEAN is_UASP;
	BOOLEAN is_VHD;
	BOOLEAN is_Removable;
} usb_device_props;

/* A partition of a Linux block device */
typedef struct {
	char Name[64];		/* e.g. "sda1" */
	int Number;		/* partition number */
	uint64_t Offset;
	uint64_t Size;
	int FSType;
	char Label[32];
} LINUX_PARTITION;

/* Current drive info. This mirrors the Windows RUFUS_DRIVE_INFO structure. */
typedef struct {
	uint64_t DiskSize;
	DWORD DeviceNumber;
	DWORD SectorsPerTrack;
	DWORD SectorSize;
	DWORD FirstDataSector;
	int MediaType;
	int PartitionStyle;
	int nPartitions;
	LINUX_PARTITION Partition[MAX_PARTITIONS];
	int FSType;
	char proposed_label[16];
	BOOL has_protective_mbr;
	BOOL has_mbr_uefi_marker;
	struct {
		ULONG Allowed;
		ULONG Default;
	} ClusterSize[FS_MAX];
} RUFUS_DRIVE_INFO;

/* Device enumeration */
extern RUFUS_DRIVE rufus_drive[MAX_DRIVES];
extern BOOL enable_HDDs, enable_VHDs, use_fake_units, list_non_usb_removable_drives;
extern RUFUS_DRIVE_INFO SelectedDrive;

void ClearDrives(void);
BOOL GetDevices(DWORD devnum);
BOOL GetUSBProperties(const char* device_path, usb_device_props* props);
BOOL IsRemovableDevice(const char* device_path);

/* Drive access */
char* GetPhysicalName(DWORD DriveIndex);
HANDLE GetPhysicalHandle(DWORD DriveIndex, BOOL bLockDrive, BOOL bWriteAccess, BOOL bWriteShare);
uint64_t GetDriveSize(DWORD DriveIndex);
BOOL GetDriveLabel(DWORD DriveIndex, char* letters, char** label, BOOL bSilent);
BOOL GetDrivePartitionData(DWORD DriveIndex, char* FileSystemName, DWORD FileSystemNameSize, BOOL bSilent);
char* GetLogicalName(DWORD DriveIndex, uint64_t PartitionOffset, BOOL bWriteAccess, BOOL bSilent);
BOOL UnmountVolume(HANDLE hDrive);
BOOL IsMediaPresent(DWORD DriveIndex);
BOOL AnalyzeMBR(HANDLE hPhysicalDrive, const char* TargetName, BOOL bSilent);
BOOL WritePartitionTable(HANDLE hDisk, int style, const LINUX_PARTITION* partitions, int nPartitions);
int linux_detect_partition_style(DWORD drive_index);
BOOL RefreshPartitionTable(HANDLE hDisk);
char* GetFsName(int fstype, uint64_t cluster_size);
const char* linux_fs_name(int fstype);

/* Formatting */
BOOL FormatPartition(DWORD DriveIndex, uint64_t PartitionOffset, DWORD UnitAllocationSize,
	USHORT FSType, LPCSTR Label, DWORD Flags);

/* Bootloader installation */
BOOL InstallBootloader(HANDLE hDisk, const char* device_path, const char* mount_point,
	const char* iso_path, BOOL bUse_bios, BOOL bUse_uefi);

/* Front-end hooks, implemented by the GUI and the CLI */
extern void (*linux_progress_callback)(int percent, const char* text);
extern void (*linux_log_callback)(const char* text);

/* Utilities */
extern const char* linux_resource_path(const char* name);
extern char* linux_find_data_file(const char* name);
BOOL read_sysfs_value(const char* path, char* buffer, size_t size);
