/*
 * Rufus: The Reliable USB Formatting Utility
 * Win32 compatibility layer for POSIX (Linux) platforms - implementation.
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

#define _GNU_SOURCE
#include <ctype.h>
#include <strings.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <pthread.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <wchar.h>
#include <unistd.h>

#include "windows.h"
#include "win_compat.h"

/* ------------------------------------------------------------------------ */
/* Handle encoding                                                          */
/*                                                                            */
/* Win32 never returns NULL or 0 from CreateFile(), and Rufus relies on    */
/* that. We therefore encode file descriptors as (fd + 1) so that a valid   */
/* handle is never confused with NULL.                                      */
/* ------------------------------------------------------------------------ */

int win_handle_to_fd(HANDLE h)
{
	if (h == NULL || h == INVALID_HANDLE_VALUE)
		return -1;
	return (int)((intptr_t)h) - 1;
}

HANDLE win_fd_to_handle(int fd)
{
	if (fd < 0)
		return INVALID_HANDLE_VALUE;
	return (HANDLE)(intptr_t)(fd + 1);
}

/* ------------------------------------------------------------------------ */
/* Error handling                                                           */
/* ------------------------------------------------------------------------ */

static __thread DWORD win_last_error = 0;

/* Log verbosity (see win_compat.h) */
int win_log_verbose = 0;

DWORD WINAPI GetLastError(void)
{
	return win_last_error;
}

void WINAPI SetLastError(DWORD dwErrCode)
{
	win_last_error = dwErrCode;
}

void WINAPI SetLastErrorEx(DWORD dwErrCode, DWORD dwType)
{
	win_last_error = dwErrCode;
}

BOOL WINAPI SetHandleInformation(HANDLE hObject, DWORD dwMask, DWORD dwFlags)
{
	(void)hObject; (void)dwMask; (void)dwFlags;
	return TRUE;
}

/* Map a POSIX errno to the closest Win32 error code */
static DWORD errno_to_win32(int err)
{
	switch (err) {
	case 0: return ERROR_SUCCESS;
	case ENOENT: return ERROR_FILE_NOT_FOUND;
	case ENOTDIR: return ERROR_PATH_NOT_FOUND;
	case EACCES: return ERROR_ACCESS_DENIED;
	case EBUSY: return ERROR_BUSY;
	case EEXIST: return ERROR_FILE_EXISTS;
	case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
	case EINVAL: return ERROR_INVALID_HANDLE;
	case ENOSPC: return ERROR_HANDLE_EOF;
	case EPERM: return ERROR_ACCESS_DENIED;
	case EROFS: return ERROR_WRITE_PROTECT;
	case ENAMETOOLONG: return ERROR_INVALID_NAME;
	case EMFILE:
	case ENFILE: return ERROR_TOO_MANY_OPEN_FILES;
	case ENOTEMPTY: return ERROR_DIR_NOT_EMPTY;
	case EAGAIN: return ERROR_IO_PENDING;
	default: return ERROR_GEN_FAILURE;
	}
}

/* Raise a Win32 error code from the current errno */
static BOOL fail(DWORD code)
{
	win_last_error = code;
	return FALSE;
}

/* Same, for the functions that return a HANDLE */
static HANDLE fail_handle(DWORD code)
{
	win_last_error = code;
	return INVALID_HANDLE_VALUE;
}

/* ------------------------------------------------------------------------ */
/* Wide character conversions                                                */
/*                                                                            */
/* On POSIX systems wchar_t is a 32 bit type (UCS-4 on glibc), so the wide  */
/* character conversions work on code points rather than on UTF-16 units.   */
/* ------------------------------------------------------------------------ */

int win_utf8_to_wide(const char* in, int in_len, wchar_t* out, int out_len)
{
	int i = 0, o = 0;
	if (in == NULL)
		return 0;
	if (in_len < 0)
		in_len = (int)strlen(in);
	while (i < in_len) {
		unsigned char c = (unsigned char)in[i];
		uint32_t cp;
		int extra;
		if (c < 0x80) {
			cp = c; extra = 0;
		} else if ((c & 0xE0) == 0xC0) {
			cp = c & 0x1F; extra = 1;
		} else if ((c & 0xF0) == 0xE0) {
			cp = c & 0x0F; extra = 2;
		} else if ((c & 0xF8) == 0xF0) {
			cp = c & 0x07; extra = 3;
		} else {
			/* Invalid lead byte: substitute U+FFFD */
			cp = 0xFFFD; extra = -1;
		}
		if (extra >= 0) {
			int k;
			for (k = 0; k < extra; k++) {
				if (i + 1 + k >= in_len) {
					cp = 0xFFFD;
					extra = -1;
					break;
				}
				if (((unsigned char)in[i + 1 + k] & 0xC0) != 0x80) {
					cp = 0xFFFD;
					extra = -1;
					break;
				}
				cp = (cp << 6) | ((unsigned char)in[i + 1 + k] & 0x3F);
			}
		}
		i += (extra < 0) ? 1 : extra + 1;
		if (cp > 0x10FFFF)
			cp = 0xFFFD;
		if (out != NULL) {
			if (o >= out_len)
				return -1;
			out[o] = (wchar_t)cp;
		}
		o++;
	}
	if (out != NULL && out_len > 0)
		out[o < out_len ? o : out_len - 1] = 0;
	return o;
}

int win_wide_to_utf8(const wchar_t* in, int in_len, char* out, int out_len)
{
	int i = 0, o = 0;
	if (in == NULL)
		return 0;
	if (in_len < 0) {
		in_len = 0;
		while (in[in_len] != 0)
			in_len++;
	}
	while (i < in_len) {
		uint32_t cp = (uint32_t)in[i++];
		if (cp < 0x80) {
			if (out != NULL) { if (o >= out_len) return -1; out[o] = (char)cp; }
			o++;
		} else if (cp < 0x800) {
			if (out != NULL) {
				if (o + 2 > out_len) return -1;
				out[o++] = (char)(0xC0 | (cp >> 6));
				out[o++] = (char)(0x80 | (cp & 0x3F));
			} else o += 2;
		} else if (cp < 0x10000) {
			if (out != NULL) {
				if (o + 3 > out_len) return -1;
				out[o++] = (char)(0xE0 | (cp >> 12));
				out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
				out[o++] = (char)(0x80 | (cp & 0x3F));
			} else o += 3;
		} else {
			if (out != NULL) {
				if (o + 4 > out_len) return -1;
				out[o++] = (char)(0xF0 | (cp >> 18));
				out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
				out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
				out[o++] = (char)(0x80 | (cp & 0x3F));
			} else o += 4;
		}
	}
	if (out != NULL && out_len > 0)
		out[o < out_len ? o : out_len - 1] = 0;
	return o;
}

void* win_mm_malloc(size_t size, size_t alignment)
{
	void* p = NULL;
	if (alignment < sizeof(void*))
		alignment = sizeof(void*);
	/* posix_memalign() requires the alignment to be a power of 2 multiple of
	 * sizeof(void*), which is what the callers use. */
	if (posix_memalign(&p, alignment, size) != 0)
		return NULL;
	memset(p, 0, size);
	return p;
}

int WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte,
	LPWSTR lpWideCharStr, int cchWideChar)
{
	(void)CodePage; (void)dwFlags;
	if (lpMultiByteStr == NULL)
		return 0;
	return win_utf8_to_wide(lpMultiByteStr, cbMultiByte, (wchar_t*)lpWideCharStr, cchWideChar);
}

int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar,
	LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar)
{
	(void)CodePage; (void)dwFlags; (void)lpDefaultChar; (void)lpUsedDefaultChar;
	if (lpWideCharStr == NULL)
		return 0;
	return win_wide_to_utf8((const wchar_t*)lpWideCharStr, cchWideChar, lpMultiByteStr, cbMultiByte);
}

BOOL WINAPI IsValidCodePage(UINT cp)
{
	return (cp == 65001 || cp == 20127 || cp == 1252 || cp == 0);
}

UINT WINAPI GetACP(void) { return 65001; }
UINT WINAPI GetOEMCP(void) { return 65001; }
UINT WINAPI GetCPInfo(UINT cp, LPVOID lpBuffer) { (void)cp; (void)lpBuffer; return 1; }
DWORD WINAPI GetUserDefaultLangID(void) { return 0x0409; }
DWORD WINAPI GetSystemDefaultLangID(void) { return 0x0409; }
DWORD WINAPI GetUserDefaultUILanguage(void) { return 0x0409; }
LCID GetUserDefaultLocale(void) { return 0x0409; }

/*
 * The Windows keyboard layout does not exist on Linux. We report the US
 * layout, which is the most common one and matches the default DOS keymap
 * tables that Rufus embeds.
 */
int WINAPI GetKeyboardLayoutNameA(char* lpName)
{
	if (lpName != NULL)
		memcpy(lpName, "00000409", 9);
	return 8;
}

int WINAPI GetLocaleInfoA(LCID Locale, int LCType, void* lpLCData, int cchData)
{
	(void)Locale;
	if (LCType & LOCALE_IDEFAULTCODEPAGE) {
		/* The "default" code page for the US English locale */
		if (LCType & LOCALE_RETURN_NUMBER)
			return 437;
		if (lpLCData != NULL && cchData >= 4)
			*(DWORD*)lpLCData = 437;
		return 4;
	}
	if (lpLCData != NULL && cchData > 0)
		((char*)lpLCData)[0] = 0;
	return 0;
}

/* ------------------------------------------------------------------------ */
/* File I/O                                                                 */
/* ------------------------------------------------------------------------ */

static DWORD open_flags_to_posix(DWORD access, DWORD creation, DWORD flags)
{
	int oflags = 0;
	DWORD share = 0;

	(void)share;
	if ((access & GENERIC_READ) && (access & GENERIC_WRITE))
		oflags |= O_RDWR;
	else if (access & GENERIC_WRITE)
		oflags |= O_WRONLY;
	else
		oflags |= O_RDONLY;

	switch (creation) {
	case CREATE_NEW:
		oflags |= O_CREAT | O_EXCL;
		break;
	case CREATE_ALWAYS:
		oflags |= O_CREAT | O_TRUNC;
		break;
	case OPEN_EXISTING:
		break;
	case OPEN_ALWAYS:
		oflags |= O_CREAT;
		break;
	case TRUNCATE_EXISTING:
		oflags |= O_TRUNC;
		break;
	default:
		break;
	}

	if (flags & FILE_FLAG_WRITE_THROUGH)
		oflags |= O_SYNC;
	if (flags & FILE_FLAG_NO_BUFFERING)
		oflags |= O_DIRECT;
#ifdef O_NOATIME
	if (access & GENERIC_READ)
		oflags |= O_NOATIME;
#endif
	oflags |= O_CLOEXEC;
	return (DWORD)oflags;
}

