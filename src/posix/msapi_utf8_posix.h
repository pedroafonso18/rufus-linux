/*
 * MSAPI_UTF8: Common API calls using UTF-8 strings - POSIX implementation.
 *
 * On Linux, UTF-8 is the native encoding, so the whole "MSAPI_UTF8" concept
 * collapses to plain POSIX calls. This header provides the same API surface as
 * the Windows version of msapi_utf8.h so that the Rufus logic can be reused
 * verbatim.
 *
 * Copyright © 2010-2026 Pete Batard <pete@akeo.ie>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "windows.h"
#include "win_compat.h"

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define _LTEXT(txt) L##txt
#define LTEXT(txt) _LTEXT(txt)

#define wchar_to_utf8_no_alloc(wsrc, dest, dest_size) \
	WideCharToMultiByte(CP_UTF8, 0, wsrc, -1, dest, (int)(dest_size), NULL, NULL)
#define wchar_to_utf8_get_size(wsrc) \
	WideCharToMultiByte(CP_UTF8, 0, wsrc, -1, NULL, 0, NULL, NULL)
#define utf8_to_wchar_no_alloc(src, wdest, wdest_size) \
	MultiByteToWideChar(CP_UTF8, 0, src, -1, wdest, (int)(wdest_size))
#define utf8_to_wchar_get_size(src) \
	MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0)

#define Edit_ReplaceSelU(hCtrl, str) ((void)SendMessageLU(hCtrl, EM_REPLACESEL, (WPARAM)FALSE, str))
#define ComboBox_AddStringU(hCtrl, str) ((int)(DWORD)SendMessageLU(hCtrl, CB_ADDSTRING, (WPARAM)FALSE, str))
#define ComboBox_InsertStringU(hCtrl, index, str) ((int)(DWORD)SendMessageLU(hCtrl, CB_INSERTSTRING, (WPARAM)index, str))
#define ComboBox_GetTextU(hCtrl, str, max_str) GetWindowTextU(hCtrl, str, max_str)
#define GetSaveFileNameU(p) GetOpenSaveFileNameU(p, TRUE)
#define GetOpenFileNameU(p) GetOpenSaveFileNameU(p, FALSE)
#define ListView_SetItemTextU(hwndLV,i,iSubItem_,pszText_) do { \
	SNDMSG((hwndLV),LVM_SETITEMTEXTW,(WPARAM)(i),(LPARAM)(pszText_)); } while(0)

#define isasciiU(c) isascii((unsigned char)(c))
#define iscntrlU(c) iscntrl((unsigned char)(c))
#define isdigitU(c) isdigit((unsigned char)(c))
#define isspaceU(c) isspace((unsigned char)(c))
#define isxdigitU(c) isxdigit((unsigned char)(c))

#define sfree(p) do {if (p != NULL) {free((void*)(p)); p = NULL;}} while(0)
#define wconvert(p)     wchar_t* w ## p = utf8_to_wchar(p)
#define walloc(p, size) wchar_t* w ## p = (p == NULL)?NULL:(wchar_t*)calloc(size, sizeof(wchar_t))
#define wfree(p) sfree(w ## p)

/* String conversions -------------------------------------------------------- */

static __inline char* wchar_to_utf8(const wchar_t* wstr)
{
	int len;
	char* out;
	if (wstr == NULL)
		return NULL;
	len = win_wide_to_utf8((const wchar_t*)wstr, -1, NULL, 0);
	if (len < 0)
		return NULL;
	out = (char*)malloc((size_t)len + 1);
	if (out == NULL)
		return NULL;
	win_wide_to_utf8((const wchar_t*)wstr, -1, out, len + 1);
	return out;
}

static __inline wchar_t* utf8_to_wchar(const char* str)
{
	int len;
	wchar_t* out;
	if (str == NULL)
		return NULL;
	len = win_utf8_to_wide(str, -1, NULL, 0);
	if (len < 0)
		return NULL;
	out = (wchar_t*)calloc((size_t)len + 1, sizeof(wchar_t));
	if (out == NULL)
		return NULL;
	win_utf8_to_wide(str, -1, (wchar_t*)out, len + 1);
	return out;
}

