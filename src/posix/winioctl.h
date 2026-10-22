/*
 * Stub for the Windows header <winioctl.h> - not available on Linux.
 */
#pragma once

#include "windows.h"

/* Media types, as used by the Rufus drive information structure */
typedef enum _MEDIA_TYPE {
	Unknown = 0,
	F5Floppy = 1,
	F3Floppy = 2,
	F8Floppy = 3,
	FixedMedia = 4,
	RemovableMedia = 5,
	CDROM = 6,
	OtherMedia = 7,
} MEDIA_TYPE;

/* Disk geometry */
typedef struct _DISK_GEOMETRY {
	LARGE_INTEGER Cylinders;
	MEDIA_TYPE MediaType;
	DWORD TracksPerCylinder;
	DWORD SectorsPerTrack;
	DWORD BytesPerSector;
} DISK_GEOMETRY;

typedef struct _DISK_GEOMETRY_EX {
	DISK_GEOMETRY Geometry;
	LARGE_INTEGER DiskSize;
} DISK_GEOMETRY_EX;

typedef struct _PARTITION_INFORMATION {
	DWORD PartitionNumber;
	DWORD PartitionType;
	LARGE_INTEGER StartingOffset;
	LARGE_INTEGER PartitionLength;
} PARTITION_INFORMATION;

typedef struct _PARTITION_INFORMATION_EX {
	GUID PartitionType;
	LARGE_INTEGER StartingOffset;
	LARGE_INTEGER PartitionLength;
} PARTITION_INFORMATION_EX;

#define IOCTL_DISK_GET_DRIVE_GEOMETRY          0x00070000
#define IOCTL_DISK_GET_DRIVE_GEOMETRY_EX       0x000700A0
#define IOCTL_DISK_GET_PARTITION_INFO          0x00074004
#define IOCTL_DISK_GET_PARTITION_INFO_EX       0x00074050
#define IOCTL_DISK_SET_PARTITION_INFO_EX       0x00074054
#define IOCTL_DISK_CREATE_DISK                 0x0007C000
#define IOCTL_DISK_SET_DRIVE_LAYOUT_EX         0x00070050