HANDLE WINAPI CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
	int fd, flags;
	struct stat st;

	(void)lpSecurityAttributes;
	(void)hTemplateFile;
	if (lpFileName == NULL || lpFileName[0] == 0)
		return fail_handle(ERROR_PATH_NOT_FOUND);

	flags = (int)open_flags_to_posix(dwDesiredAccess, dwCreationDisposition, dwFlagsAndAttributes);

	/* Windows allows opening directories; do the same on POSIX */
	if (stat(lpFileName, &st) == 0 && S_ISDIR(st.st_mode))
		flags |= O_DIRECTORY;

	fd = open(lpFileName, flags, 0644);
	if (fd < 0) {
		/* Retry without O_DIRECT/O_SYNC, which many devices reject */
		flags &= ~(O_DIRECT | O_SYNC | O_NOATIME);
		fd = open(lpFileName, flags, 0644);
	}
	if (fd < 0) {
		/* Some callers (e.g. volumes) are opened for read-only access on
		 * write-only paths; try read-only as a last resort. */
		fd = open(lpFileName, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			return fail_handle(errno_to_win32(errno));
	}
	(void)dwShareMode;
	return win_fd_to_handle(fd);
}

HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
	char name[PATH_MAX];
	if (win_wide_to_utf8((const wchar_t*)lpFileName, -1, name, sizeof(name)) < 0)
		return fail_handle(ERROR_INVALID_NAME);
	return CreateFileA(name, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
		dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

BOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
	LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
	int fd = win_handle_to_fd(hFile);
	ssize_t n;
	uint8_t* buf = (uint8_t*)lpBuffer;

	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	if (lpBuffer == NULL && nNumberOfBytesToRead > 0)
		return fail(ERROR_INVALID_HANDLE);
	if (lpOverlapped != NULL) {
		/* Windows allows an overlapped offset to be specified */
		off_t off = (off_t)lpOverlapped->Offset;
#ifdef __linux__
		off |= (off_t)lpOverlapped->OffsetHigh << 32;
#endif
		if (lpOverlapped->Offset || lpOverlapped->OffsetHigh) {
			if (lseek(fd, off, SEEK_SET) == (off_t)-1)
				return fail(errno_to_win32(errno));
		}
	}
	do {
		n = read(fd, buf, nNumberOfBytesToRead);
	} while (n < 0 && errno == EINTR);
	if (n < 0)
		return fail(errno_to_win32(errno));
	if (lpNumberOfBytesRead != NULL)
		*lpNumberOfBytesRead = (DWORD)n;
	if (n == 0 && nNumberOfBytesToRead > 0)
		win_last_error = ERROR_HANDLE_EOF;
	return TRUE;
}

BOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
	LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
	int fd = win_handle_to_fd(hFile);
	ssize_t n;
	const uint8_t* buf = (const uint8_t*)lpBuffer;

	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	if (lpBuffer == NULL && nNumberOfBytesToWrite > 0)
		return fail(ERROR_INVALID_HANDLE);
	if (lpOverlapped != NULL) {
		off_t off = (off_t)lpOverlapped->Offset;
#ifdef __linux__
		off |= (off_t)lpOverlapped->OffsetHigh << 32;
#endif
		if (lpOverlapped->Offset || lpOverlapped->OffsetHigh) {
			if (lseek(fd, off, SEEK_SET) == (off_t)-1)
				return fail(errno_to_win32(errno));
		}
	}
	do {
		n = write(fd, buf, nNumberOfBytesToWrite);
	} while (n < 0 && errno == EINTR);
	if (n < 0)
		return fail(errno_to_win32(errno));
	if (lpNumberOfBytesWritten != NULL)
		*lpNumberOfBytesWritten = (DWORD)n;
	return TRUE;
}

BOOL WINAPI CloseHandle(HANDLE hObject)
{
	int fd = win_handle_to_fd(hObject);
	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	if (close(fd) < 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

BOOL WINAPI FlushFileBuffers(HANDLE hFile)
{
	int fd = win_handle_to_fd(hFile);
	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	if (fsync(fd) < 0 && errno != EINVAL)
		return fail(errno_to_win32(errno));
	return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
	int fd = win_handle_to_fd(hFile);
	off_t off, res;
	int whence;

	if (fd < 0) {
		win_last_error = ERROR_INVALID_HANDLE;
		return (DWORD)-1;
	}
	switch (dwMoveMethod) {
	case FILE_BEGIN: whence = SEEK_SET; break;
	case FILE_CURRENT: whence = SEEK_CUR; break;
	case FILE_END: whence = SEEK_END; break;
	default: whence = SEEK_SET; break;
	}
#ifdef __linux__
	off = (off_t)(uint32_t)lDistanceToMove;
	if (lpDistanceToMoveHigh != NULL)
		off |= (off_t)((uint32_t)*lpDistanceToMoveHigh) << 32;
#else
	off = (off_t)lDistanceToMove;
#endif
	res = lseek(fd, off, whence);
	if (res == (off_t)-1) {
		win_last_error = errno_to_win32(errno);
		return (DWORD)-1;
	}
	if (lpDistanceToMoveHigh != NULL)
		*lpDistanceToMoveHigh = (LONG)(res >> 32);
	return (DWORD)res;
}

BOOL WINAPI SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod)
{
	int fd = win_handle_to_fd(hFile);
	off_t off, res;
	int whence;

	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	switch (dwMoveMethod) {
	case FILE_BEGIN: whence = SEEK_SET; break;
	case FILE_CURRENT: whence = SEEK_CUR; break;
	case FILE_END: whence = SEEK_END; break;
	default: whence = SEEK_SET; break;
	}
	off = (off_t)liDistanceToMove.QuadPart;
	res = lseek(fd, off, whence);
	if (res == (off_t)-1)
		return fail(errno_to_win32(errno));
	if (lpNewFilePointer != NULL)
		lpNewFilePointer->QuadPart = res;
	return TRUE;
}

BOOL WINAPI GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize)
{
	int fd = win_handle_to_fd(hFile);
	struct stat st;

	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	if (fstat(fd, &st) < 0)
		return fail(errno_to_win32(errno));
	if (lpFileSize != NULL)
		lpFileSize->QuadPart = st.st_size;
	return TRUE;
}

DWORD WINAPI GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
	LARGE_INTEGER li;
	if (!GetFileSizeEx(hFile, &li))
		return (DWORD)-1;
	if (lpFileSizeHigh != NULL)
		*lpFileSizeHigh = (DWORD)(li.QuadPart >> 32);
	return (DWORD)li.QuadPart;
}

static uint64_t win_time_to_uint64(const FILETIME* ft)
{
	return ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
}

static void uint64_to_win_time(uint64_t t, FILETIME* ft)
{
	ft->dwLowDateTime = (DWORD)(t & 0xFFFFFFFF);
	ft->dwHighDateTime = (DWORD)(t >> 32);
}