static __inline char* wchar_len_to_utf8(const wchar_t* wstr, int wlen)
{
	int len;
	char* out;
	if (wstr == NULL)
		return NULL;
	len = win_wide_to_utf8((const wchar_t*)wstr, wlen, NULL, 0);
	if (len < 0)
		return NULL;
	out = (char*)malloc((size_t)len + 1);
	if (out == NULL)
		return NULL;
	win_wide_to_utf8((const wchar_t*)wstr, wlen, out, len + 1);
	return out;
}

/* Messages and dialogs ----------------------------------------------------- */

static __inline LRESULT SendMessageLU(HWND hWnd, UINT Msg, WPARAM wParam, const char* lParam)
{
	wchar_t* w = utf8_to_wchar(lParam);
	LRESULT r = SendMessageW(hWnd, Msg, wParam, (LPARAM)w);
	sfree(w);
	return r;
}

static __inline int MessageBoxU(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
	return MessageBoxA(hWnd, lpText, lpCaption, uType);
}

static __inline int MessageBoxExU(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType, WORD wLanguageId)
{
	(void)wLanguageId;
	return MessageBoxA(hWnd, lpText, lpCaption, uType);
}

static __inline int LoadStringU(HINSTANCE hInstance, UINT uID, LPSTR lpBuffer, int nBufferMax)
{
	(void)hInstance; (void)uID;
	if (lpBuffer != NULL && nBufferMax > 0)
		lpBuffer[0] = 0;
	return 0;
}

static __inline HMODULE LoadLibraryU(LPCSTR lpFileName) { return LoadLibraryA(lpFileName); }
static __inline HMODULE LoadLibraryExU(LPCSTR lpFileName, HANDLE hFile, DWORD dwFlags)
{
	return LoadLibraryExA(lpFileName, hFile, dwFlags);
}

static __inline int DrawTextU(HDC hDC, LPCSTR lpText, int nCount, LPRECT lpRect, UINT uFormat)
{
	(void)hDC; (void)lpText; (void)nCount; (void)lpRect; (void)uFormat;
	return 0;
}

static __inline int DrawTextExU(HDC hDC, LPCSTR lpchText, int nCount, LPRECT lpRect, UINT uFormat, void* lpDTParams)
{
	(void)hDC; (void)lpchText; (void)nCount; (void)lpRect; (void)uFormat; (void)lpDTParams;
	return 0;
}

static __inline int GetWindowTextU(HWND hWnd, char* lpString, int nMaxCount)
{
	return GetWindowTextA(hWnd, lpString, nMaxCount);
}

static __inline BOOL SetWindowTextU(HWND hWnd, const char* lpString)
{
	return SetWindowTextA(hWnd, lpString);
}

static __inline int GetWindowTextLengthU(HWND hWnd)
{
	return GetWindowTextLengthA(hWnd);
}

static __inline UINT GetDlgItemTextU(HWND hDlg, int nIDDlgItem, char* lpString, int nMaxCount)
{
	return (UINT)GetDlgItemTextA(hDlg, nIDDlgItem, lpString, nMaxCount);
}

static __inline BOOL SetDlgItemTextU(HWND hDlg, int nIDDlgItem, const char* lpString)
{
	return SetDlgItemTextA(hDlg, nIDDlgItem, lpString);
}

static __inline int ComboBox_GetLBTextU(HWND hCtrl, int index, char* lpString)
{
	(void)hCtrl; (void)index;
	if (lpString != NULL)
		lpString[0] = 0;
	return 0;
}

static __inline DWORD CharUpperBuffU(char* lpString, DWORD len)
{
	DWORD i;
	for (i = 0; i < len && lpString[i] != 0; i++)
		lpString[i] = (char)toupper((unsigned char)lpString[i]);
	return i;
}

static __inline BOOL InsertMenuU(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, const char* lpNewItem)
{
	(void)hMenu; (void)uPosition; (void)uFlags; (void)uIDNewItem; (void)lpNewItem;
	return TRUE;
}

static __inline BOOL GetTextExtentPointU(HDC hdc, const char* lpString, LPSIZE lpSize)
{
	return GetTextExtentPoint32(hdc, lpString, (int)strlen(lpString), lpSize);
}

