/*
 * Stub for the Windows header <virtdisk.h> - not available on Linux.
 *
 * Virtual disk support is Windows specific; only the constants that the
 * Rufus code refers to are provided here.
 */
#pragma once

#include "windows.h"

#define VIRTUAL_STORAGE_TYPE_DEVICE_ISO   1
#define VIRTUAL_STORAGE_TYPE_DEVICE_VHD   2
#define VIRTUAL_STORAGE_TYPE_DEVICE_VHDX  3
#define VIRTUAL_STORAGE_TYPE_DEVICE_VHDS  4

typedef struct _VIRTUAL_STORAGE_TYPE {
	ULONG DeviceId;
	ULONG VendorId;
} VIRTUAL_STORAGE_TYPE, *PVIRTUAL_STORAGE_TYPE;