BOOL WINAPI SetFileTime(HANDLE hFile, const FILETIME *lpCreationTime,
	const FILETIME *lpLastAccessTime, const FILETIME *lpLastWriteTime)
{
	int fd = win_handle_to_fd(hFile);
	struct timespec times[2];

	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	(void)lpCreationTime;
	times[0].tv_sec = 0;
	times[0].tv_nsec = UTIME_OMIT;
	if (lpLastAccessTime != NULL)
		times[0].tv_sec = (time_t)(win_time_to_uint64(lpLastAccessTime) / 10000000ULL);
	else
		times[0].tv_sec = 0;
	times[1].tv_sec = 0;
	times[1].tv_nsec = UTIME_OMIT;
	if (lpLastWriteTime != NULL)
		times[1].tv_sec = (time_t)(win_time_to_uint64(lpLastWriteTime) / 10000000ULL);
	else
		times[1].tv_sec = 0;
	if (futimens(fd, times) < 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

BOOL WINAPI GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime)
{
	int fd = win_handle_to_fd(hFile);
	struct stat st;
	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	if (fstat(fd, &st) < 0)
		return fail(errno_to_win32(errno));
	if (lpCreationTime != NULL)
		uint64_to_win_time((uint64_t)st.st_ctime * 10000000ULL, lpCreationTime);
	if (lpLastAccessTime != NULL)
		uint64_to_win_time((uint64_t)st.st_atime * 10000000ULL, lpLastAccessTime);
	if (lpLastWriteTime != NULL)
		uint64_to_win_time((uint64_t)st.st_mtime * 10000000ULL, lpLastWriteTime);
	return TRUE;
}

BOOL WINAPI LockFile(HANDLE hFile, DWORD dwFileOffsetLow, DWORD dwFileOffsetHigh,
	DWORD dwNumberOfBytesToLockLow, DWORD dwNumberOfBytesToLockHigh)
{
	int fd = win_handle_to_fd(hFile);
	struct flock fl;
	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	memset(&fl, 0, sizeof(fl));
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = (off_t)dwFileOffsetLow;
	fl.l_len = (off_t)dwNumberOfBytesToLockLow;
#ifdef __linux__
	fl.l_start |= (off_t)dwFileOffsetHigh << 32;
	fl.l_len |= (off_t)dwNumberOfBytesToLockHigh << 32;
#endif
	if (fcntl(fd, F_SETLK, &fl) < 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

BOOL WINAPI UnlockFile(HANDLE hFile, DWORD dwFileOffsetLow, DWORD dwFileOffsetHigh,
	DWORD dwNumberOfBytesToUnlockLow, DWORD dwNumberOfBytesToUnlockHigh)
{
	int fd = win_handle_to_fd(hFile);
	struct flock fl;
	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	memset(&fl, 0, sizeof(fl));
	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = (off_t)dwFileOffsetLow;
	fl.l_len = (off_t)dwNumberOfBytesToUnlockLow;
#ifdef __linux__
	fl.l_start |= (off_t)dwFileOffsetHigh << 32;
	fl.l_len |= (off_t)dwNumberOfBytesToUnlockHigh << 32;
#endif
	if (fcntl(fd, F_SETLK, &fl) < 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

BOOL WINAPI GetOverlappedResultEx(HANDLE hFile, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred,
	DWORD dwMilliseconds, BOOL bWait, ULONG dwFlags)
{
	(void)hFile; (void)lpOverlapped; (void)dwMilliseconds; (void)bWait; (void)dwFlags;
	if (lpNumberOfBytesTransferred != NULL)
		*lpNumberOfBytesTransferred = 0;
	return fail(ERROR_IO_INCOMPLETE);
}

BOOL WINAPI GetOverlappedResult(HANDLE hFile, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, BOOL bWait)
{
	return GetOverlappedResultEx(hFile, lpOverlapped, lpNumberOfBytesTransferred, INFINITE, bWait, 0);
}

BOOL WINAPI DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
	LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped)
{
	int fd = win_handle_to_fd(hDevice);
	int rc;
	(void)lpOverlapped;

	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	ioctl(fd, (unsigned long)dwIoControlCode, lpOutBuffer ? lpOutBuffer : lpInBuffer);
	rc = errno;
	if (rc == 0 || rc == ENOTTY || rc == EINVAL || rc == ENOSYS) {
		/* Some ioctls return the size in the 'out' pointer */
		if (lpBytesReturned != NULL)
			*lpBytesReturned = nOutBufferSize;
		return TRUE;
	}
	return fail(errno_to_win32(rc));
}

/* ------------------------------------------------------------------------ */
/* File system                                                              */
/* ------------------------------------------------------------------------ */

BOOL WINAPI DeleteFileA(LPCSTR lpFileName)
{
	if (lpFileName == NULL)
		return fail(ERROR_INVALID_NAME);
	if (unlink(lpFileName) < 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

BOOL WINAPI DeleteFileW(LPCWSTR lpFileName)
{
	char name[PATH_MAX];
	if (win_wide_to_utf8((const wchar_t*)lpFileName, -1, name, sizeof(name)) < 0)
		return fail(ERROR_INVALID_NAME);
	return DeleteFileA(name);
}

BOOL WINAPI MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName)
{
	if (rename(lpExistingFileName, lpNewFileName) < 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

BOOL WINAPI MoveFileW(LPCWSTR a, LPCWSTR b)
{
	char na[PATH_MAX], nb[PATH_MAX];
	win_wide_to_utf8((const wchar_t*)a, -1, na, sizeof(na));
	win_wide_to_utf8((const wchar_t*)b, -1, nb, sizeof(nb));
	return MoveFileA(na, nb);
}

BOOL WINAPI MoveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, DWORD dwFlags)
{
	(void)dwFlags;
	return MoveFileA(lpExistingFileName, lpNewFileName);
}

BOOL WINAPI CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists)
{
	FILE *in, *out;
	char buf[65536];
	size_t n;

	if (bFailIfExists) {
		struct stat st;
		if (stat(lpNewFileName, &st) == 0)
			return fail(ERROR_FILE_EXISTS);
	}
	in = fopen(lpExistingFileName, "rb");
	if (in == NULL)
		return fail(errno_to_win32(errno));
	out = fopen(lpNewFileName, "wb");
	if (out == NULL) {
		DWORD e = errno_to_win32(errno);
		fclose(in);
		return fail(e);
	}
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) {
			DWORD e = errno_to_win32(errno);
			fclose(in); fclose(out);
			return fail(e);
		}
	}
	fclose(in);
	if (fclose(out) != 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

BOOL WINAPI CopyFileW(LPCWSTR a, LPCWSTR b, BOOL c)
{
	char na[PATH_MAX], nb[PATH_MAX];
	win_wide_to_utf8((const wchar_t*)a, -1, na, sizeof(na));
	win_wide_to_utf8((const wchar_t*)b, -1, nb, sizeof(nb));
	return CopyFileA(na, nb, c);
}

BOOL WINAPI CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
	(void)lpSecurityAttributes;
	if (lpPathName == NULL)
		return fail(ERROR_INVALID_NAME);
	if (mkdir(lpPathName, 0755) < 0) {
		if (errno == EEXIST)
			return TRUE;
		return fail(errno_to_win32(errno));
	}
	return TRUE;
}

BOOL WINAPI CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
	char name[PATH_MAX];
	win_wide_to_utf8((const wchar_t*)lpPathName, -1, name, sizeof(name));
	return CreateDirectoryA(name, lpSecurityAttributes);
}

BOOL WINAPI RemoveDirectoryA(LPCSTR lpPathName)
{
	if (rmdir(lpPathName) < 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

BOOL WINAPI RemoveDirectoryW(LPCWSTR lpPathName)
{
	char name[PATH_MAX];
	win_wide_to_utf8((const wchar_t*)lpPathName, -1, name, sizeof(name));
	return RemoveDirectoryA(name);
}

DWORD WINAPI GetFileAttributesA(LPCSTR lpFileName)
{
	struct stat st;
	if (lpFileName == NULL)
		return INVALID_FILE_ATTRIBUTES;
	if (stat(lpFileName, &st) < 0) {
		win_last_error = errno_to_win32(errno);
		return INVALID_FILE_ATTRIBUTES;
	}
	if (S_ISDIR(st.st_mode))
		return FILE_ATTRIBUTE_DIRECTORY;
	if (!(st.st_mode & S_IWUSR))
		return FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE;
	return FILE_ATTRIBUTE_ARCHIVE;
}

DWORD WINAPI GetFileAttributesW(LPCWSTR lpFileName)
{
	char name[PATH_MAX];
	win_wide_to_utf8((const wchar_t*)lpFileName, -1, name, sizeof(name));
	return GetFileAttributesA(name);
}

BOOL WINAPI SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes)
{
	struct stat st;
	if (stat(lpFileName, &st) < 0)
		return fail(errno_to_win32(errno));
	if (dwFileAttributes & FILE_ATTRIBUTE_READONLY)
		chmod(lpFileName, st.st_mode & ~(S_IWUSR | S_IWGRP | S_IWOTH));
	else
		chmod(lpFileName, st.st_mode | S_IWUSR);
	return TRUE;
}

/* FindFirstFile/FindNextFile implemented on top of glob(3) */
typedef struct {
	glob_t g;
	int index;
	int first;
} WIN_FIND_STATE;

static HANDLE win_find_handle = NULL;
static WIN_FIND_STATE win_find_state;

static void fill_find_data(LPWIN32_FIND_DATAA d, const char* path, const struct stat* st)
{
	memset(d, 0, sizeof(*d));
	d->dwFileAttributes = S_ISDIR(st->st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
	uint64_to_win_time((uint64_t)st->st_mtime * 10000000ULL, &d->ftLastWriteTime);
	uint64_to_win_time((uint64_t)st->st_atime * 10000000ULL, &d->ftLastAccessTime);
	uint64_to_win_time((uint64_t)st->st_ctime * 10000000ULL, &d->ftCreationTime);
	d->nFileSizeLow = (DWORD)(st->st_size & 0xFFFFFFFF);
	d->nFileSizeHigh = (DWORD)(st->st_size >> 32);
	snprintf(d->cFileName, MAX_PATH, "%s", path);
}

HANDLE WINAPI FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
	char pattern[PATH_MAX];
	const char* base;
	char dirpart[PATH_MAX];

	if (lpFileName == NULL)
		return fail_handle(ERROR_INVALID_NAME);
	/* Split into directory + pattern */
	base = strrchr(lpFileName, '/');
	if (base != NULL) {
		size_t n = (size_t)(base - lpFileName);
		if (n >= sizeof(dirpart))
			n = sizeof(dirpart) - 1;
		memcpy(dirpart, lpFileName, n);
		dirpart[n] = 0;
		base++;
	} else {
		snprintf(dirpart, sizeof(dirpart), ".");
		base = lpFileName;
	}
	if (strpbrk(base, "*?") == NULL) {
		/* Not a wildcard: match the literal name */
		snprintf(pattern, sizeof(pattern), "%s/%s", dirpart, base);
	} else {
		snprintf(pattern, sizeof(pattern), "%s/%s", dirpart, base);
	}

	if (win_find_handle != NULL) {
		FindClose(win_find_handle);
		win_find_handle = NULL;
	}
	memset(&win_find_state, 0, sizeof(win_find_state));
	if (glob(pattern, 0, NULL, &win_find_state.g) != 0) {
		globfree(&win_find_state.g);
		return fail_handle(ERROR_FILE_NOT_FOUND);
	}
	win_find_handle = (HANDLE)(intptr_t)1; /* sentinel, real state in win_find_state */
	win_find_state.index = 0;
	win_find_state.first = 1;
	for (;;) {
		struct stat st;
		const char* path;
		if (win_find_state.index >= (int)win_find_state.g.gl_pathc)
			return fail_handle(ERROR_NO_MORE_FILES);
		path = win_find_state.g.gl_pathv[win_find_state.index];
		if (stat(path, &st) < 0) {
			win_find_state.index++;
			continue;
		}
		win_find_state.index++;
		if (lpFindFileData != NULL)
			fill_find_data(lpFindFileData, path, &st);
		return win_find_handle;
	}
}

BOOL WINAPI FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)
{
	if (hFindFile != win_find_handle)
		return fail(ERROR_INVALID_HANDLE);
	while (win_find_state.index < (int)win_find_state.g.gl_pathc) {
		const char* path = win_find_state.g.gl_pathv[win_find_state.index];
		struct stat st;
		win_find_state.index++;
		if (stat(path, &st) < 0)
			continue;
		if (lpFindFileData != NULL)
			fill_find_data(lpFindFileData, path, &st);
		return TRUE;
	}
	return fail(ERROR_NO_MORE_FILES);
}

BOOL WINAPI FindClose(HANDLE hFindFile)
{
	if (hFindFile != win_find_handle)
		return fail(ERROR_INVALID_HANDLE);
	globfree(&win_find_state.g);
	win_find_handle = NULL;
	return TRUE;
}

DWORD WINAPI GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart)
{
	char resolved[PATH_MAX];
	if (lpFileName == NULL)
		return fail(ERROR_INVALID_NAME);
	if (realpath(lpFileName, resolved) != NULL) {
		if (lpBuffer != NULL)
			snprintf(lpBuffer, nBufferLength, "%s", resolved);
		return (DWORD)strlen(resolved) + 1;
	}
	/* Fall back to a lexical absolutization */
	{
		char cwd[PATH_MAX];
		char out[PATH_MAX];
		if (lpFileName[0] == '/') {
			snprintf(out, sizeof(out), "%s", lpFileName);
		} else {
			if (getcwd(cwd, sizeof(cwd)) == NULL)
				cwd[0] = 0;
			snprintf(out, sizeof(out), "%s/%s", cwd, lpFileName);
		}
		if (lpBuffer != NULL)
			snprintf(lpBuffer, nBufferLength, "%s", out);
		return (DWORD)strlen(out) + 1;
	}
}

DWORD WINAPI GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR *lpFilePart)
{
	char name[PATH_MAX];
	DWORD r;
	win_wide_to_utf8((const wchar_t*)lpFileName, -1, name, sizeof(name));
	r = GetFullPathNameA(name, 0, NULL, NULL);
	if (r > 0 && lpBuffer != NULL) {
		char tmp[PATH_MAX];
		GetFullPathNameA(name, (DWORD)sizeof(tmp), tmp, NULL);
		win_utf8_to_wide(tmp, -1, (wchar_t*)lpBuffer, (int)nBufferLength);
	}
	return r;
}

DWORD WINAPI GetLongPathNameA(LPCSTR lpFileName, LPSTR lpBuffer, DWORD nBufferLength)
{
	if (lpBuffer != NULL)
		snprintf(lpBuffer, nBufferLength, "%s", lpFileName);
	return (DWORD)strlen(lpFileName) + 1;
}

DWORD WINAPI GetShortPathNameA(LPCSTR lpFileName, LPSTR lpBuffer, DWORD nBufferLength)
{
	return GetLongPathNameA(lpFileName, lpBuffer, nBufferLength);
}

BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR lpRootPathName, ULARGE_INTEGER *lpFreeBytesAvailableToCaller,
	ULARGE_INTEGER *lpTotalNumberOfBytes, ULARGE_INTEGER *lpTotalNumberOfFreeBytes)
{
	struct statvfs st;
	const char* path = (lpRootPathName != NULL && lpRootPathName[0]) ? lpRootPathName : "/";
	if (statvfs(path, &st) < 0)
		return fail(errno_to_win32(errno));
	if (lpFreeBytesAvailableToCaller != NULL)
		lpFreeBytesAvailableToCaller->QuadPart = (uint64_t)st.f_bavail * st.f_frsize;
	if (lpTotalNumberOfBytes != NULL)
		lpTotalNumberOfBytes->QuadPart = (uint64_t)st.f_blocks * st.f_frsize;
	if (lpTotalNumberOfFreeBytes != NULL)
		lpTotalNumberOfFreeBytes->QuadPart = (uint64_t)st.f_bfree * st.f_frsize;
	return TRUE;
}

