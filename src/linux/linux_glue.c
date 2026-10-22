/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - platform glue.
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
 * This file provides the handful of globals and entry points that the
 * platform independent Rufus code expects to find, but that have no
 * equivalent on Linux.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linux_api.h"
#include "linux_headers.h"
#include "posix/win_compat.h"

/* The Syslinux library needs to know the sector geometry of the volume */
uint32_t LIBFAT_SECTOR_SHIFT = 9;
uint32_t LIBFAT_SECTOR_SIZE = 512;
uint32_t LIBFAT_SECTOR_MASK = 511;

/* Options that only make sense on Windows */
BOOL preserve_timestamps = FALSE;
BOOL enable_ntfs_compression = FALSE;
HANDLE format_thread = NULL;
BOOL is_darkmode_enabled = FALSE;
int default_thread_priority = 0;

/* ------------------------------------------------------------------------ */
/* Progress reporting                                                        */
/* ------------------------------------------------------------------------ */

static void linux_progress(int percent, const char* text)
{
	const win_dispatch* dispatch = win_get_dispatch();
	if ((dispatch != NULL) && (dispatch->progress != NULL))
		dispatch->progress(dispatch->context, percent, text);
	else if (linux_progress_callback != NULL)
		linux_progress_callback(percent, text);
}


void UpdateProgress(int op, float percent)
{
	(void)op;
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	linux_progress((int)percent, NULL);
}

void _UpdateProgressWithInfo(int op, int msg, uint64_t processed, uint64_t total, BOOL force)
{
	int percent;

	(void)msg;
	(void)force;
	if (total == 0)
		return;
	percent = (int)((processed * 100) / total);
	if (percent > 100)
		percent = 100;
	UpdateProgress(op, (float)percent);
}

/* ------------------------------------------------------------------------ */
/* Windows only features                                                     */
/* ------------------------------------------------------------------------ */

/*
 * Optical disc handling is not available on Linux.
 */
BOOL GetOpticalMedia(IMG_SAVE* img_save)
{
	(void)img_save;
	return FALSE;
}

/*
 * WIM splitting is not available on Linux.
 */
BOOL WimSplitFile(const char* src, const char* dst)
{
	(void)src; (void)dst;
	return FALSE;
}

/*
 * Certificate inspection requires the Windows certificate store.
 */
int GetIssuerCertificateInfo(uint8_t* cert, cert_info_t* info)
{
	(void)cert;
	if (info != NULL)
		memset(info, 0, sizeof(cert_info_t));
	return 0;
}

uint64_t GetSignatureTimeStamp(const char* path)
{
	(void)path;
	return 0;
}

LONG ValidateSignature(HWND hDlg, const char* path)
{
	(void)hDlg; (void)path;
	return ERROR_SUCCESS;
}

/*
 * The UI control enabling and progress initialization are handled by the
 * front-end.
 */
void EnableControls(BOOL enable, BOOL remove_checkboxes)
{
	(void)enable; (void)remove_checkboxes;
}

void InitProgress(BOOL bOnlyFormat)
{
	(void)bOnlyFormat;
}

/* ------------------------------------------------------------------------ */
/* User interface stubs                                                      */
/*                                                                            */
/* The Linux front-ends provide their own user interface, so the Win32       */
/* dialog helpers are reduced to no-ops.                                     */
/* ------------------------------------------------------------------------ */

void CenterDialog(HWND hDlg, HWND hParent) { (void)hDlg; (void)hParent; }
void ResizeMoveCtrl(HWND hDlg, HWND hCtrl, int dx, int dy, int dw, int dh, float scale)
{
	(void)hDlg; (void)hCtrl; (void)dx; (void)dy; (void)dw; (void)dh; (void)scale;
}
void ResizeButtonHeight(HWND hDlg, int id) { (void)hDlg; (void)id; }
void SetDarkModeForChild(HWND hDlg) { (void)hDlg; }
void SetDarkTitleBar(HWND hDlg) { (void)hDlg; }
void SubclassCtlColor(HWND hDlg, int id) { (void)hDlg; (void)id; }
INT_PTR MyDialogBox(HINSTANCE hInstance, int Dialog_ID, HWND hWndParent, DLGPROC lpDialogFunc)
{
	(void)hInstance; (void)Dialog_ID; (void)hWndParent; (void)lpDialogFunc;
	return 0;
}
HWND MyCreateDialog(HINSTANCE hInstance, int Dialog_ID, HWND hWndParent, DLGPROC lpDialogFunc)
{
	(void)hInstance; (void)Dialog_ID; (void)hWndParent; (void)lpDialogFunc;
	return NULL;
}

/*
 * The Syslinux version detection requires the PE resources of the Windows
 * build of ldlinux.sys.
 */
uint16_t GetSyslinuxVersion(char* buf, size_t buf_size, char** ext)
{
	if (buf != NULL && buf_size > 0)
		buf[0] = 0;
	if (ext != NULL)
		*ext = NULL;
	return 0;
}

/*
 * The architecture of a PE image.
 */
enum ArchType MachineToArch(WORD machine)
{
	switch (machine) {
	case 0x014C: return ARCH_X86_32;
	case 0x8664: return ARCH_X86_64;
	case 0x01C0: return ARCH_ARM_32;
	case 0xAA64: return ARCH_ARM_64;
	case 0x5064: return ARCH_RISCV_64;
	case 0x6264: return ARCH_LOONGARCH_64;
	default: return ARCH_UNKNOWN;
	}
}

/*
 * The Secure Boot revocation database is Windows specific.
 */
BOOL UseLocalDbx(void)
{
	return FALSE;
}

/*
 * A file selection dialog. The Linux front-end installs its own handler
 * through win_set_file_dialog().
 */
char* FileDialog(BOOL save, char* path, const ext_t* ext, UINT* selected_ext)
{
	char buffer[MAX_PATH * 4];
	const char* filter = NULL;

	(void)ext;
	if (selected_ext != NULL)
		*selected_ext = 0;
	buffer[0] = 0;
	if (path != NULL)
		snprintf(buffer, sizeof(buffer), "%s", path);
	if (!win_file_dialog_impl(save, NULL, filter, buffer, (DWORD)sizeof(buffer)))
		return NULL;
	return strdup(buffer);
}

/* Secure Boot related strings */
char* sb_active_txt = NULL;
char* sb_revoked_txt = NULL;
char* sbat_level_txt = NULL;

/* Advanced mode */
BOOL expert_mode = FALSE;
