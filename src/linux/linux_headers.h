/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - replacements for the Windows platform headers.
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
 * The Windows build of Rufus relies on drive.h, dev.h, ui.h, vhd.h and
 * format.h. On Linux, those headers are replaced by the declarations below,
 * which expose the same functionality through a POSIX implementation.
 */

#pragma once

#include <stdint.h>
#include "rufus.h"
#include "linux_api.h"

/* --- drive.h ------------------------------------------------------------ */

/* Format flags (same values as the Windows ones) */
#define FP_FORCE                            0x00000001
#define FP_QUICK                            0x00000002
#define FP_COMPRESSION                      0x00000004
#define FP_DUPLICATE_METADATA               0x00000008
#define FP_LARGE_FAT32                      0x00010000
#define FP_NO_BOOT                          0x00020000
#define FP_CREATE_PERSISTENCE_CONF          0x00040000
#define FP_NO_PROGRESS                      0x00080000

#define PARTITION_STYLE_MBR_LINUX PARTITION_STYLE_MBR

/* --- dev.h -------------------------------------------------------------- */

/* --- ui.h --------------------------------------------------------------- */
/* The Linux front-ends implement their own progress reporting; the shared
 * Rufus logic only needs the update functions declared in rufus.h. */
extern void EnableControls(BOOL enable, BOOL remove_checkboxes);
extern void InitProgress(BOOL bOnlyFormat);

/* Optical disc handling is Windows specific */
extern BOOL GetOpticalMedia(IMG_SAVE* img_save);

/* WIM splitting is Windows specific */
extern BOOL WimSplitFile(const char* src, const char* dst);

/* --- vhd.h -------------------------------------------------------------- */
/* VHD/VHDX support is Windows specific and is not available on Linux. */
static __inline uint32_t GetWimVersion(const char* image)
{
	(void)image;
	return 0;
}

/* Unlock and close a volume handle (no-op on Linux) */
#define safe_unlockclose(h) do { if ((h != INVALID_HANDLE_VALUE) && (h != NULL)) { CloseHandle(h); h = INVALID_HANDLE_VALUE; } } while(0)

static __inline char* VhdMountImageAndGetSize(const char* image_path, uint64_t* size)
{
	(void)image_path; (void)size;
	return NULL;
}

static __inline void VhdUnmountImage(void)
{
}

/* --- format.h ----------------------------------------------------------- */

/* Ext file system formatting (implemented on top of the vendored e2fsprogs) */
BOOL FormatExtFs(DWORD DriveIndex, uint64_t PartitionOffset, DWORD BlockSize, LPCSTR FSName,
	LPCSTR Label, DWORD Flags);
const char* GetExtFsLabel(DWORD DriveIndex, uint64_t PartitionOffset);
char* GetExtPartitionName(DWORD DriveIndex, uint64_t PartitionOffset);