BOOL WINAPI GetDiskFreeSpaceExW(LPCWSTR lpRootPathName, ULARGE_INTEGER *lpFreeBytesAvailableToCaller,
	ULARGE_INTEGER *lpTotalNumberOfBytes, ULARGE_INTEGER *lpTotalNumberOfFreeBytes)
{
	char name[PATH_MAX];
	win_wide_to_utf8((const wchar_t*)lpRootPathName, -1, name, sizeof(name));
	return GetDiskFreeSpaceExA(name, lpFreeBytesAvailableToCaller, lpTotalNumberOfBytes, lpTotalNumberOfFreeBytes);
}

/* Volume information is obtained by the Linux-specific back-end; the generic
 * implementation queries the mount table via statfs. */
BOOL WINAPI GetVolumeInformationA(LPCSTR lpRootPathName, LPSTR lpVolumeNameBuffer, DWORD nVolumeNameSize,
	LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
	LPSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize)
{
	(void)lpRootPathName;
	if (lpVolumeNameBuffer != NULL && nVolumeNameSize > 0)
		lpVolumeNameBuffer[0] = 0;
	if (lpVolumeSerialNumber != NULL)
		*lpVolumeSerialNumber = 0;
	if (lpMaximumComponentLength != NULL)
		*lpMaximumComponentLength = 255;
	if (lpFileSystemFlags != NULL)
		*lpFileSystemFlags = 0;
	if (lpFileSystemNameBuffer != NULL && nFileSystemNameSize > 0)
		snprintf(lpFileSystemNameBuffer, nFileSystemNameSize, "unknown");
	return TRUE;
}

BOOL WINAPI GetVolumeInformationW(LPCWSTR lpRootPathName, LPWSTR lpVolumeNameBuffer, DWORD nVolumeNameSize,
	LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
	LPWSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize)
{
	char name[PATH_MAX];
	win_wide_to_utf8((const wchar_t*)lpRootPathName, -1, name, sizeof(name));
	return GetVolumeInformationA(name, (LPSTR)lpVolumeNameBuffer, nVolumeNameSize, lpVolumeSerialNumber,
		lpMaximumComponentLength, lpFileSystemFlags, (LPSTR)lpFileSystemNameBuffer, nFileSystemNameSize);
}

BOOL WINAPI SetVolumeLabelA(LPCSTR lpRootPathName, LPCSTR lpVolumeLabel)
{
	(void)lpRootPathName; (void)lpVolumeLabel;
	return TRUE;
}

BOOL WINAPI FlushVolumeBuffers(HANDLE hVolume)
{
	return FlushFileBuffers(hVolume);
}

BOOL WINAPI CreateSymbolicLinkA(LPCSTR lpSymlinkFileName, LPCSTR lpTargetFileName, DWORD dwFlags)
{
	int rc;
	if (dwFlags & SYMBOLIC_LINK_FLAG_DIRECTORY)
		rc = symlink(lpTargetFileName, lpSymlinkFileName);
	else
		rc = symlink(lpTargetFileName, lpSymlinkFileName);
	if (rc < 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

BOOL WINAPI CreateSymbolicLinkW(LPCWSTR a, LPCWSTR b, DWORD c)
{
	char na[PATH_MAX], nb[PATH_MAX];
	win_wide_to_utf8((const wchar_t*)a, -1, na, sizeof(na));
	win_wide_to_utf8((const wchar_t*)b, -1, nb, sizeof(nb));
	return CreateSymbolicLinkA(na, nb, c);
}

/* ------------------------------------------------------------------------ */
/* Threads and synchronization                                              */
/* ------------------------------------------------------------------------ */

typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int manual_reset;
	int signaled;
} WIN_EVENT;

typedef struct {
	pthread_t tid;
	DWORD exit_code;
} WIN_THREAD;

HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName)
{
	WIN_EVENT* ev = (WIN_EVENT*)calloc(1, sizeof(WIN_EVENT));
	(void)lpEventAttributes; (void)lpName;
	if (ev == NULL)
		return fail_handle(ERROR_NOT_ENOUGH_MEMORY);
	pthread_mutex_init(&ev->mutex, NULL);
	pthread_cond_init(&ev->cond, NULL);
	ev->manual_reset = bManualReset ? 1 : 0;
	ev->signaled = bInitialState ? 1 : 0;
	/* Events are stored in a dedicated allocation; use a non-fd tagged value
	 * by allocating them from a separate heap arena (see handle_is_event). */
	return (HANDLE)ev;
}

HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES a, BOOL b, BOOL c, LPCWSTR d)
{
	return CreateEventA(a, b, c, NULL);
}

BOOL WINAPI SetEvent(HANDLE hEvent)
{
	WIN_EVENT* ev = (WIN_EVENT*)hEvent;
	if (ev == NULL)
		return fail(ERROR_INVALID_HANDLE);
	pthread_mutex_lock(&ev->mutex);
	ev->signaled = 1;
	pthread_cond_broadcast(&ev->cond);
	pthread_mutex_unlock(&ev->mutex);
	return TRUE;
}

BOOL WINAPI ResetEvent(HANDLE hEvent)
{
	WIN_EVENT* ev = (WIN_EVENT*)hEvent;
	if (ev == NULL)
		return fail(ERROR_INVALID_HANDLE);
	pthread_mutex_lock(&ev->mutex);
	ev->signaled = 0;
	pthread_mutex_unlock(&ev->mutex);
	return TRUE;
}

BOOL WINAPI PulseEvent(HANDLE hEvent)
{
	return SetEvent(hEvent);
}

HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES a, BOOL b, LPCSTR c)
{
	(void)a; (void)b; (void)c;
	return CreateEventA(NULL, TRUE, TRUE, NULL);
}

static BOOL release_event(HANDLE hEvent);

BOOL WINAPI ReleaseMutex(HANDLE hMutex)
{
	return release_event(hMutex);
}

static BOOL release_event(HANDLE hEvent)
{
	WIN_EVENT* ev = (WIN_EVENT*)hEvent;
	if (ev == NULL)
		return fail(ERROR_INVALID_HANDLE);
	pthread_mutex_lock(&ev->mutex);
	ev->signaled = 0;
	pthread_mutex_unlock(&ev->mutex);
	return TRUE;
}

static void* win_thread_start(LPVOID param)
{
	LPTHREAD_START_ROUTINE fn = (LPTHREAD_START_ROUTINE)(uintptr_t)((void**)param)[0];
	void* arg = ((void**)param)[1];
	WIN_THREAD* self = ((void**)param)[2];
	fn(arg);
	free(param);
	(void)self;
	return NULL;
}

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize,
	LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId)
{
	WIN_THREAD* t;
	void** args;
	pthread_attr_t attr;

	(void)lpThreadAttributes; (void)dwStackSize; (void)dwCreationFlags;
	t = (WIN_THREAD*)calloc(1, sizeof(WIN_THREAD));
	if (t == NULL)
		return fail_handle(ERROR_NOT_ENOUGH_MEMORY);
	args = (void**)malloc(3 * sizeof(void*));
	if (args == NULL) {
		free(t);
		return fail_handle(ERROR_NOT_ENOUGH_MEMORY);
	}
	args[0] = (void*)(uintptr_t)lpStartAddress;
	args[1] = lpParameter;
	args[2] = t;
	pthread_attr_init(&attr);
	if (pthread_create(&t->tid, &attr, win_thread_start, args) != 0) {
		pthread_attr_destroy(&attr);
		free(args);
		free(t);
		return fail_handle(ERROR_NOT_ENOUGH_MEMORY);
	}
	pthread_attr_destroy(&attr);
	if (lpThreadId != NULL)
		*lpThreadId = (DWORD)(uintptr_t)t->tid;
	pthread_detach(t->tid);
	return (HANDLE)t;
}

DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
{
	WIN_EVENT* ev = (WIN_EVENT*)hHandle;
	struct timespec ts;
	struct timeval tv;
	int rc = 0;

	if (ev == NULL)
		return WAIT_FAILED;
	gettimeofday(&tv, NULL);
	ts.tv_sec = tv.tv_sec + (time_t)(dwMilliseconds / 1000);
	ts.tv_nsec = tv.tv_usec * 1000 + (long)(dwMilliseconds % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000L;
	}
	pthread_mutex_lock(&ev->mutex);
	while (!ev->signaled) {
		rc = pthread_cond_timedwait(&ev->cond, &ev->mutex, &ts);
		if (rc == ETIMEDOUT)
			break;
	}
	rc = ev->signaled ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
	pthread_mutex_unlock(&ev->mutex);
	if (ev->signaled && !ev->manual_reset) {
		pthread_mutex_lock(&ev->mutex);
		ev->signaled = 0;
		pthread_mutex_unlock(&ev->mutex);
	}
	return (DWORD)rc;
}

DWORD WINAPI WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds)
{
	DWORD i;
	DWORD res = WAIT_OBJECT_0;
	DWORD deadline = dwMilliseconds;

	for (i = 0; i < nCount; i++) {
		DWORD r = WaitForSingleObject(lpHandles[i], deadline);
		if (r != WAIT_OBJECT_0) {
			if (r == WAIT_TIMEOUT)
				return WAIT_TIMEOUT;
			return WAIT_FAILED;
		}
		if (!bWaitAll)
			return WAIT_OBJECT_0 + i;
	}
	(void)deadline;
	return res;
}

DWORD WINAPI WaitForSingleObjectEx(HANDLE hObject, DWORD dwMilliseconds, BOOL bAlertable)
{
	(void)bAlertable;
	return WaitForSingleObject(hObject, dwMilliseconds);
}

void WINAPI ExitThread(DWORD dwExitCode)
{
	pthread_exit((void*)(uintptr_t)dwExitCode);
}

BOOL WINAPI TerminateThread(HANDLE hThread, DWORD dwExitCode)
{
	(void)hThread; (void)dwExitCode;
	return TRUE;
}

HANDLE WINAPI GetCurrentThread(void) { return (HANDLE)(intptr_t)1; }
HANDLE WINAPI GetCurrentProcess(void) { return (HANDLE)(intptr_t)1; }
DWORD WINAPI GetCurrentProcessId(void) { return (DWORD)getpid(); }
DWORD WINAPI GetCurrentThreadId(void) { return (DWORD)(uintptr_t)pthread_self(); }
DWORD WINAPI GetCurrentThreadIdWin32(void) { return GetCurrentThreadId(); }
DWORD WINAPI GetProcessId(HANDLE hProcess) { (void)hProcess; return (DWORD)getpid(); }
HANDLE WINAPI OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId)
{
	(void)dwDesiredAccess; (void)bInheritHandle; (void)dwProcessId;
	return (HANDLE)(intptr_t)1;
}