static __inline DWORD FormatMessageU(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId,
	char* lpBuffer, DWORD nSize, void* lpArguments)
{
	(void)dwFlags; (void)lpSource; (void)dwMessageId; (void)dwLanguageId;
	(void)lpArguments;
	if (lpBuffer != NULL && nSize > 0)
		lpBuffer[0] = 0;
	return 0;
}

static __inline int SHGetPathFromIDListU(LPCITEMIDLIST pidl, char* pszPath)
{
	(void)pidl;
	if (pszPath != NULL)
		pszPath[0] = 0;
	return FALSE;
}

/* File system -------------------------------------------------------------- */

static __inline HANDLE CreateFileU(const char* lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
	return CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
		dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

static __inline HANDLE CreateFileRestrictedU(const char* lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
	return CreateFileU(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
		dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

static __inline BOOL CreateDirectoryU(const char* lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
	return CreateDirectoryA(lpPathName, lpSecurityAttributes);
}

static __inline BOOL CopyFileU(const char* lpExistingFileName, const char* lpNewFileName, BOOL bFailIfExists)
{
	return CopyFileA(lpExistingFileName, lpNewFileName, bFailIfExists);
}

static __inline BOOL DeleteFileU(const char* lpFileName)
{
	return DeleteFileA(lpFileName);
}

static __inline BOOL MoveFileU(const char* lpExistingFileName, const char* lpNewFileName)
{
	return MoveFileA(lpExistingFileName, lpNewFileName);
}

static __inline BOOL MoveFileExU(const char* lpExistingFileName, const char* lpNewFileName, DWORD dwFlags)
{
	return MoveFileExA(lpExistingFileName, lpNewFileName, dwFlags);
}

static __inline BOOL CreateSymbolicLinkU(const char* lpSymlinkFileName, const char* lpTargetFileName, DWORD dwFlags)
{
	return CreateSymbolicLinkA(lpSymlinkFileName, lpTargetFileName, dwFlags);
}

static __inline BOOL PathFileExistsU(char* szPath)
{
	struct stat st;
	if (szPath == NULL)
		return FALSE;
	return stat(szPath, &st) == 0;
}

static __inline int PathGetDriveNumberU(char* lpPath)
{
	(void)lpPath;
	return 0;
}

static __inline const char* PathFindFileNameU(const char* szPath)
{
	const char* p;
	if (szPath == NULL)
		return NULL;
	p = strrchr(szPath, '/');
	return (p != NULL) ? p + 1 : szPath;
}

static __inline char* PathCombineU(char* lpDest, char* lpDir, char* lpFile)
{
	size_t len;
	if (lpDest == NULL)
		return NULL;
	if (lpDir == NULL || lpDir[0] == 0) {
		snprintf(lpDest, MAX_PATH, "%s", lpFile ? lpFile : "");
		return lpDest;
	}
	if (lpFile == NULL || lpFile[0] == 0) {
		snprintf(lpDest, MAX_PATH, "%s", lpDir);
		return lpDest;
	}
	len = strlen(lpDir);
	if (len > 0 && lpDir[len - 1] == '/')
		snprintf(lpDest, MAX_PATH, "%s%s", lpDir, lpFile);
	else
		snprintf(lpDest, MAX_PATH, "%s/%s", lpDir, lpFile);
	return lpDest;
}

static __inline HANDLE FindFirstFileU(char* lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
	return FindFirstFileA(lpFileName, lpFindFileData);
}

static __inline BOOL FindNextFileU(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)
{
	return FindNextFileA(hFindFile, lpFindFileData);
}

static __inline DWORD GetAppDirectoryU(DWORD nBufferLength, char* lpBuffer)
{
	char path[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (len < 0)
		len = 0;
	path[len] = 0;
	{
		char* slash = strrchr(path, '/');
		if (slash != NULL)
			*slash = 0;
		else
			snprintf(path, sizeof(path), ".");
	}
	if (lpBuffer != NULL)
		snprintf(lpBuffer, nBufferLength, "%s", path);
	return (DWORD)strlen(path) + 1;
}

static __inline UINT GetSystemDirectoryU(char* lpBuffer, UINT uSize)
{
	return GetSystemDirectoryA(lpBuffer, uSize);
}

static __inline UINT GetSystemWindowsDirectoryU(char* lpBuffer, UINT uSize)
{
	return GetWindowsDirectoryA(lpBuffer, uSize);
}

static __inline DWORD GetTempPathU(DWORD nBufferLength, char* lpBuffer)
{
	return GetTempPathA(nBufferLength, lpBuffer);
}

static __inline DWORD GetTempFileNameU(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT uUnique, char* lpTempFileName)
{
	return GetTempFileNameA(lpPathName, lpPrefixString, uUnique, lpTempFileName);
}

static __inline DWORD GetTempDirNameU(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT uUnique, char* lpTempDirName)
{
	DWORD r = GetTempFileNameU(lpPathName, lpPrefixString, uUnique, lpTempDirName);
	char* dot;
	if (r == 0)
		return 0;
	dot = strrchr(lpTempDirName, '.');
	if (dot != NULL)
		*dot = 0;
	if (mkdir(lpTempDirName, 0700) < 0 && errno != EEXIST)
		return 0;
	return r;
}

static __inline DWORD GetModuleFileNameU(HMODULE hModule, char* lpFilename, DWORD nSize)
{
	return GetModuleFileNameA(hModule, lpFilename, nSize);
}

static __inline DWORD GetModuleFileNameExU(HANDLE hProcess, HMODULE hModule, char* lpFilename, DWORD nSize)
{
	(void)hProcess;
	return GetModuleFileNameA(hModule, lpFilename, nSize);
}

static __inline DWORD GetFinalPathNameByHandleU(HANDLE hFile, char* lpszFilePath, DWORD cchFilePath, DWORD dwFlags)
{
	int fd = win_handle_to_fd(hFile);
	char link[PATH_MAX];
	ssize_t len;
	(void)dwFlags;
	if (fd < 0)
		return 0;
	len = readlink("/proc/self/fd", link, 0);
	(void)len;
	{
		char procpath[64];
		snprintf(procpath, sizeof(procpath), "/proc/self/fd/%d", fd);
		len = readlink(procpath, lpszFilePath, cchFilePath - 1);
		if (len < 0)
			return 0;
		lpszFilePath[len] = 0;
	}
	return (DWORD)strlen(lpszFilePath);
}

static __inline DWORD GetFileVersionInfoSizeU(const char* lpFileName, LPDWORD lpdwHandle)
{
	return GetFileVersionInfoSizeA(lpFileName, lpdwHandle);
}

static __inline BOOL GetFileVersionInfoU(const char* lpFileName, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	return GetFileVersionInfoA(lpFileName, dwHandle, dwLen, lpData);
}

static __inline DWORD GetFullPathNameU(const char* lpFileName, DWORD nBufferLength, char* lpBuffer, char** lpFilePart)
{
	return GetFullPathNameA(lpFileName, nBufferLength, lpBuffer, lpFilePart);
}

static __inline DWORD GetFileAttributesU(const char* lpFileName)
{
	return GetFileAttributesA(lpFileName);
}

static __inline BOOL SetFileAttributesU(const char* lpFileName, DWORD dwFileAttributes)
{
	return SetFileAttributesA(lpFileName, dwFileAttributes);
}

/* Shell -------------------------------------------------------------------- */

static __inline BOOL SHGetSpecialFolderPathU(HWND hwnd, char* pszPath, int csidl, BOOL fCreate)
{
	const char* home = getenv("HOME");
	const char* xdg;
	(void)hwnd; (void)fCreate;
	switch (csidl) {
	case 0x1a:	/* CSIDL_PROFILE */
	case 0x26:	/* CSIDL_APPDATA */
		xdg = getenv("XDG_CONFIG_HOME");
		if (xdg != NULL && xdg[0] != 0)
			snprintf(pszPath, MAX_PATH, "%s", xdg);
		else if (home != NULL)
			snprintf(pszPath, MAX_PATH, "%s/.config", home);
		else
			snprintf(pszPath, MAX_PATH, "/tmp");
		break;
	case 0x24:	/* CSIDL_LOCAL_APPDATA */
		xdg = getenv("XDG_DATA_HOME");
		if (xdg != NULL && xdg[0] != 0)
			snprintf(pszPath, MAX_PATH, "%s", xdg);
		else if (home != NULL)
			snprintf(pszPath, MAX_PATH, "%s/.local/share", home);
		else
			snprintf(pszPath, MAX_PATH, "/tmp");
		break;
	case 0x30:	/* CSIDL_PROGRAM_FILES */
		snprintf(pszPath, MAX_PATH, "/usr/bin");
		break;
	default:
		if (home != NULL)
			snprintf(pszPath, MAX_PATH, "%s", home);
		else
			snprintf(pszPath, MAX_PATH, "/tmp");
		break;
	}
	return TRUE;
}

static __inline int SHCreateDirectoryExU(HWND hwnd, const char* pszPath, SECURITY_ATTRIBUTES *psa)
{
	char tmp[PATH_MAX];
	size_t i;
	(void)hwnd; (void)psa;
	snprintf(tmp, sizeof(tmp), "%s", pszPath);
	for (i = 1; tmp[i] != 0; i++) {
		if (tmp[i] == '/') {
			tmp[i] = 0;
			mkdir(tmp, 0755);
			tmp[i] = '/';
		}
	}
	if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
		return 0;
	return 1;
}

static __inline int SHDeleteDirectoryExU(HWND hwnd, const char* pszPath, DWORD fFlags)
{
	(void)hwnd; (void)fFlags;
	if (rmdir(pszPath) < 0)
		return 0;
	return 1;
}

static __inline BOOL ShellExecuteExU(SHELLEXECUTEINFOA* lpExecInfo)
{
	return ShellExecuteExA(lpExecInfo);
}

static __inline BOOL CreateProcessU(const char* lpApplicationName, const char* lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, const char* lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
	char* cmd = (lpCommandLine != NULL) ? strdup(lpCommandLine) : NULL;
	BOOL r;
	if (cmd == NULL)
		return FALSE;
	r = CreateProcessA(lpApplicationName, cmd, lpProcessAttributes, lpThreadAttributes,
		bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory,
		lpStartupInfo, lpProcessInformation);
	sfree(cmd);
	return r;
}

/*
 * File open/save dialogs. On Linux the front-end can install a handler
 * through win_set_file_dialog(); when none is installed, the call fails
 * gracefully (which is what the CLI wants).
 */
static __inline int GetOpenSaveFileNameU(LPOPENFILENAMEA lpofn, BOOL save)
{
	char buffer[MAX_PATH * 4];
	if (lpofn == NULL)
		return FALSE;
	buffer[0] = 0;
	if (lpofn->lpstrFile != NULL && lpofn->nMaxFile > 0)
		snprintf(buffer, sizeof(buffer), "%s", lpofn->lpstrFile);
	if (!win_file_dialog_impl(save, lpofn->lpstrTitle, lpofn->lpstrFilter, buffer, (DWORD)sizeof(buffer)))
		return FALSE;
	if (lpofn->lpstrFile != NULL)
		snprintf(lpofn->lpstrFile, lpofn->nMaxFile, "%s", buffer);
	if (lpofn->lpstrFileTitle != NULL)
		snprintf(lpofn->lpstrFileTitle, 260, "%s", buffer);
	return TRUE;
}

/* Privilege / security ----------------------------------------------------- */

static __inline DWORD GetNamedSecurityInfoU(const char* lpObjectName, DWORD ObjectType,
	DWORD dwSecurityInfo, DWORD *ppsidOwner, DWORD *ppsidGroup, DWORD *ppDacl, DWORD *ppSacl, DWORD *ppSecurityDescriptor)
{
	(void)lpObjectName; (void)ObjectType; (void)dwSecurityInfo;
	(void)ppsidOwner; (void)ppsidGroup; (void)ppDacl; (void)ppSacl; (void)ppSecurityDescriptor;
	return ERROR_SUCCESS;
}

static __inline DWORD SetNamedSecurityInfoU(const char* lpObjectName, DWORD ObjectType,
	DWORD dwSecurityInfo, DWORD psidOwner, DWORD psidGroup, DWORD pDacl, DWORD pSacl, DWORD psSecurityDescriptor)
{
	(void)lpObjectName; (void)ObjectType; (void)dwSecurityInfo; (void)psidOwner; (void)psidGroup;
	(void)pDacl; (void)pSacl; (void)psSecurityDescriptor;
	return ERROR_SUCCESS;
}

/* SetupAPI ----------------------------------------------------------------- */

static __inline BOOL SetupDiGetDeviceRegistryPropertyU(void* DeviceInfoSet, void* DeviceInfoData,
	DWORD Property, void* pdwPropertyRegDataType, void* pbPropertyBuffer, DWORD cbPropertyBuffer,
	DWORD* pcbRequiredSize)
{
	(void)DeviceInfoSet; (void)DeviceInfoData; (void)Property; (void)pdwPropertyRegDataType;
	(void)pbPropertyBuffer; (void)cbPropertyBuffer;
	if (pcbRequiredSize != NULL)
		*pcbRequiredSize = 0;
	return FALSE;
}

static __inline BOOL SetupCopyOEMInfU(const char* SourceInfFileName, const char* OEMSourceMediaLocation,
	DWORD OEMSourceMediaType, DWORD dwFlags, void* pPostInstallInfSection, void* pPostInstallInfSectionList,
	void* pbCopyResult)
{
	(void)SourceInfFileName; (void)OEMSourceMediaLocation; (void)OEMSourceMediaType; (void)dwFlags;
	(void)pPostInstallInfSection; (void)pPostInstallInfSectionList; (void)pbCopyResult;
	return FALSE;
}

static __inline BOOL UpdateDriverForPlugAndPlayDevicesU(HWND hwndParent, const char* HardwareId,
	const char* FullInfPath, DWORD InstallFlags, BOOL *bRebootRequired)
{
	(void)hwndParent; (void)HardwareId; (void)FullInfPath; (void)InstallFlags;
	if (bRebootRequired != NULL)
		*bRebootRequired = FALSE;
	return FALSE;
}

/* CRT-style helpers -------------------------------------------------------- */

static __inline int _chdirU(const char *dirname) { return chdir(dirname); }

static __inline FILE* fopenU(const char* filename, const char* mode)
{
	return fopen(filename, mode);
}

static __inline int _openU(const char *filename, int oflag, int pmode)
{
	return open(filename, oflag | O_CLOEXEC, pmode);
}

static __inline int _unlinkU(const char* path) { return unlink(path); }

static __inline int _stat64U(const char *path, struct stat *buffer) { return stat(path, buffer); }

static __inline int _accessU(const char* path, int mode) { return access(path, mode); }

static __inline const char* _filenameU(const char* path) { return PathFindFileNameU(path); }

static __inline uint64_t _filesizeU(const char* path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return 0;
	return (uint64_t)st.st_size;
}

static __inline char* getenvU(const char* varname) { return getenv(varname); }

static __inline int _mkdirU(const char* dirname) { return mkdir(dirname, 0755); }

static __inline int _mkdirExU(const char* dirname)
{
	char tmp[PATH_MAX];
	size_t i;
	snprintf(tmp, sizeof(tmp), "%s", dirname);
	for (i = 1; tmp[i] != 0; i++) {
		if (tmp[i] == '/') {
			tmp[i] = 0;
			mkdir(tmp, 0755);
			tmp[i] = '/';
		}
	}
	return mkdir(tmp, 0755);
}

static __inline int _rmdirU(const char* dirname) { return rmdir(dirname); }

static __inline BOOL GetUserNameU(LPSTR lpBuffer, LPDWORD pcbBuffer)
{
	return GetUserNameA(lpBuffer, pcbBuffer);
}

static __inline BOOL GetVolumeInformationU(LPCSTR lpRootPathName, LPSTR lpVolumeNameBuffer,
	DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength,
	LPDWORD lpFileSystemFlags, LPSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize)
{
	return GetVolumeInformationA(lpRootPathName, lpVolumeNameBuffer, nVolumeNameSize,
		lpVolumeSerialNumber, lpMaximumComponentLength, lpFileSystemFlags,
		lpFileSystemNameBuffer, nFileSystemNameSize);
}

#ifdef __cplusplus
}
#endif