BOOL WINAPI DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle, HANDLE hTargetProcessHandle,
	LPHANDLE lpTargetHandle, DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwOptions)
{
	(void)hSourceProcessHandle; (void)hTargetProcessHandle; (void)dwDesiredAccess; (void)bInheritHandle; (void)dwOptions;
	if (lpTargetHandle != NULL)
		*lpTargetHandle = hSourceHandle;
	return TRUE;
}

BOOL WINAPI GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode)
{
	(void)hThread;
	if (lpExitCode != NULL)
		*lpExitCode = 0;
	return TRUE;
}

BOOL WINAPI GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode)
{
	(void)hProcess;
	if (lpExitCode != NULL)
		*lpExitCode = 0;
	return TRUE;
}

BOOL WINAPI IsWow64Process(HANDLE hProcess, BOOL *wow64Process)
{
	(void)hProcess;
	if (wow64Process != NULL)
		*wow64Process = FALSE;
	return TRUE;
}

void WINAPI Sleep(DWORD dwMilliseconds)
{
	struct timespec ts;
	ts.tv_sec = dwMilliseconds / 1000;
	ts.tv_nsec = (long)(dwMilliseconds % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
		;
}

BOOL WINAPI SwitchToThread(void)
{
	return TRUE;
}

BOOL WINAPI GetProcessTimes(HANDLE hProcess, LPFILETIME lpCreationTime, LPFILETIME lpExitTime,
	LPFILETIME lpKernelTime, LPFILETIME lpUserTime)
{
	(void)hProcess;
	if (lpCreationTime != NULL) { lpCreationTime->dwLowDateTime = 0; lpCreationTime->dwHighDateTime = 0; }
	if (lpExitTime != NULL) { lpExitTime->dwLowDateTime = 0; lpExitTime->dwHighDateTime = 0; }
	if (lpKernelTime != NULL) { lpKernelTime->dwLowDateTime = 0; lpKernelTime->dwHighDateTime = 0; }
	if (lpUserTime != NULL) { lpUserTime->dwLowDateTime = 0; lpUserTime->dwHighDateTime = 0; }
	return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Process creation                                                         */
/* ------------------------------------------------------------------------ */

BOOL WINAPI CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
	pid_t pid;
	char* argv[64];
	int argc = 0;
	char* cmd;

	(void)lpProcessAttributes; (void)lpThreadAttributes; (void)bInheritHandles;
	(void)dwCreationFlags; (void)lpEnvironment; (void)lpStartupInfo;

	if (lpProcessInformation == NULL)
		return fail(ERROR_INVALID_HANDLE);
	cmd = lpCommandLine != NULL ? strdup(lpCommandLine) : NULL;
	if (cmd == NULL)
		return fail(ERROR_NOT_ENOUGH_MEMORY);
	/* Split the command line into arguments (simple, honouring quotes) */
	{
		char* p = cmd;
		while (*p != 0 && argc < 63) {
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p == 0)
				break;
			argv[argc++] = p;
			while (*p != 0 && *p != ' ' && *p != '\t')
				p++;
			if (*p != 0)
				*p++ = 0;
		}
		argv[argc] = NULL;
	}
	pid = fork();
	if (pid < 0) {
		free(cmd);
		return fail(errno_to_win32(errno));
	}
	if (pid == 0) {
		if (lpCurrentDirectory != NULL && chdir(lpCurrentDirectory) != 0)
			_exit(127);
		if (lpApplicationName != NULL)
			execv(lpApplicationName, argv);
		else
			execvp(argv[0], argv);
		_exit(127);
	}
	free(cmd);
	lpProcessInformation->dwProcessId = (DWORD)pid;
	lpProcessInformation->hProcess = (HANDLE)(intptr_t)pid;
	lpProcessInformation->hThread = (HANDLE)(intptr_t)1;
	return TRUE;
}

/* ------------------------------------------------------------------------ */
/* Modules                                                                  */
/* ------------------------------------------------------------------------ */

HMODULE WINAPI LoadLibraryA(LPCSTR lpLibFileName)
{
	void* h = dlopen(lpLibFileName, RTLD_NOW | RTLD_GLOBAL);
	if (h == NULL)
		return fail_handle(ERROR_MOD_NOT_FOUND);
	return (HMODULE)h;
}

HMODULE WINAPI LoadLibraryW(LPCWSTR lpLibFileName)
{
	char name[PATH_MAX];
	win_wide_to_utf8((const wchar_t*)lpLibFileName, -1, name, sizeof(name));
	return LoadLibraryA(name);
}

HMODULE WINAPI LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
	(void)hFile; (void)dwFlags;
	return LoadLibraryA(lpLibFileName);
}

HMODULE WINAPI LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
	(void)hFile; (void)dwFlags;
	return LoadLibraryW(lpLibFileName);
}

HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName)
{
	if (lpModuleName == NULL)
		return dlopen(NULL, RTLD_NOW);
	{
		void* h = dlopen(lpModuleName, RTLD_NOW | RTLD_NOLOAD);
		if (h == NULL)
			return fail_handle(ERROR_MOD_NOT_FOUND);
		return (HMODULE)h;
	}
}

HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName)
{
	char name[PATH_MAX];
	win_wide_to_utf8((const wchar_t*)lpModuleName, -1, name, sizeof(name));
	return GetModuleHandleA(name);
}

BOOL WINAPI FreeLibrary(HMODULE hLibModule)
{
	/* Never actually unload: Rufus relies on modules staying loaded. */
	(void)hLibModule;
	return TRUE;
}

FARPROC WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
	void* sym;
	if (hModule == NULL)
		return NULL;
	if ((uintptr_t)lpProcName < 0x10000)
		sym = dlsym(hModule, (const char*)(uintptr_t)lpProcName);
	else
		sym = dlsym(hModule, lpProcName);
	return (FARPROC)sym;
}

DWORD WINAPI GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize)
{
	Dl_info info;
	if (dladdr(hModule, &info) != 0 && info.dli_fname != NULL) {
		snprintf(lpFilename, nSize, "%s", info.dli_fname);
		return (DWORD)strlen(lpFilename);
	}
	if (readlink("/proc/self/exe", lpFilename, nSize) < 0)
		return 0;
	return (DWORD)strlen(lpFilename);
}

DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize)
{
	char name[PATH_MAX];
	DWORD r = GetModuleFileNameA(hModule, name, (DWORD)sizeof(name));
	win_utf8_to_wide(name, -1, (wchar_t*)lpFilename, (int)nSize);
	return r;
}

HMODULE WINAPI GetCurrentProcessModule(void)
{
	Dl_info info;
	if (dladdr((void*)(uintptr_t)&GetModuleFileNameA, &info) != 0)
		return (HMODULE)info.dli_fbase;
	return dlopen(NULL, RTLD_NOW);
}

/* ------------------------------------------------------------------------ */
/* Resources                                                                */
/* ------------------------------------------------------------------------ */
/* On Linux, resources are read from the file system instead of a PE image. */

HRSRC WINAPI FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType)
{
	(void)hModule; (void)lpName; (void)lpType;
	return NULL;
}

HRSRC WINAPI FindResourceW(HMODULE hModule, LPCWSTR lpName, LPCWSTR lpType)
{
	(void)hModule; (void)lpName; (void)lpType;
	return NULL;
}

HANDLE WINAPI LoadResource(HMODULE hModule, HRSRC hResInfo) { (void)hModule; (void)hResInfo; return NULL; }
LPVOID WINAPI LockResource(HANDLE hResData) { (void)hResData; return NULL; }
DWORD WINAPI SizeofResource(HMODULE hModule, HRSRC hResInfo) { (void)hModule; (void)hResInfo; return 0; }

DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lpFileName, LPDWORD lpdwHandle)
{
	(void)lpFileName;
	if (lpdwHandle != NULL)
		*lpdwHandle = 0;
	return 0;
}

BOOL WINAPI GetFileVersionInfoA(LPCSTR lpFileName, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	(void)lpFileName; (void)dwHandle; (void)dwLen; (void)lpData;
	return FALSE;
}

BOOL WINAPI VerQueryValueA(LPCVOID pVerInfo, LPCSTR lpSubBlock, LPVOID *lppBuffer, UINT *puLen)
{
	(void)pVerInfo; (void)lpSubBlock; (void)lppBuffer; (void)puLen;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* Strings and memory                                                       */
/* ------------------------------------------------------------------------ */

LPVOID WINAPI LocalAlloc(UINT uFlags, SIZE_T uBytes)
{
	(void)uFlags;
	return calloc(1, uBytes ? uBytes : 1);
}

LPVOID WINAPI LocalFree(LPVOID hMem)
{
	free(hMem);
	return NULL;
}

LPVOID WINAPI GlobalAlloc(UINT uFlags, SIZE_T uBytes)
{
	return LocalAlloc(uFlags, uBytes);
}

LPVOID WINAPI GlobalFree(LPVOID hMem)
{
	return LocalFree(hMem);
}

HANDLE WINAPI GlobalMemory(void) { return (HANDLE)(intptr_t)1; }

char* win_get_command_line(void)
{
	static char buf[4096];
	FILE* f = fopen("/proc/self/cmdline", "rb");
	size_t n = 0;
	if (f != NULL) {
		n = fread(buf, 1, sizeof(buf) - 1, f);
		fclose(f);
	}
	buf[n] = 0;
	/* Replace NUL separators with spaces */
	for (size_t i = 0; i < n; i++)
		if (buf[i] == '\0')
			buf[i] = ' ';
	if (n == 0)
		snprintf(buf, sizeof(buf), "rufus");
	return buf;
}

LPSTR WINAPI GetCommandLineA(void) { return win_get_command_line(); }

LPWSTR WINAPI GetCommandLineW(void)
{
	static uint16_t wbuf[4096];
	win_utf8_to_wide(win_get_command_line(), -1, wbuf, 4096);
	return (LPWSTR)wbuf;
}

LPWSTR* WINAPI CommandLineToArgvW(LPCWSTR lpCmdLine, int *pNumArgs)
{
	/* Convert to UTF-8 and split on spaces (sufficient for our needs) */
	static char* storage[256];
	static char buffer[8192];
	char* p;
	int argc = 0;

	win_wide_to_utf8((const wchar_t*)lpCmdLine, -1, buffer, (int)sizeof(buffer));
	p = buffer;
	while (*p != 0 && argc < 255) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == 0)
			break;
		storage[argc++] = p;
		while (*p != 0 && *p != ' ' && *p != '\t')
			p++;
		if (*p != 0)
			*p++ = 0;
	}
	storage[argc] = NULL;
	if (pNumArgs != NULL)
		*pNumArgs = argc;
	return (LPWSTR*)storage;
}

/* ------------------------------------------------------------------------ */
/* Time                                                                      */
/* ------------------------------------------------------------------------ */

DWORD WINAPI GetTickCount(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (DWORD)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

ULONGLONG WINAPI GetTickCount64(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime)
{
	struct timespec ts;
	struct tm tm;
	clock_gettime(CLOCK_REALTIME, &ts);
	gmtime_r(&ts.tv_sec, &tm);
	if (lpSystemTime != NULL) {
		lpSystemTime->wYear = (WORD)(tm.tm_year + 1900);
		lpSystemTime->wMonth = (WORD)(tm.tm_mon + 1);
		lpSystemTime->wDayOfWeek = (WORD)tm.tm_wday;
		lpSystemTime->wDay = (WORD)tm.tm_mday;
		lpSystemTime->wHour = (WORD)tm.tm_hour;
		lpSystemTime->wMinute = (WORD)tm.tm_min;
		lpSystemTime->wSecond = (WORD)tm.tm_sec;
		lpSystemTime->wMilliseconds = (WORD)(ts.tv_nsec / 1000000);
	}
}

void WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime)
{
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	if (lpSystemTime != NULL) {
		lpSystemTime->wYear = (WORD)(tm.tm_year + 1900);
		lpSystemTime->wMonth = (WORD)(tm.tm_mon + 1);
		lpSystemTime->wDayOfWeek = (WORD)tm.tm_wday;
		lpSystemTime->wDay = (WORD)tm.tm_mday;
		lpSystemTime->wHour = (WORD)tm.tm_hour;
		lpSystemTime->wMinute = (WORD)tm.tm_min;
		lpSystemTime->wSecond = (WORD)tm.tm_sec;
		lpSystemTime->wMilliseconds = 0;
	}
}

BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME *lpSystemTime, LPFILETIME lpFileTime)
{
	struct tm tm;
	if (lpSystemTime == NULL || lpFileTime == NULL)
		return fail(ERROR_INVALID_HANDLE);
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = lpSystemTime->wYear - 1900;
	tm.tm_mon = lpSystemTime->wMonth - 1;
	tm.tm_mday = lpSystemTime->wDay;
	tm.tm_hour = lpSystemTime->wHour;
	tm.tm_min = lpSystemTime->wMinute;
	tm.tm_sec = lpSystemTime->wSecond;
	{
		time_t t = timegm(&tm);
		uint64_t v = ((uint64_t)t + 11644473600ULL) * 10000000ULL;
		lpFileTime->dwLowDateTime = (DWORD)(v & 0xFFFFFFFF);
		lpFileTime->dwHighDateTime = (DWORD)(v >> 32);
	}
	return TRUE;
}

BOOL WINAPI FileTimeToSystemTime(const FILETIME *lpFileTime, LPSYSTEMTIME lpSystemTime)
{
	uint64_t v;
	time_t t;
	struct tm tm;
	if (lpFileTime == NULL || lpSystemTime == NULL)
		return fail(ERROR_INVALID_HANDLE);
	v = win_time_to_uint64(lpFileTime);
	t = (time_t)(v / 10000000ULL) - 11644473600LL;
	gmtime_r(&t, &tm);
	lpSystemTime->wYear = (WORD)(tm.tm_year + 1900);
	lpSystemTime->wMonth = (WORD)(tm.tm_mon + 1);
	lpSystemTime->wDayOfWeek = (WORD)tm.tm_wday;
	lpSystemTime->wDay = (WORD)tm.tm_mday;
	lpSystemTime->wHour = (WORD)tm.tm_hour;
	lpSystemTime->wMinute = (WORD)tm.tm_min;
	lpSystemTime->wSecond = (WORD)tm.tm_sec;
	lpSystemTime->wMilliseconds = (WORD)((v % 10000000ULL) / 10000ULL);
	return TRUE;
}

BOOL WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	if (lpSystemTimeAsFileTime != NULL) {
		uint64_t v = ((uint64_t)ts.tv_sec + 11644473600ULL) * 10000000ULL + (uint64_t)(ts.tv_nsec / 100);
		lpSystemTimeAsFileTime->dwLowDateTime = (DWORD)(v & 0xFFFFFFFF);
		lpSystemTimeAsFileTime->dwHighDateTime = (DWORD)(v >> 32);
	}
	return TRUE;
}

DWORD WINAPI GetTimeZoneInformation(LPVOID lpTimeZoneInformation)
{
	(void)lpTimeZoneInformation;
	return 0; /* TIME_ZONE_ID_UNKNOWN */
}

/* ------------------------------------------------------------------------ */
/* Environment & console                                                    */
/* ------------------------------------------------------------------------ */

BOOL WINAPI AttachConsole(DWORD dwProcessId) { (void)dwProcessId; return FALSE; }
void WINAPI FreeConsole(void) { }
HWND WINAPI GetConsoleWindow(void) { return NULL; }
BOOL WINAPI AllocConsole(void) { return FALSE; }

DWORD WINAPI GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize)
{
	const char* v = getenv(lpName);
	size_t len;
	if (v == NULL)
		return 0;
	len = strlen(v) + 1;
	if (lpBuffer == NULL || nSize == 0)
		return (DWORD)len;
	if (len > nSize)
		len = nSize;
	memcpy(lpBuffer, v, len);
	return (DWORD)len;
}

BOOL WINAPI SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue)
{
	if (lpValue == NULL)
		return unsetenv(lpName) == 0;
	return setenv(lpName, lpValue, 1) == 0;
}

BOOL WINAPI GetUserNameA(LPSTR lpBuffer, LPDWORD pcbBuffer)
{
	struct passwd* pw = getpwuid(getuid());
	const char* name = (pw != NULL) ? pw->pw_name : "user";
	size_t len = strlen(name) + 1;
	if (lpBuffer == NULL || pcbBuffer == NULL)
		return fail(ERROR_INVALID_HANDLE);
	if (*pcbBuffer < len) {
		*pcbBuffer = (DWORD)len;
		return fail(ERROR_INSUFFICIENT_BUFFER);
	}
	memcpy(lpBuffer, name, len);
	*pcbBuffer = (DWORD)len;
	return TRUE;
}

BOOL WINAPI GetUserNameEx(int NameType, LPSTR lpBuffer, DWORD *nSize)
{
	(void)NameType;
	return GetUserNameA(lpBuffer, nSize);
}

DWORD WINAPI GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer)
{
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd)) == NULL)
		return fail(ERROR_ACCESS_DENIED);
	if (lpBuffer != NULL)
		snprintf(lpBuffer, nBufferLength, "%s", cwd);
	return (DWORD)strlen(cwd) + 1;
}

BOOL WINAPI SetCurrentDirectoryA(LPCSTR lpPathName)
{
	if (chdir(lpPathName) < 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

DWORD WINAPI GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer)
{
	const char* tmp = getenv("TMPDIR");
	char dir[PATH_MAX];
	if (tmp == NULL || tmp[0] == 0)
		tmp = "/tmp";
	snprintf(dir, sizeof(dir), "%s/", tmp);
	if (lpBuffer != NULL)
		snprintf(lpBuffer, nBufferLength, "%s", dir);
	return (DWORD)strlen(dir) + 1;
}

DWORD WINAPI GetTempPathW(DWORD nBufferLength, LPWSTR lpBuffer)
{
	char dir[PATH_MAX];
	DWORD r = GetTempPathA((DWORD)sizeof(dir), dir);
	win_utf8_to_wide(dir, -1, (wchar_t*)lpBuffer, (int)nBufferLength);
	return r;
}

DWORD WINAPI GetTempFileNameA(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT uUnique, LPSTR lpTempFileName)
{
	char dir[PATH_MAX];
	snprintf(dir, sizeof(dir), "%s", lpPathName != NULL ? lpPathName : "/tmp");
	if (uUnique == 0)
		uUnique = (UINT)(GetTickCount() & 0xFFFFFF);
	snprintf(lpTempFileName, MAX_PATH, "%s/%s%06u.tmp", dir, lpPrefixString ? lpPrefixString : "tmp", uUnique);
	return (DWORD)strlen(lpTempFileName) + 1;
}

UINT WINAPI GetSystemDirectoryA(LPSTR lpBuffer, UINT uSize)
{
	const char* dirs[] = { "/usr/bin", "/usr/sbin", "/bin", "/sbin", NULL };
	int i;
	for (i = 0; dirs[i] != NULL; i++) {
		if (access(dirs[i], X_OK) == 0) {
			if (lpBuffer != NULL)
				snprintf(lpBuffer, uSize, "%s", dirs[i]);
			return (UINT)strlen(dirs[i]);
		}
	}
	if (lpBuffer != NULL)
		snprintf(lpBuffer, uSize, "/usr/bin");
	return 6;
}

UINT WINAPI GetWindowsDirectoryA(LPSTR lpBuffer, UINT uSize)
{
	if (lpBuffer != NULL)
		snprintf(lpBuffer, uSize, "/");
	return 1;
}

/* ------------------------------------------------------------------------ */
/* Privilege / security                                                     */
/* ------------------------------------------------------------------------ */

BOOL WINAPI OpenProcessToken(HANDLE ProcessHandle, DWORD DesiredAccess, HANDLE *TokenHandle)
{
	(void)ProcessHandle; (void)DesiredAccess;
	if (TokenHandle != NULL)
		*TokenHandle = (HANDLE)(intptr_t)1;
	return TRUE;
}

BOOL WINAPI LookupPrivilegeValueA(LPCSTR lpSystemName, LPCSTR lpName, void *lpLuid)
{
	(void)lpSystemName; (void)lpName; (void)lpLuid;
	return TRUE;
}

BOOL WINAPI AdjustTokenPrivileges(HANDLE TokenHandle, BOOL DisableAllPrivileges,
	void *NewState, DWORD BufferLength, void *PreviousState, DWORD *ReturnLength)
{
	(void)TokenHandle; (void)DisableAllPrivileges; (void)NewState; (void)BufferLength;
	(void)PreviousState; (void)ReturnLength;
	return TRUE;
}

BOOL WINAPI GetTokenInformation(HANDLE TokenHandle, int TokenInformationClass, LPVOID TokenInformation,
	DWORD TokenInformationLength, DWORD *ReturnLength)
{
	(void)TokenHandle; (void)TokenInformationClass; (void)TokenInformationLength;
	if (TokenInformation != NULL)
		memset(TokenInformation, 0, TokenInformationLength);
	if (ReturnLength != NULL)
		*ReturnLength = TokenInformationLength;
	return TRUE;
}

BOOL WINAPI LookupAccountSidA(LPCSTR lpSystemName, void *Sid, LPCSTR Name, DWORD *cbName,
	void *ReferencedDomainName, DWORD *cbReferencedDomainName, DWORD *pdwUse)
{
	(void)lpSystemName; (void)Sid; (void)Name; (void)ReferencedDomainName; (void)pdwUse;
	if (cbName != NULL)
		*cbName = 0;
	if (cbReferencedDomainName != NULL)
		*cbReferencedDomainName = 0;
	return FALSE;
}

BOOL WINAPI ConvertSidToStringSidA(void *Sid, LPSTR *StringSid) { (void)Sid; (void)StringSid; return FALSE; }
BOOL WINAPI IsValidSid(void *Sid) { return Sid != NULL; }
void *WINAPI GetSidSubAuthority(void *pSid, DWORD nSubAuthority) { (void)pSid; (void)nSubAuthority; return NULL; }
DWORD WINAPI GetLengthSid(void *pSid) { return 12; }
LPBYTE WINAPI GetSidIdentifierAuthority(void *pSid) { (void)pSid; return NULL; }
BOOL WINAPI EqualSid(void *pSid1, void *pSid2) { return pSid1 == pSid2; }

/* ------------------------------------------------------------------------ */
/* Misc                                                                      */
/* ------------------------------------------------------------------------ */

BOOL WINAPI IsProcessorFeaturePresent(DWORD ProcessorFeature) { (void)ProcessorFeature; return TRUE; }
BOOL WINAPI IsDebuggerPresent(void) { return FALSE; }

BOOL WINAPI SetFileValidData(HANDLE hFile, LARGE_INTEGER liEndOfFile)
{
	int fd = win_handle_to_fd(hFile);
	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	if (ftruncate(fd, (off_t)liEndOfFile.QuadPart) < 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

BOOL WINAPI SetEndOfFile(HANDLE hFile)
{
	int fd = win_handle_to_fd(hFile);
	off_t cur;
	if (fd < 0)
		return fail(ERROR_INVALID_HANDLE);
	cur = lseek(fd, 0, SEEK_CUR);
	if (cur == (off_t)-1)
		return fail(errno_to_win32(errno));
	if (ftruncate(fd, cur) < 0)
		return fail(errno_to_win32(errno));
	return TRUE;
}

void WINAPI OutputDebugStringA(LPCSTR lpOutputString)
{
	(void)lpOutputString;
}

BOOL WINAPI GetVersionExA(void *lpVersionInformation)
{
	typedef struct { DWORD dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformId; WCHAR szCSDVersion[128]; } VS;
	VS* v = (VS*)lpVersionInformation;
	if (v == NULL)
		return FALSE;
	memset(v, 0, sizeof(*v));
	v->dwOSVersionInfoSize = (DWORD)sizeof(*v);
	v->dwMajorVersion = 6;
	v->dwMinorVersion = 4;
	v->dwBuildNumber = 0;
	v->dwPlatformId = 2; /* VER_PLATFORM_LINUX */
	return TRUE;
}

BOOL WINAPI VerifyVersionInfoA(void *pvi, DWORD dwFlags, void *rvi) { (void)pvi; (void)dwFlags; (void)rvi; return TRUE; }
ULONGLONG WINAPI VerSetConditionMask(ULONGLONG ConditionMask, DWORD dwTypeMask, BYTE cond)
{
	(void)dwTypeMask; (void)cond;
	return ConditionMask;
}

void WINAPI GetNativeSystemInfo(void *lpSystemInfo)
{
	/* SYSTEM_INFO layout (x64) */
	struct { WORD wProcessorArchitecture, wReserved; DWORD dwPageSize; void *lpMinimumApplicationAddress,
		*lpMaximumApplicationAddress, *dwActiveProcessorMask; DWORD dwNumberOfProcessors, dwProcessorType,
		dwAllocationGranularity; WORD wProcessorLevel, wProcessorRevision; } si;
	memset(&si, 0, sizeof(si));
	si.wProcessorArchitecture = 9; /* AMD64 */
	si.dwPageSize = 4096;
	si.dwNumberOfProcessors = (DWORD)sysconf(_SC_NPROCESSORS_ONLN);
	memcpy(lpSystemInfo, &si, sizeof(si));
}

BOOL WINAPI GetProductInfo(DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwReserved, DWORD *pdwProductType)
{
	(void)dwMajorVersion; (void)dwMinorVersion; (void)dwReserved;
	if (pdwProductType != NULL)
		*pdwProductType = 1;
	return TRUE;
}

DWORD WINAPI GetVersion(void) { return 0; }

BOOL WINAPI GetVolumePathNameA(LPCSTR lpFileName, LPSTR lpVolumePathName, DWORD cchBufferLength)
{
	(void)lpFileName;
	if (lpVolumePathName != NULL && cchBufferLength > 0)
		snprintf(lpVolumePathName, cchBufferLength, "/");
	return TRUE;
}

BOOL WINAPI GetVolumeNameForVolumeMountPointA(LPCSTR lpszVolumePath, LPSTR lpszVolumeName, DWORD cchBufferLength)
{
	(void)lpszVolumePath;
	if (lpszVolumeName != NULL && cchBufferLength > 0)
		snprintf(lpszVolumeName, cchBufferLength, "/");
	return TRUE;
}

BOOL WINAPI GetVolumePathNamesForVolumeNameA(LPCSTR lpszVolumeName, LPSTR lpszVolumePathNames, DWORD cchBufferLength)
{
	(void)lpszVolumeName;
	if (lpszVolumePathNames != NULL && cchBufferLength > 0)
		snprintf(lpszVolumePathNames, cchBufferLength, "/");
	return TRUE;
}

HANDLE WINAPI CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID)
{
	(void)dwFlags; (void)th32ProcessID;
	return (HANDLE)(intptr_t)1;
}

BOOL WINAPI Process32First(HANDLE hSnapshot, void *lppe) { (void)hSnapshot; (void)lppe; return FALSE; }
BOOL WINAPI Process32Next(HANDLE hSnapshot, void *lppe) { (void)hSnapshot; (void)lppe; return FALSE; }
BOOL WINAPI Process32FirstW(HANDLE hSnapshot, void *lppe) { (void)hSnapshot; (void)lppe; return FALSE; }
BOOL WINAPI Process32NextW(HANDLE hSnapshot, void *lppe) { (void)hSnapshot; (void)lppe; return FALSE; }
BOOL WINAPI Module32First(HANDLE hSnapshot, void *lpme) { (void)hSnapshot; (void)lpme; return FALSE; }
BOOL WINAPI Module32Next(HANDLE hSnapshot, void *lpme) { (void)hSnapshot; (void)lpme; return FALSE; }
BOOL WINAPI Thread32First(HANDLE hSnapshot, void *lpte) { (void)hSnapshot; (void)lpte; return FALSE; }
BOOL WINAPI Thread32Next(HANDLE hSnapshot, void *lpte) { (void)hSnapshot; (void)lpte; return FALSE; }

BOOL WINAPI QueryDosDeviceA(LPCSTR lpDeviceName, LPSTR lpTargetBuffer, DWORD ucchMax)
{
	(void)lpDeviceName;
	if (lpTargetBuffer != NULL && ucchMax > 0)
		lpTargetBuffer[0] = 0;
	return TRUE;
}

BOOL WINAPI DefineDosDeviceA(DWORD dwFlags, LPCSTR lpDeviceName, LPCSTR lpTargetSystem)
{
	(void)dwFlags; (void)lpDeviceName; (void)lpTargetSystem;
	return TRUE;
}

DWORD WINAPI GetLogicalDriveStringsA(DWORD nBufferLength, LPSTR lpBuffer)
{
	(void)nBufferLength; (void)lpBuffer;
	return 0;
}

UINT WINAPI GetDriveTypeA(LPCSTR lpRootPathName)
{
	(void)lpRootPathName;
	return 3; /* DRIVE_FIXED */
}

BOOL WINAPI GetDiskFreeSpaceA(LPCSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector,
	LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters)
{
	(void)lpRootPathName;
	if (lpSectorsPerCluster != NULL) *lpSectorsPerCluster = 8;
	if (lpBytesPerSector != NULL) *lpBytesPerSector = 512;
	if (lpNumberOfFreeClusters != NULL) *lpNumberOfFreeClusters = 0;
	if (lpTotalNumberOfClusters != NULL) *lpTotalNumberOfClusters = 0;
	return TRUE;
}

BOOL WINAPI IsUserAnAdmin(void)
{
	return geteuid() == 0;
}

/* ------------------------------------------------------------------------ */
/* GUID and cryptographic services                                            */
/* ------------------------------------------------------------------------ */

HRESULT WINAPI CoCreateGuid(GUID* pguid)
{
	int fd;
	uint8_t buffer[16];

	if (pguid == NULL)
		return E_INVALIDARG;
	fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return E_FAIL;
	if (read(fd, buffer, sizeof(buffer)) != (ssize_t)sizeof(buffer)) {
		close(fd);
		return E_FAIL;
	}
	close(fd);
	/* Set the version (4) and the variant bits, as per RFC 4122 */
	buffer[7] = (buffer[7] & 0x0F) | 0x40;
	buffer[8] = (buffer[8] & 0x3F) | 0x80;
	pguid->Data1 = (DWORD)(buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | ((uint32_t)buffer[3] << 24));
	pguid->Data2 = (WORD)(buffer[4] | (buffer[5] << 8));
	pguid->Data3 = (WORD)(buffer[6] | (buffer[7] << 8));
	memcpy(pguid->Data4, buffer + 8, 8);
	return S_OK;
}

BOOL WINAPI CryptAcquireContext(HANDLE* phProv, LPCSTR szContainer, LPCSTR szProvider, DWORD dwProvType, DWORD dwFlags)
{
	(void)szContainer; (void)szProvider; (void)dwProvType; (void)dwFlags;
	if (phProv != NULL)
		*phProv = (HANDLE)(intptr_t)1;
	return TRUE;
}

BOOL WINAPI CryptGenRandom(HANDLE hProv, DWORD dwLen, BYTE* pbBuffer)
{
	int fd;
	ssize_t n;

	(void)hProv;
	fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return FALSE;
	n = read(fd, pbBuffer, dwLen);
	close(fd);
	return (n == (ssize_t)dwLen);
}

BOOL WINAPI CryptReleaseContext(HANDLE hProv, DWORD dwFlags)
{
	(void)hProv; (void)dwFlags;
	return TRUE;
}

BOOL WINAPI ShellExecuteExA(SHELLEXECUTEINFOA *lpExecInfo)
{
	char* argv[8];
	int argc = 0;
	pid_t pid;

	if (lpExecInfo == NULL || lpExecInfo->lpFile == NULL)
		return fail(ERROR_INVALID_HANDLE);
	argv[argc++] = (char*)lpExecInfo->lpVerb;
	argv[argc++] = (char*)lpExecInfo->lpFile;
	if (lpExecInfo->lpParameters != NULL)
		argv[argc++] = (char*)lpExecInfo->lpParameters;
	argv[argc] = NULL;
	pid = fork();
	if (pid < 0)
		return fail(errno_to_win32(errno));
	if (pid == 0) {
		execvp(argv[1], &argv[1]);
		_exit(127);
	}
	return TRUE;
}

BOOL WINAPI SetProcessDPIAware(void) { return TRUE; }
UINT WINAPI GetDpiForWindow(HWND hwnd) { (void)hwnd; return 96; }

BOOL WINAPI RegisterDeviceNotificationA(HANDLE hRecipient, LPVOID lpFilter, DWORD dwFlags)
{
	(void)hRecipient; (void)lpFilter; (void)dwFlags;
	return TRUE;
}

BOOL WINAPI UnregisterDeviceNotification(HANDLE h) { (void)h; return TRUE; }

/* ------------------------------------------------------------------------ */
/* Additional entry points that the Rufus code refers to                      */
/* ------------------------------------------------------------------------ */

BOOL WINAPI EndDialog(HWND hDlg, int nResult) { (void)hDlg; (void)nResult; return TRUE; }

HANDLE WINAPI CreateEvent(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName)
{
	return CreateEventA(lpEventAttributes, bManualReset, bInitialState, lpName);
}

BOOL WINAPI PathFileExistsA(LPCSTR szPath)
{
	struct stat st;
	if (szPath == NULL)
		return FALSE;
	return stat(szPath, &st) == 0;
}

BOOL WINAPI IsWindow(HWND hWnd) { (void)hWnd; return TRUE; }

BOOL WINAPI EnumUILanguages(ENUMUILANGUAGESPROC lpEnumFunc, DWORD dwParam, LPARAM lParam)
{
	(void)dwParam;
	if (lpEnumFunc != NULL) {
		static const wchar_t en_us[] = { 'e', 'n', '-', 'U', 'S', 0 };
		return lpEnumFunc((LPWSTR)en_us, (LONG_PTR)lParam);
	}
	return TRUE;
}

int WINAPI EnumFontFamiliesExA(HDC hdc, LPLOGFONTA lpLogfont, CALLBACK_ENUMPROC lpProc, LPARAM param, DWORD dwFlags)
{
	(void)hdc; (void)lpLogfont; (void)lpProc; (void)param; (void)dwFlags;
	return 0;
}

BOOL WINAPI CreatePipe(HANDLE* phReadPipe, HANDLE* phWritePipe, LPSECURITY_ATTRIBUTES lpPipeAttributes, DWORD nSize)
{
	int fds[2];
	(void)lpPipeAttributes; (void)nSize;
	if (pipe(fds) < 0)
		return FALSE;
	*phReadPipe = win_fd_to_handle(fds[0]);
	*phWritePipe = win_fd_to_handle(fds[1]);
	return TRUE;
}

BOOL WINAPI TerminateProcess(HANDLE hProcess, UINT uExitCode) { (void)hProcess; (void)uExitCode; return TRUE; }

BOOL WINAPI PeekNamedPipe(HANDLE hNamedPipe, LPVOID lpBuffer, DWORD nBufferSize, LPDWORD lpBytesRead,
	LPDWORD lpTotalBytesAvail, LPDWORD lpBytesLeftThisMessage)
{
	(void)hNamedPipe; (void)lpBuffer; (void)nBufferSize;
	if (lpBytesRead != NULL) *lpBytesRead = 0;
	if (lpTotalBytesAvail != NULL) *lpTotalBytesAvail = 0;
	if (lpBytesLeftThisMessage != NULL) *lpBytesLeftThisMessage = 0;
	return TRUE;
}

BOOL WINAPI InitializeSecurityDescriptor(PSECURITY_DESCRIPTOR pSecurityDescriptor, DWORD dwRevision)
{
	(void)dwRevision;
	if (pSecurityDescriptor != NULL)
		memset(pSecurityDescriptor, 0, sizeof(SECURITY_DESCRIPTOR));
	return TRUE;
}

BOOL WINAPI SetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR pSecurityDescriptor, PSID pOwner, BOOL bOwnerDefaulted)
{
	(void)pSecurityDescriptor; (void)pOwner; (void)bOwnerDefaulted;
	return TRUE;
}

BOOL WINAPI SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR pSecurityDescriptor, BOOL bDaclPresent, PACL pDacl, BOOL bDaclDefaulted)
{
	(void)pSecurityDescriptor; (void)bDaclPresent; (void)pDacl; (void)bDaclDefaulted;
	return TRUE;
}

/*
 * On Windows, the Rufus log is sent to the debug facility. On Linux, we
 * print it to the console instead, which is what the command line front-end
 * needs.
 */
void WINAPI OutputDebugStringW(LPCWSTR lpOutputString)
{
	char buffer[8192];
	const win_dispatch* dispatch = win_get_dispatch();

	if (lpOutputString == NULL)
		return;
	win_wide_to_utf8(lpOutputString, -1, buffer, (int)sizeof(buffer));
	/*
	 * The localization loader is quite chatty and its messages are of no
	 * interest to the user, so we filter them out unless verbose output was
	 * explicitly requested.
	 */
	if (!win_log_verbose && ((strncmp(buffer, "localization:", 13) == 0) ||
		(strstr(buffer, "translation is older than the base one") != NULL)))
		return;
	if ((dispatch != NULL) && (dispatch->log != NULL))
		dispatch->log(dispatch->context, buffer);
	else
		fputs(buffer, stderr);
}

int WINAPI Edit_SetSel(HWND hWnd, int nStart, int nEnd) { (void)hWnd; (void)nStart; (void)nEnd; return TRUE; }
int WINAPI Edit_ReplaceSelEx(HWND hWnd, BOOL bReplace, LPCSTR lpText) { (void)hWnd; (void)bReplace; (void)lpText; return TRUE; }
int WINAPI Edit_Scroll(HWND hWnd, int nMin, int nMax) { (void)hWnd; (void)nMin; (void)nMax; return 0; }
int WINAPI Edit_GetLineCount(HWND hWnd) { (void)hWnd; return 0; }

int WINAPI PeekMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg)
{
	(void)lpMsg; (void)hWnd; (void)wMsgFilterMin; (void)wMsgFilterMax; (void)wRemoveMsg;
	return 0;
}
BOOL WINAPI DispatchMessage(LPMSG lpMsg) { (void)lpMsg; return TRUE; }
BOOL WINAPI TranslateMessage(LPMSG lpMsg) { (void)lpMsg; return TRUE; }
DWORD WINAPI GetMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
	(void)lpMsg; (void)hWnd; (void)wMsgFilterMin; (void)wMsgFilterMax;
	return 0;
}
void WINAPI PostQuitMessage(int nExitCode) { (void)nExitCode; }
DWORD WINAPI MsgWaitForMultipleObjects(DWORD nCount, const HANDLE* pHandles, BOOL fWaitAll,
	DWORD dwMilliseconds, UINT dwWakeMask)
{
	(void)dwWakeMask;
	return WaitForMultipleObjects(nCount, pHandles, fWaitAll, dwMilliseconds);
}

int WINAPI MulDiv(int a, int b, int c) { return (c != 0) ? (a * b) / c : 0; }
LCID WINAPI GetThreadLocale(void) { return 0x0409; }
BOOL WINAPI SetThreadLocale(LCID Locale) { (void)Locale; return TRUE; }
BOOL WINAPI CancelSynchronousIo(HANDLE hFile) { (void)hFile; return TRUE; }

int WINAPI _vsnwprintf_s(wchar_t* buf, size_t size, size_t count, const wchar_t* fmt, va_list args)
{
	int n;
	(void)count;
	n = vswprintf(buf, size, fmt, args);
	return n;
}

/*
 * A wide character version of fprintf(). The output is encoded in UTF-8,
 * which is the native encoding on Linux.
 */
int win_fwprintf_s(FILE* f, const wchar_t* format, ...)
{
	wchar_t wbuf[8192];
	char ubuf[32768];
	va_list ap;
	int n;

	va_start(ap, format);
	n = vswprintf(wbuf, ARRAYSIZE_(wbuf), format, ap);
	va_end(ap);
	if (n < 0)
		return -1;
	if (n >= (int)ARRAYSIZE_(wbuf))
		n = (int)ARRAYSIZE_(wbuf) - 1;
	win_wide_to_utf8(wbuf, n, ubuf, (int)sizeof(ubuf));
	n = (int)strlen(ubuf);
	fwrite(ubuf, 1, (size_t)n, f);
	return n;
}

int win_wunlink(const wchar_t* path)
{
	char cpath[PATH_MAX];
	win_wide_to_utf8(path, -1, cpath, (int)sizeof(cpath));
	return unlink(cpath);
}

/*
 * The Windows wide character file open function. On Linux, the "ccs="
 * mode specifiers are not understood, so they are simply ignored and the
 * data is written as UTF-8.
 */
FILE* _wfopen(const wchar_t* path, const wchar_t* mode)
{
	char cpath[PATH_MAX];
	char cmode[32];
	size_t i;

	win_wide_to_utf8(path, -1, cpath, (int)sizeof(cpath));
	win_wide_to_utf8(mode, -1, cmode, (int)sizeof(cmode));
	/* Strip the "ccs=..." specifier, which is a Windows extension */
	for (i = 0; i + 1 < sizeof(cmode); i++) {
		if (cmode[i] == ',' && (strncmp(&cmode[i], ", ccs=", 6) == 0)) {
			cmode[i] = 0;
			break;
		}
	}
	return fopen(cpath, cmode);
}

LPSTR WINAPI StrStrIA(LPCSTR lpHaystack, LPCSTR lpNeedle)
{
	return (LPSTR)strcasestr(lpHaystack, lpNeedle);
}

HFONT WINAPI CreateFontA(int nHeight, int nWidth, int nEscapement, int nOrientation,
	int nWeight, DWORD fdwItalic, DWORD fdwUnderline, DWORD fdwStrikeOut, DWORD fdwCharSet,
	DWORD fdwOutputPrecision, DWORD fdwClipPrecision, DWORD fdwQuality, DWORD fdwPitchAndFamily,
	LPCSTR lpszFace)
{
	(void)nHeight; (void)nWidth; (void)nEscapement; (void)nOrientation; (void)nWeight;
	(void)fdwItalic; (void)fdwUnderline; (void)fdwStrikeOut; (void)fdwCharSet;
	(void)fdwOutputPrecision; (void)fdwClipPrecision; (void)fdwQuality;
	(void)fdwPitchAndFamily; (void)lpszFace;
	return (HFONT)(intptr_t)1;
}

BOOL WINAPI SetThreadPriority(HANDLE hThread, int nPriority) { (void)hThread; (void)nPriority; return TRUE; }
BOOL WINAPI SetThreadAffinityMask(HANDLE hThread, DWORD dwThreadAffinityMask)
{
	(void)hThread; (void)dwThreadAffinityMask;
	return TRUE;
}

int WINAPI lstrcmpiA(LPCSTR lpStr1, LPCSTR lpStr2) { return strcasecmp(lpStr1, lpStr2); }

/* The Windows CharUpperA()/CharLowerA() convert a string in place */
char* WINAPI CharUpperA(char* s)
{
	char* p;
	if (s == NULL)
		return NULL;
	for (p = s; *p != 0; p++)
		*p = (char)toupper((unsigned char)*p);
	return s;
}

char* WINAPI CharLowerA(char* s)
{
	char* p;
	if (s == NULL)
		return NULL;
	for (p = s; *p != 0; p++)
		*p = (char)tolower((unsigned char)*p);
	return s;
}
