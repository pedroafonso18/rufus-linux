/*
 * Rufus: The Reliable USB Formatting Utility
 * Win32 compatibility layer for POSIX (Linux) platforms.
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
 * This header provides just enough of the Win32 API surface for the Rufus
 * code base to build and run on Linux. It is *not* a general purpose
 * Win32 emulation layer: it only implements the subset of the API that the
 * (platform independent) Rufus logic actually relies upon.
 *
 * Design notes:
 *  - HANDLE is an opaque pointer. For files, it wraps a POSIX file descriptor.
 *  - Strings are handled as UTF-8 throughout. The 'W' (UTF-16) entry points
 *    convert to/from UTF-16 on the fly.
 *  - Errors are stored in thread-local storage, mirroring SetLastError().
 *  - GUI entry points are either no-ops or routed to a pluggable callback
 *    (see win_compat.c) so that the same code can drive a GTK front-end.
 */

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <wchar.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Basic types                                                              */
/* ------------------------------------------------------------------------ */

typedef uint8_t BYTE;
typedef uint8_t UCHAR;
typedef uint8_t *PBYTE;
typedef uint8_t *LPBYTE;
typedef char CHAR;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef wchar_t WCHAR;
typedef wchar_t *LPWSTR;
typedef const wchar_t *PCWSTR;
typedef const wchar_t *LPCWSTR;
typedef uint16_t WORD;
typedef uint16_t *LPWORD;
typedef uint32_t DWORD;
typedef uint32_t *LPDWORD;
typedef uint32_t ULONG;
typedef ULONG *PULONG;
typedef int32_t LONG;
typedef int32_t *LPLONG;
typedef int32_t *PLONG;
typedef uint64_t ULONGLONG;
typedef uint64_t ULONG64;
typedef int64_t LONGLONG;
typedef uint32_t REGSAM;
typedef uint64_t UINT_PTR;
typedef int64_t INT_PTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t SIZE_T;
typedef uintptr_t DWORD_PTR;
typedef void *PVOID;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef int BOOL;
typedef BOOL *LPBOOL;
typedef uint8_t BOOLEAN;
typedef uint8_t UINT8;
typedef uint8_t INT8;
typedef uint16_t UINT16;
typedef uint16_t INT16;
typedef uint32_t UINT32;
typedef uint32_t INT32;
typedef uint64_t UINT64;
typedef int64_t INT64;
typedef uint32_t COLORREF;
typedef void *HTHEME;
typedef void *HPEN;
typedef void *HBRUSH2;
typedef uint32_t *PUINT32;
typedef uint16_t *PUINT16;
typedef uint8_t *PUINT8;
typedef uint8_t BOOL8;
typedef int INT;
typedef unsigned int UINT;
typedef short SHORT;
typedef unsigned short USHORT;
typedef int32_t HRESULT;
typedef uint32_t LCID;
typedef unsigned char CB;
typedef void *HANDLE;
typedef HANDLE *LPHANDLE;
typedef void *HMODULE;
typedef void *HINSTANCE;
typedef void *HWND;
typedef void *HDC;
typedef void *HGDIOBJ;
typedef void *HMENU;
typedef void *HICON;
typedef void *HCURSOR;
typedef void *HBRUSH;
typedef void *HFONT;
typedef void *HBITMAP;
typedef void *HMONITOR;
typedef void *HIMAGELIST;
typedef void *HHOOK;
typedef void *HKEY;
typedef void *HIMC;
typedef void *PSID;
typedef void *PACL;
typedef struct _SECURITY_DESCRIPTOR SECURITY_DESCRIPTOR, *PSECURITY_DESCRIPTOR;
struct _SECURITY_DESCRIPTOR {
	BYTE Revision;
	BYTE Sbz1;
	WORD Control;
	PSID Owner;
	PSID Group;
	PACL Dacl;
	PACL Sacl;
};
#define SECURITY_DESCRIPTOR_REVISION 1
typedef struct _STARTUPINFO STARTUPINFO, *LPSTARTUPINFOA;
struct _STARTUPINFO {
	DWORD cb;
	LPSTR lpReserved;
	LPSTR lpDesktop;
	LPSTR lpTitle;
	DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
	WORD wShowWindow;
	WORD cbReserved2;
	LPBYTE lpReserved2;
	HANDLE hStdInput, hStdOutput, hStdError;
};
typedef STARTUPINFO STARTUPINFOA;
#define STARTF_USESTDHANDLES 0x00000100
#define STARTF_USESHOWWINDOW 0x00000001
#define STARTF_TITLEISAPPID 0x00001000
#define STARTF_PREVENTPINNING 0x00002000
#define STARTF_FORCEONFEEDBACK 0x00004000
#define CREATE_NO_WINDOW 0x08000000
#define CREATE_NEW_CONSOLE 0x00000010
#define DETACHED_PROCESS 0x00000008
typedef uint64_t WPARAM;
typedef int64_t LPARAM;
typedef int64_t LRESULT;
typedef int64_t LONG_PTR;
typedef void (*LPTHREAD_START_ROUTINE)(LPVOID);
typedef int (*DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL (*WNDENUMPROC)(HWND, LPARAM);
typedef int (*PROC)(void);
typedef void VOID;
typedef int (*FARPROC)(void);
typedef struct _HRSRC { void* ptr; } *HRSRC;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFF)
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_HANDLE_EOF 38
#define ERROR_NOT_SUPPORTED 50
#define ERROR_IO_PENDING 997
#define ERROR_SECTOR_NOT_FOUND 1200
#define ERROR_NO_MORE_ITEMS 259
#define ERROR_GEN_FAILURE 31
#define ERROR_CANCELLED 1223
#define ERROR_ALREADY_EXISTS 183
#define ERROR_PRIVILEGE_NOT_HELD 1314
#define ERROR_BUSY 170
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define WAIT_FAILED 0xFFFFFFFF
#define INFINITE 0xFFFFFFFF
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define GENERIC_EXECUTE 0x20000000
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004
#define FILE_READ_DATA 0x0001
#define FILE_WRITE_DATA 0x0002
#define FILE_APPEND_DATA 0x0004
#define FILE_READ_ATTRIBUTES 0x0080
#define FILE_WRITE_ATTRIBUTES 0x0100
#define FILE_DELETE_ON_CLOSE 0x00001000
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FILE_FLAG_NO_BUFFERING 0x20000000
#define FILE_FLAG_WRITE_THROUGH 0x80000000
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_SYSTEM 0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define DUPLICATE_SAME_ACCESS 0x00000002
#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)
#define _UI_NONE 0
#define BST_UNCHECKED 0
#define BST_CHECKED 1
#define BST_INDETERMINATE 2
#define MB_OK 0x00000000
#define MB_OKCANCEL 0x00000001
#define MB_YESNO 0x00000004
#define MB_YESNOCANCEL 0x00000003
#define MB_RETRYCANCEL 0x00000005
#define MB_ICONERROR 0x00000010
#define MB_ICONQUESTION 0x00000020
#define MB_ICONWARNING 0x00000030
#define MB_ICONINFORMATION 0x00000040
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7
#define IDCLOSE 8
#define IDHELP 9
#define S_OK ((HRESULT)0x00000000L)
#define S_FALSE ((HRESULT)0x00000001L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define SCODE_MASK(c) ((unsigned long)(c) & 0x1FFFFF)
#define SCODE_FACILITY(c) ((SCODE_MASK(c) >> 19) & 0x1FF)
#define SCODE_CODE(c) ((unsigned long)(c) & 0xFFFFF)
#define HRESULT_CODE(hr) SCODE_CODE(hr)
#define IS_ERROR(hr) (((HRESULT)(hr)) < 0)
#define S_OK_RESULT(hr) (((HRESULT)(hr)) >= 0)
#define CF_TEXT 0x0001
#define CF_UNICODETEXT 0x0002
#define DBT_DEVNODES_CHANGED 0x00000007
#define DBT_DEVICEARRIVAL 0x80000000
#define DBT_DEVICEREMOVECOMPLETE 0x80000004
#define WM_APP 0x8000
#define WM_USER 0x0400
#define WM_TIMER 0x0113
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_COMMAND 0x0111
#define WM_INITDIALOG 0x0110
#define WM_NOTIFY 0x004E
#define WM_DEVICECHANGE 0x0219
#define WM_CTLCOLORSTATIC 0x0138
#define SB_SETTEXTW 0x040C
#define SBT_OWNERDRAW 0x000D
#define WM_SETFONT 0x0030
#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001
#define PM_NOYIELD 0x0002
#define QS_ALLINPUT 0x00FF
#define QS_ALLPOSTMESSAGE 0x0100
#define QS_HARDWAREINPUT 0x0000
#define WM_NEXTDLGCTL 0x0028
#define WM_NCDESTROY 0x0082
#define WM_NCHITTEST 0x0084
#define WM_ERASEBKGND 0x0014
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_KEYDOWN 0x0100
#define WM_DESTROY 0x0002
#define WM_PAINT 0x000F
#define DT_CALCRECT 0x00000400
#define DT_SINGLELINE 0x00000020
#define DT_NOPREFIX 0x00000800
#define LOGPIXELSY 90
#define FW_NORMAL 400
#define DEFAULT_CHARSET 1
#define PROOF_QUALITY 2
#define CLEARTYPE_QUALITY 5
#define STATUS_TIMEOUT 0x00000102
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define ANTIALIASED_QUALITY 4
#define WS_OVERLAPPED 0x00000000
#define WS_CAPTION 0x00C00000
#define WS_SYSMENU 0x00080000
#define WS_CHILD 0x40000000
#define WS_VISIBLE 0x10000000
#define WS_DISABLED 0x08000000
#define WS_TABSTOP 0x00010000
#define ES_LEFT 0x0000
#define ES_CENTER 0x0001
#define ES_RIGHT 0x0002
#define ES_MULTILINE 0x0004
#define ES_AUTOVSCROLL 0x0040
#define ES_READONLY 0x0800
#define BS_PUSHBUTTON 0x00000000
#define BS_AUTOCHECKBOX 0x00000003
#define BS_AUTORADIOBUTTON 0x00000009
#define BS_GROUPBOX 0x00000007
#define CBS_DROPDOWNLIST 0x00000003
#define CBS_SIMPLE 0x0001
#define CBS_DROPDOWN 0x0002
#define LVM_FIRST 0x1000
#define LVM_GETITEMCOUNT (LVM_FIRST + 4)
#define LVM_GETITEMTEXTW (LVM_FIRST + 115)
#define TB_GETIMAGELIST 0x0417
#define PBM_SETRANGE32 0x0406
#define PBM_GETPOS 0x0408
#define PBM_SETPOS 0x0402
#define PBM_SETMARQUEE 0x040A
#define PBS_MARQUEE 0x0008
#define EM_SETSEL 0x00B1
#define EM_REPLACESEL 0x00C2
#define CB_GETCOUNT 0x0146
#define CB_ADDSTRING 0x0143
#define CB_INSERTSTRING 0x0142
#define CB_GETCURSEL 0x0147
#define CB_SETCURSEL 0x014E
#define CB_GETITEMDATA 0x0150
#define CB_SETITEMDATA 0x0151
#define LVM_SETITEMTEXTW 0x1026
#define SNDMSG(hwnd, msg, wp, lp) SendMessageA((hwnd), (msg), (wp), (lp))
#define CP_UTF8 65001
#define CP_ACP 0
#define CP_OEMCP 1
#define LPCITEMIDLIST const void*
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_GETTEXTLENGTH 0x000E
#define SW_SHOW 5
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_SHOWMINIMIZED 2
#define GWLP_HINSTANCE ((LONG_PTR)(-24))
#define GWLP_USERDATA ((LONG_PTR)(-21))
#define GWL_STYLE ((LONG_PTR)(-16))
#define GWL_EXSTYLE ((LONG_PTR)(-20))
#define IMAGE_BITMAP 0
#define IMAGE_ICON 1
#define LR_LOADFROMFILE 0x00000010
#define LR_DEFAULTSIZE 0x00000040
#define SPI_GETICONTITLELOGFONT 0x0000001F
#define SHDT_FOLDER 0x0000000A
#define SHGFP_TYPE_CURRENT 0
#define FOF_NOCONFIRMATION 0x00000010
#define FOF_SILENT 0x00000004
#define FOF_NOERRORUI 0x00000400
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_COPY_ALLOWED 0x00000002
#define MOVEFILE_DELAY_UNTIL_REBOOT 0x00000004
#define SYMBOLIC_LINK_FLAG_DIRECTORY 0x00000002
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x00000002

/* ------------------------------------------------------------------------ */
/* Windows Error Codes (subset)                                            */
/* ------------------------------------------------------------------------ */

#define ERROR_SUCCESS_CODE 0
#define ERROR_FILE_EXISTS 80
#define ERROR_MORE_DATA 234
#define ERROR_NO_MORE_FILES 18
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_BAD_LENGTH 24
#define ERROR_INVALID_NAME 123
#define ERROR_DIR_NOT_EMPTY 145
#define ERROR_WRITE_PROTECT 19
#define ERROR_SEM_TIMEOUT 121
#define ERROR_BAD_COMMAND 22
#define ERROR_BAD_NETPATH 53
#define ERROR_NETWORK_BUSY 54
#define ERROR_DEV_NOT_EXIST 55
#define ERROR_SHARING_VIOLATION 32
#define ERROR_LOCK_VIOLATION 33
#define ERROR_OPERATION_ABORTED 995
#define ERROR_UNHANDLED_EXCEPTION 0xE0434F4D
#define ERROR_IO_INCOMPLETE 996
#define ERROR_STRUCTURE_LENGTH_MISMATCH 178
#define ERROR_MOD_NOT_FOUND 126
#define ERROR_TOO_MANY_OPEN_FILES 4
#define ERROR_BAD_EXE_FORMAT 193
#define ERROR_NO_SYSTEM_RESOURCES 1450
#define ERROR_SERVICE_DOES_NOT_EXIST 1060
#define ERROR_CALL_NOT_IMPLEMENTED 120
#define ERROR_INVALID_FUNCTION 1
#define ERROR_DATABASE_FAILURE 1533
#define ERROR_INVALID_DRIVE 15
#define ERROR_NO_MORE_FILES_END 18
#define ERROR_DISK_FULL 112
#define ERROR_BAD_UNIT 6
#define ERROR_INVALID_HANDLE_VALUE 6
#define ERROR_NOT_FOUND 1168
#define ERROR_CANCELLED_OP 1223
#define ERROR_CANNOT_COPY 1005
#define ERROR_DEVICE_IN_USE 32
#define ERROR_FILE_CORRUPT 1392
#define ERROR_INSTALL_FAILURE 1603
#define ERROR_INVALID_DATA 13
#define ERROR_INVALID_IMAGE_HASH 921
#define ERROR_INVALID_PARAMETER 87
#define ERROR_LABEL_TOO_LONG 206
#define ERROR_MR_MID_NOT_FOUND 317
#define ERROR_MUI_FILE_NOT_FOUND 1902
#define ERROR_MUI_FILE_NOT_LOADED 1913
#define ERROR_NO_MEDIA_IN_DRIVE 433
#define ERROR_NOT_ALL_ASSIGNED 1301
#define ERROR_NOT_READY 21
#define ERROR_OPEN_FAILED 110
#define ERROR_PARTITION_FAILURE 1007
#define ERROR_READ_FAULT 30
#define ERROR_TIMEOUT 1460
#define ERROR_WRITE_FAULT 29
#define ERROR_OBJECT_IN_LIST 1201
#define ERROR_INVALID_BLOCK 1006

/* GDI types used by the icon extraction code */
typedef void *HGLOBAL;
typedef struct {
	DWORD biSize;
	LONG biWidth;
	LONG biHeight;
	WORD biPlanes;
	WORD biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG biXPelsPerMeter;
	LONG biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
} BITMAPINFOHEADER;
typedef struct {
	BYTE rgbBlue;
	BYTE rgbGreen;
	BYTE rgbRed;
	BYTE rgbReserved;
} RGBQUAD;

/* Locale constants and helpers */
#define KL_NAMELENGTH 9
#define PRIMARYLANGID(lgid) ((WORD)(lgid) & 0x3FF)
#define LOCALE_IDEFAULTCODEPAGE 0x1000
#define LOCALE_RETURN_NUMBER 0x0100
#define LOCALE_USER_DEFAULT 0x400
#define LOCALE_SYSTEM_DEFAULT 0x800
typedef WORD LANGID;
#define LANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#define SUBLANG_NEUTRAL 0x00
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))
#define PRIMARYLANGID_N(lgid) MAKELANGID(lgid, SUBLANG_NEUTRAL)

/* Access token information classes */
#define TokenUser 1
#define TokenGroups 2
#define TokenPrivileges 3
#define TokenOwner 4
#define TokenPrimaryGroup 5
#define TokenDefaultDacl 6
#define TokenSource 7
#define TokenType 8
#define TokenImpersonationLevel 9
#define TokenStatistics 10
#define TokenElevationType 18
#define TokenElevation 20
#define TokenIntegrityLevel 25

/* ------------------------------------------------------------------------ */
/* Structures                                                               */
/* ------------------------------------------------------------------------ */

typedef union _LARGE_INTEGER {
	struct {
		DWORD LowPart;
		LONG HighPart;
	};
	long long QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
	struct {
		DWORD LowPart;
		DWORD HighPart;
	};
	unsigned long long QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _FILETIME {
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME, *LPFILETIME;

typedef struct _SECURITY_ATTRIBUTES {
	DWORD nLength;
	void *lpSecurityDescriptor;
	BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _OVERLAPPED {
	ULONG_PTR Internal[2];
	ULONG_PTR Offset;
	ULONG_PTR OffsetHigh;
	HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef struct _WIN32_FIND_DATAA {
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD dwReserved0;
	DWORD dwReserved1;
	char cFileName[MAX_PATH];
	char cAlternateFileName[14];
} WIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;

typedef struct _WIN32_FIND_DATAW {
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD dwReserved0;
	DWORD dwReserved1;
	WCHAR cFileName[MAX_PATH];
	WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;

typedef struct _POINT {
	LONG x, y;
} POINT, *LPPOINT, *PPOINT;

typedef struct _MSG {
	HWND hwnd;
	UINT message;
	WPARAM wParam;
	LPARAM lParam;
	DWORD time;
	POINT pt;
} MSG, *LPMSG, *PMSG;

typedef struct _RECT {
	LONG left, top, right, bottom;
} RECT, *LPRECT, *PRECT;

typedef struct _SIZE {
	LONG cx, cy;
} SIZE, *LPSIZE;

typedef struct _GUID {
	DWORD Data1;
	WORD Data2;
	WORD Data3;
	BYTE Data4[8];
} GUID, *LPGUID;

typedef GUID IID;
typedef GUID CLSID;

typedef struct _OSVERSIONINFOA {
	DWORD dwOSVersionInfoSize;
	DWORD dwMajorVersion;
	DWORD dwMinorVersion;
	DWORD dwBuildNumber;
	DWORD dwPlatformId;
	char szCSDVersion[128];
} OSVERSIONINFOA, *POSVERSIONINFOA, *LPOSVERSIONINFOA;

typedef struct _OSVERSIONINFOEXA {
	DWORD dwOSVersionInfoSize;
	DWORD dwMajorVersion;
	DWORD dwMinorVersion;
	DWORD dwBuildNumber;
	DWORD dwPlatformId;
	char szCSDVersion[128];
	WORD wServicePackMajor;
	WORD wServicePackMinor;
	WORD wSuiteMask;
	BYTE wProductType;
	BYTE wReserved;
} OSVERSIONINFOEXA, *POSVERSIONINFOEXA, *LPOSVERSIONINFOEXA;

typedef struct _UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	wchar_t* Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _SYSTEMTIME {
	WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *LPSYSTEMTIME;

typedef struct _LIST_ENTRY {
	struct _LIST_ENTRY *Flink;
	struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

typedef struct _PROCESS_INFORMATION {
	HANDLE hProcess;
	HANDLE hThread;
	DWORD dwProcessId;
	DWORD dwThreadId;
} PROCESS_INFORMATION, *LPPROCESS_INFORMATION;



typedef struct _SHELLEXECUTEINFOA {
	DWORD cbSize;
	ULONG fMask;
	void *hwnd;
	LPCSTR lpVerb;
	LPCSTR lpFile;
	LPCSTR lpParameters;
	LPCSTR lpDirectory;
	int nShow;
	int hInstApp;
	void *lpIDList;
	LPCSTR lpClass;
	HKEY hkeyClass;
	DWORD dwHotKey;
	union {
		HANDLE hIcon;
		HANDLE hMonitor;
	} u;
	void *hProcess;
} SHELLEXECUTEINFOA, *LSHELLEXECUTEINFOA;

typedef struct _OPENFILENAMEA {
	DWORD lStructSize;
	HWND hwndOwner;
	HINSTANCE hInstance;
	LPCSTR lpstrFilter;
	LPSTR lpstrCustomFilter;
	DWORD nMaxCustFilter;
	LPSTR lpstrInitialDir;
	LPSTR lpstrTitle;
	DWORD Flags;
	LPSTR lpstrFile;
	DWORD nMaxFile;
	LPSTR lpstrFileTitle;
	DWORD nMaxFileTitle;
	LPCSTR lpstrDefExt;
	LPARAM lCustData;
	void *lpfnHook;
	LPCSTR lpTemplateName;
	void *pvReserved;
	DWORD dwReserved;
} OPENFILENAMEA, *LPOPENFILENAMEA;

typedef int (*PF_TYPE_DECL)(LPVOID);
typedef BOOL (*PBOOL_TYPE_DECL)(LPVOID);
typedef DWORD (*PDWORD_TYPE_DECL)(LPVOID);
typedef void *(*PVOID_TYPE_DECL)(LPVOID);
typedef INT (*PINT_TYPE_DECL)(LPVOID);
typedef HRESULT (*HRESULT_TYPE_DECL)(LPVOID);
typedef ULONGLONG (*PULONGLONG_TYPE_DECL)(LPVOID);
typedef void (*PVOID_CALLEE)(LPVOID);

/* ------------------------------------------------------------------------ */
/* String & conversion helpers                                              */
/* ------------------------------------------------------------------------ */

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define _TRUNCATE ((size_t)-1)
#define _stricmp(a, b) strcasecmp((a), (b))
#define stricmp(a, b) strcasecmp((a), (b))
#define _strnicmp(a, b, c) strncasecmp((a), (b), (c))
#define _wcsnicmp(a, b, c) wcsncmp((a), (b), (c))
#define _wcsicmp(a, b) _wcsnicmp((a), (b), (size_t)-1)
#define _wunlink(p) win_wunlink(p)
#define _snprintf_s(buf, size, trunc, ...) snprintf((buf), (size), __VA_ARGS__)
#define _vsnprintf_s(buf, size, trunc, fmt, args) vsnprintf((buf), (size), fmt, args)
#define _snwprintf(buf, size, ...) swprintf((buf), (size), __VA_ARGS__)
#define _vsnwprintf(buf, size, fmt, args) vswprintf((buf), (size), fmt, args)
#define SelectFont(hwnd, font) SendMessageA((hwnd), WM_SETFONT, (WPARAM)(font), 1)
#define ComboBox_GetCurSel(hCtrl) ((int)SendMessageA((hCtrl), CB_GETCURSEL, (WPARAM)0, (LPARAM)0))
#define ComboBox_GetItemData(hCtrl, index) ((DWORD)SendMessageA((hCtrl), CB_GETITEMDATA, (WPARAM)(index), (LPARAM)0))

#define APPLICATION_ERROR_MASK 0x20000000
#define ERROR_APPLICATION_ERROR 0x20000000
#define swprintf_s(buf, size, ...) swprintf((buf), (size), __VA_ARGS__)

#define _stat64 stat
#define _strtoi64(s, end, base) strtoll((s), (end), (base))
#define __stat64 stat
#define _stricmp(a, b) strcasecmp((a), (b))
#define stricmp(a, b) strcasecmp((a), (b))
#define _strnicmp(a, b, c) strncasecmp((a), (b), (c))
#define _wcsnicmp(a, b, c) wcsncmp((a), (b), (c))
#define _wcsicmp(a, b) _wcsnicmp((a), (b), (size_t)-1)
#define _wunlink(p) win_wunlink(p)
#define strncat_s(dst, dstmax, src, cnt) strncat((dst), (src), (cnt))
#define strcpy_s(dst, dstmax, src) strcpy((dst), (src))
#define strncpy_s(dst, dstmax, src, cnt) strncpy((dst), (src), (cnt))
#define _strdup(s) strdup(s)
#define _read(fd, buf, n) read((fd), (buf), (n))
#define _close(fd) close((fd))
#define _dupenv_s(dst, size, src) ((void)(*(dst) = strdup(src)))
#define _rotr8(x, n) (((x) >> (n)) | ((x) << (8 - (n))))
#define _rotl8(x, n) (((x) << (n)) | ((x) >> (8 - (n))))
#define _rotr16(x, n) (((x) >> (n)) | ((x) << (16 - (n))))
#define _rotl16(x, n) (((x) << (n)) | ((x) >> (16 - (n))))
#define _rotr64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define _rotl64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))
#define MAKEINTRESOURCEA(i) ((LPSTR)(uintptr_t)((WORD)(i)))
#define MAKEINTRESOURCEW(i) ((LPWSTR)(uintptr_t)((WORD)(i)))
#ifndef MAKEINTRESOURCE
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#endif
#define GET_RESOURCE_HANDLE(x) ((LPSTR)(x))
#define SIZEOF_RESOURCE(x) 0
#define LOWORD(x) ((WORD)(((DWORD)(x)) & 0xFFFF))
#define HIWORD(x) ((WORD)((((DWORD)(x)) >> 16) & 0xFFFF))
#define LOBYTE(x) ((BYTE)(((DWORD)(x)) & 0xFF))
#define HIBYTE(x) ((BYTE)((((DWORD)(x)) >> 8) & 0xFF))
#define MAKELONG(lo, hi) (((DWORD)(lo)) | (((DWORD)(hi)) << 16))
#define MAKELONGHI(lo, hi) MAKELONG(lo, hi)
#define ARRAYSIZE_(A) (sizeof(A) / sizeof((A)[0]))
#define __cdecl
#define OPTIONAL
#define __stdcall
#define __fastcall
#define WINAPI
#define CALLBACK
#define WINBASEAPI
#define WINAPIV
#define APIENTRY
#define DECLSPEC_IMPORT
#define _In_
#define _Out_
#define _Inout_
#define _In_reads_(x)
#define _Out_writes_(x)
#define _Out_writes_bytes_(x)
#define _Ret_notnull_
#define _Field_size_(x)
#define _Inout_count_(x)
#define _When_(x)
#define _Success_(x)
#define _Use_decl_annotations_
#define _Post_equals_to_count_(x)
#define __int64 long long
#define _int64 long long
#define _uint64 unsigned long long
#define FORCEINLINE static __inline
#define NTAPI
#ifndef _STATIC_ASSERT
#define _STATIC_ASSERT(x) _Static_assert((x), #x)
#endif
#ifndef STATIC_ASSERT
#define STATIC_ASSERT(x) _Static_assert((x), #x)
#endif
#define STDMETHODCALLTYPE
#define STDMETHODIMP HRESULT
#define STDMETHODIMP_(t) t STDMETHODCALLTYPE
#define LPCTSTR LPCSTR
#define PCTSTR LPCSTR
#define LPTSTR LPSTR
#define PTSTR LPSTR
#define TCHAR char
#define _T(x) x
#define TEXT(x) x

/* ------------------------------------------------------------------------ */
/* Error handling                                                           */
/* ------------------------------------------------------------------------ */

DWORD WINAPI GetLastError(void);
void WINAPI SetLastError(DWORD dwErrCode);
void WINAPI SetLastErrorEx(DWORD dwErrCode, DWORD dwType);
BOOL WINAPI SetHandleInformation(HANDLE hObject, DWORD dwMask, DWORD dwFlags);

/* ------------------------------------------------------------------------ */
/* File I/O                                                                 */
/* ------------------------------------------------------------------------ */

HANDLE WINAPI CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
HANDLE WINAPI CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
BOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
	LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
BOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
	LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
BOOL WINAPI CloseHandle(HANDLE hObject);
BOOL WINAPI FlushFileBuffers(HANDLE hFile);
DWORD WINAPI SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);
BOOL WINAPI SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod);
BOOL WINAPI GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize);
DWORD WINAPI GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
BOOL WINAPI SetFileTime(HANDLE hFile, const FILETIME *lpCreationTime,
	const FILETIME *lpLastAccessTime, const FILETIME *lpLastWriteTime);
BOOL WINAPI GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime);
BOOL WINAPI LockFile(HANDLE hFile, DWORD dwFileOffsetLow, DWORD dwFileOffsetHigh,
	DWORD dwNumberOfBytesToLockLow, DWORD dwNumberOfBytesToLockHigh);
BOOL WINAPI UnlockFile(HANDLE hFile, DWORD dwFileOffsetLow, DWORD dwFileOffsetHigh,
	DWORD dwNumberOfBytesToUnlockLow, DWORD dwNumberOfBytesToUnlockHigh);
BOOL WINAPI GetOverlappedResultEx(HANDLE hFile, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred,
	DWORD dwMilliseconds, BOOL bWait, ULONG dwFlags);
BOOL WINAPI DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
	LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped);
BOOL WINAPI GetOverlappedResult(HANDLE hFile, LPOVERLAPPED lpOverlapped, LPDWORD lpNumberOfBytesTransferred, BOOL bWait);

/* ------------------------------------------------------------------------ */
/* File system                                                              */
/* ------------------------------------------------------------------------ */

BOOL WINAPI DeleteFileA(LPCSTR lpFileName);
BOOL WINAPI DeleteFileW(LPCWSTR lpFileName);
BOOL WINAPI MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName);
BOOL WINAPI MoveFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName);
BOOL WINAPI MoveFileExA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, DWORD dwFlags);
BOOL WINAPI CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists);
BOOL WINAPI CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists);
BOOL WINAPI CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
BOOL WINAPI CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
BOOL WINAPI RemoveDirectoryA(LPCSTR lpPathName);
BOOL WINAPI RemoveDirectoryW(LPCWSTR lpPathName);
DWORD WINAPI GetFileAttributesA(LPCSTR lpFileName);
DWORD WINAPI GetFileAttributesW(LPCWSTR lpFileName);
BOOL WINAPI SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes);
BOOL WINAPI PathFileExistsA(LPCSTR szPath);
HANDLE WINAPI FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
HANDLE WINAPI FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData);
BOOL WINAPI FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);
BOOL WINAPI FindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData);
BOOL WINAPI FindClose(HANDLE hFindFile);
DWORD WINAPI GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart);
DWORD WINAPI GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR *lpFilePart);
DWORD WINAPI GetLongPathNameA(LPCSTR lpFileName, LPSTR lpBuffer, DWORD nBufferLength);
DWORD WINAPI GetShortPathNameA(LPCSTR lpFileName, LPSTR lpBuffer, DWORD nBufferLength);
BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR lpRootPathName, ULARGE_INTEGER *lpFreeBytesAvailableToCaller,
	ULARGE_INTEGER *lpTotalNumberOfBytes, ULARGE_INTEGER *lpTotalNumberOfFreeBytes);
BOOL WINAPI GetVolumeInformationA(LPCSTR lpRootPathName, LPSTR lpVolumeNameBuffer, DWORD nVolumeNameSize,
	LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
	LPSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize);
BOOL WINAPI GetVolumeInformationW(LPCWSTR lpRootPathName, LPWSTR lpVolumeNameBuffer, DWORD nVolumeNameSize,
	LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
	LPWSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize);
BOOL WINAPI SetVolumeLabelA(LPCSTR lpRootPathName, LPCSTR lpVolumeLabel);
BOOL WINAPI FlushVolumeBuffers(HANDLE hVolume);
BOOL WINAPI CancelSynchronousIo(HANDLE hFile);
DWORD WINAPI GetLogicalDrives(void);
BOOL WINAPI CreateSymbolicLinkA(LPCSTR lpSymlinkFileName, LPCSTR lpTargetFileName, DWORD dwFlags);
BOOL WINAPI CreateSymbolicLinkW(LPCWSTR lpSymlinkFileName, LPCWSTR lpTargetFileName, DWORD dwFlags);
int WINAPI _access(const char* filename, int mode);

/* ------------------------------------------------------------------------ */
/* UTF-16 conversions                                                       */
/* ------------------------------------------------------------------------ */

int WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr, int cbMultiByte,
	LPWSTR lpWideCharStr, int cchWideChar);
int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr, int cchWideChar,
	LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar);
BOOL WINAPI IsValidCodePage(UINT cp);
UINT WINAPI GetACP(void);
UINT WINAPI GetOEMCP(void);
UINT WINAPI GetCPInfo(UINT cp, LPVOID lpBuffer);
DWORD WINAPI GetUserDefaultLangID(void);
LCID WINAPI GetThreadLocale(void);
BOOL WINAPI SetThreadLocale(LCID Locale);
int WINAPI Edit_Scroll(HWND hWnd, int nMin, int nMax);
int WINAPI Edit_GetLineCount(HWND hWnd);
int WINAPI _vsnwprintf_s(wchar_t* buf, size_t size, size_t count, const wchar_t* fmt, va_list args);
DWORD WINAPI GetSystemDefaultLangID(void);
DWORD WINAPI GetUserDefaultUILanguage(void);
LCID GetUserDefaultLocale(void);
int win_fwprintf_s(FILE* f, const wchar_t* format, ...);
FILE* _wfopen(const wchar_t* path, const wchar_t* mode);
int win_wunlink(const wchar_t* path);
#define fwprintf_s(f, ...) win_fwprintf_s((f), __VA_ARGS__)
int WINAPI GetKeyboardLayoutNameA(char* lpName);
int WINAPI GetLocaleInfoA(LCID Locale, int LCType, void* lpLCData, int cchData);

/* ------------------------------------------------------------------------ */
/* Process / thread / synchronization                                       */
/* ------------------------------------------------------------------------ */

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize,
	LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId);
DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
DWORD WINAPI WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds);
BOOL WINAPI ReleaseMutex(HANDLE hMutex);
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName);
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName);
HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCWSTR lpName);
BOOL WINAPI SetEvent(HANDLE hEvent);
BOOL WINAPI ResetEvent(HANDLE hEvent);
BOOL WINAPI PulseEvent(HANDLE hEvent);
void WINAPI ExitThread(DWORD dwExitCode);
BOOL WINAPI TerminateThread(HANDLE hThread, DWORD dwExitCode);
HANDLE WINAPI GetCurrentThread(void);
HANDLE WINAPI GetCurrentProcess(void);
DWORD WINAPI GetCurrentProcessId(void);
DWORD WINAPI GetCurrentThreadId(void);
DWORD WINAPI GetProcessId(HANDLE hProcess);
BOOL WINAPI DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle, HANDLE hTargetProcessHandle,
	LPHANDLE lpTargetHandle, DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwOptions);
BOOL WINAPI CloseHandle(HANDLE hObject);
BOOL WINAPI GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode);
BOOL WINAPI GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode);
BOOL WINAPI IsWow64Process(HANDLE hProcess, BOOL *wow64Process);
void WINAPI Sleep(DWORD dwMilliseconds);
BOOL WINAPI SwitchToThread(void);
BOOL WINAPI CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);
HANDLE WINAPI OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId);
BOOL WINAPI GetProcessTimes(HANDLE hProcess, LPFILETIME lpCreationTime, LPFILETIME lpExitTime,
	LPFILETIME lpKernelTime, LPFILETIME lpUserTime);
DWORD WINAPI WaitForSingleObjectEx(HANDLE hObject, DWORD dwMilliseconds, BOOL bAlertable);

/* ------------------------------------------------------------------------ */
/* Modules & resources                                                      */
/* ------------------------------------------------------------------------ */

HMODULE WINAPI LoadLibraryA(LPCSTR lpLibFileName);
HMODULE WINAPI LoadLibraryW(LPCWSTR lpLibFileName);
HMODULE WINAPI LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
HMODULE WINAPI LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName);
HMODULE WINAPI GetModuleHandleW(LPCWSTR lpModuleName);
BOOL WINAPI FreeLibrary(HMODULE hLibModule);
FARPROC WINAPI GetProcAddress(HMODULE hModule, LPCSTR lpProcName);
DWORD WINAPI GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
DWORD WINAPI GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
HMODULE WINAPI GetCurrentProcessModule(void);
HRSRC WINAPI FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType);
HRSRC WINAPI FindResourceW(HMODULE hModule, LPCWSTR lpName, LPCWSTR lpType);
HANDLE WINAPI LoadResource(HMODULE hModule, HRSRC hResInfo);
LPVOID WINAPI LockResource(HANDLE hResData);
DWORD WINAPI SizeofResource(HMODULE hModule, HRSRC hResInfo);
DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lpFileName, LPDWORD lpdwHandle);
BOOL WINAPI GetFileVersionInfoA(LPCSTR lpFileName, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
BOOL WINAPI VerQueryValueA(LPCVOID pVerInfo, LPCSTR lpSubBlock, LPVOID *lppBuffer, UINT *puLen);

/* ------------------------------------------------------------------------ */
/* Strings, memory                                                          */
/* ------------------------------------------------------------------------ */

LPWSTR* WINAPI CommandLineToArgvW(LPCWSTR lpCmdLine, int *pNumArgs);
LPSTR WINAPI GetCommandLineA(void);
LPWSTR WINAPI GetCommandLineW(void);
LPVOID WINAPI LocalAlloc(UINT uFlags, SIZE_T uBytes);
LPVOID WINAPI LocalFree(LPVOID hMem);
LPVOID WINAPI GlobalAlloc(UINT uFlags, SIZE_T uBytes);
LPVOID WINAPI GlobalFree(LPVOID hMem);
HANDLE WINAPI GlobalMemory(void);

/* ------------------------------------------------------------------------ */
/* Time                                                                      */
/* ------------------------------------------------------------------------ */

DWORD WINAPI GetTickCount(void);
ULONGLONG WINAPI GetTickCount64(void);
void WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime);
BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME *lpSystemTime, LPFILETIME lpFileTime);
BOOL WINAPI FileTimeToSystemTime(const FILETIME *lpFileTime, LPSYSTEMTIME lpSystemTime);
DWORD WINAPI GetTimeZoneInformation(LPVOID lpTimeZoneInformation);
void WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime);
time_t time(time_t *t);
struct tm *localtime_r(const time_t *timep, struct tm *result);

/* ------------------------------------------------------------------------ */
/* Console / environment                                                    */
/* ------------------------------------------------------------------------ */

BOOL WINAPI AttachConsole(DWORD dwProcessId);
void WINAPI FreeConsole(void);
HWND WINAPI GetConsoleWindow(void);
BOOL WINAPI AllocConsole(void);
DWORD WINAPI GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize);
BOOL WINAPI SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue);
BOOL WINAPI GetUserNameA(LPSTR lpBuffer, LPDWORD pcbBuffer);
BOOL WINAPI InitializeSecurityDescriptor(PSECURITY_DESCRIPTOR pSecurityDescriptor, DWORD dwRevision);
BOOL WINAPI SetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR pSecurityDescriptor, PSID pOwner, BOOL bOwnerDefaulted);
BOOL WINAPI SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR pSecurityDescriptor, BOOL bDaclPresent, PACL pDacl, BOOL bDaclDefaulted);
void WINAPI OutputDebugStringW(LPCWSTR lpOutputString);
int WINAPI Edit_SetSel(HWND hWnd, int nStart, int nEnd);
int WINAPI Edit_ReplaceSelEx(HWND hWnd, BOOL bReplace, LPCSTR lpText);
#define Edit_ReplaceSel(hwnd, text) Edit_ReplaceSelEx((hwnd), FALSE, (text))
int WINAPI PeekMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg);
BOOL WINAPI DispatchMessage(LPMSG lpMsg);
DWORD WINAPI MsgWaitForMultipleObjects(DWORD nCount, const HANDLE* pHandles, BOOL fWaitAll, DWORD dwMilliseconds, UINT dwWakeMask);
BOOL WINAPI TranslateMessage(LPMSG lpMsg);
DWORD WINAPI GetMessage(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
void WINAPI PostQuitMessage(int nExitCode);
DWORD WINAPI GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer);
BOOL WINAPI SetCurrentDirectoryA(LPCSTR lpPathName);
DWORD WINAPI GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer);
DWORD WINAPI GetTempPathW(DWORD nBufferLength, LPWSTR lpBuffer);
DWORD WINAPI GetTempFileNameA(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT uUnique, LPSTR lpTempFileName);
UINT WINAPI GetSystemDirectoryA(LPSTR lpBuffer, UINT uSize);
UINT WINAPI GetWindowsDirectoryA(LPSTR lpBuffer, UINT uSize);
DWORD WINAPI GetTickCountPosix(void);

/* ------------------------------------------------------------------------ */
/* GUI stubs. These are routed through win_compat.c so that a front-end     */
/* (GTK) can observe them.                                                   */
/* ------------------------------------------------------------------------ */

LRESULT WINAPI SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#define SendMessage SendMessageA
LRESULT WINAPI SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
#define PostMessage PostMessageA
BOOL WINAPI PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
int WINAPI MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);
int WINAPI MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);
BOOL WINAPI IsDlgButtonChecked(HWND hDlg, int nIDDlgItem);
BOOL WINAPI SetDlgItemTextA(HWND hDlg, int nIDDlgItem, LPCSTR lpString);
BOOL WINAPI SetDlgItemTextW(HWND hDlg, int nIDDlgItem, LPCWSTR lpString);
BOOL WINAPI EnableWindow(HWND hWnd, BOOL bEnable);
BOOL WINAPI ShowWindow(HWND hWnd, int nCmdShow);
BOOL WINAPI SetWindowTextA(HWND hWnd, LPCSTR lpString);
BOOL WINAPI SetWindowTextW(HWND hWnd, LPCWSTR lpString);
int WINAPI GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount);
int WINAPI GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount);
int WINAPI GetWindowTextLengthA(HWND hWnd);
HWND WINAPI GetDlgItem(HWND hDlg, int nIDDlgItem);
int WINAPI GetDlgItemTextA(HWND hDlg, int nIDDlgItem, LPSTR lpString, int nMaxCount);
LRESULT WINAPI SendDlgItemMessageA(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI SendDlgItemMessageW(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL WINAPI IsWindowEnabled(HWND hWnd);
BOOL WINAPI IsWindow(HWND hWnd);
char* WINAPI CharUpperA(char* s);
char* WINAPI CharLowerA(char* s);
typedef BOOL (CALLBACK *ENUMUILANGUAGESPROC)(LPWSTR lpLanguage, LONG_PTR lParam);
BOOL WINAPI EnumUILanguages(ENUMUILANGUAGESPROC lpEnumFunc, DWORD dwParam, LPARAM lParam);
BOOL WINAPI IsWindowVisible(HWND hWnd);
BOOL WINAPI MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint);
BOOL WINAPI SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
BOOL WINAPI UpdateWindow(HWND hWnd);
BOOL WINAPI InvalidateRect(HWND hWnd, const RECT *lpRect, BOOL bErase);
BOOL WINAPI GetWindowRect(HWND hWnd, LPRECT lpRect);
BOOL WINAPI GetClientRect(HWND hWnd, LPRECT lpRect);
int WINAPI GetSystemMetrics(int nIndex);
DWORD WINAPI GetSysColor(int nIndex);
HBRUSH WINAPI GetSysColorBrush(int nIndex);
int WINAPI GetDeviceCaps(HDC hdc, int nIndex);
HDC WINAPI BeginPaint(HWND hWnd, LPVOID lpPaintStruct);
BOOL WINAPI EndPaint(HWND hWnd, LPVOID lpPaintStruct);
HDC WINAPI GetDC(HWND hWnd);
HDC WINAPI GetWindowDC(HWND hWnd);
int WINAPI ReleaseDC(HWND hWnd, HDC hDC);
HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ hgdiobj);
BOOL WINAPI DeleteObject(HGDIOBJ hObject);
HGDIOBJ WINAPI GetStockObject(int i);
int WINAPI SetWindowLongPtrA(HWND hWnd, int nIndex, LONG_PTR dwNewLong);
LONG_PTR WINAPI GetWindowLongPtrA(HWND hWnd, int nIndex);
int WINAPI SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong);
LONG WINAPI GetWindowLongA(HWND hWnd, int nIndex);
BOOL WINAPI SetForegroundWindow(HWND hWnd);
BOOL WINAPI BringWindowToTop(HWND hWnd);
HWND WINAPI GetForegroundWindow(void);
BOOL WINAPI SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, void *lpTimerFunc);
BOOL WINAPI KillTimer(HWND hWnd, UINT_PTR uIDEvent);
BOOL WINAPI GetCursorPos(LPPOINT lpPoint);
HCURSOR WINAPI LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName);
BOOL WINAPI SetCursor(HCURSOR hCursor);
BOOL WINAPI RegisterDeviceNotificationA(HANDLE hRecipient, LPVOID lpFilter, DWORD dwFlags);
BOOL WINAPI UnregisterDeviceNotification(HANDLE hDeviceNotificationHandle);
BOOL WINAPI IsUserAnAdmin(void);
HRESULT WINAPI CoCreateGuid(GUID* pguid);
BOOL WINAPI CryptAcquireContext(HANDLE* phProv, LPCSTR szContainer, LPCSTR szProvider, DWORD dwProvType, DWORD dwFlags);
BOOL WINAPI CryptGenRandom(HANDLE hProv, DWORD dwLen, BYTE* pbBuffer);
BOOL WINAPI CryptReleaseContext(HANDLE hProv, DWORD dwFlags);
#define PROV_RSA_FULL 1
#define CRYPT_VERIFYCONTEXT 0xF0000000
BOOL WINAPI ShellExecuteExA(SHELLEXECUTEINFOA *lpExecInfo);
BOOL WINAPI SetProcessDPIAware(void);
UINT WINAPI GetDpiForWindow(HWND hwnd);
BOOL WINAPI AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle);
int WINAPI GetTextExtentPoint32(HDC hdc, LPCSTR lpString, int cchString, LPSIZE lpSize);
HIMAGELIST WINAPI ImageList_Create(int cx, int cy, int cGrow, int cInitial, int cFlags);
int WINAPI ImageList_AddIcon(HIMAGELIST himl, HICON hIcon);
BOOL WINAPI ImageList_Destroy(HIMAGELIST himl);
HICON WINAPI CreateIconIndirect(void *lplIconInfo);
BOOL WINAPI DestroyIcon(HICON hIcon);
BOOL WINAPI GetIconInfo(HICON hIcon, void *lppIconInfo);
HICON WINAPI LoadImageA(HINSTANCE hInstance, LPCSTR lpName, UINT uType, int cxDesired, int cyDesired, UINT fuLoad);
HFONT WINAPI CreateFontIndirectA(const void *lplf);
HFONT WINAPI CreateFontA(int nHeight, int nWidth, int nEscapement, int nOrientation,
	int nWeight, DWORD fdwItalic, DWORD fdwUnderline, DWORD fdwStrikeOut, DWORD fdwCharSet,
	DWORD fdwOutputPrecision, DWORD fdwClipPrecision, DWORD fdwQuality, DWORD fdwPitchAndFamily,
	LPCSTR lpszFace);
int WINAPI MulDiv(int a, int b, int c);
BOOL WINAPI EndDialog(HWND hWnd, int nResult);
BOOL WINAPI SetThreadAffinityMask(HANDLE hThread, DWORD dwThreadAffinityMask);
HANDLE WINAPI CreateEvent(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState, LPCSTR lpName);
BOOL WINAPI SetThreadPriority(HANDLE hThread, int nPriority);
int WINAPI lstrcmpiA(LPCSTR lpStr1, LPCSTR lpStr2);
LPSTR WINAPI StrStrIA(LPCSTR lpHaystack, LPCSTR lpNeedle);
int WINAPI GetDlgCtrlID(HWND hWnd);
BOOL WINAPI SetWindowTextLength(HWND hWnd, int cch);
BOOL WINAPI GetTextMetricsA(HDC hdc, void *lpm);
void *WINAPI SelectObjectPtr(HDC hdc, void *obj);

/* ------------------------------------------------------------------------ */
/* Privilege / security stubs                                               */
/* ------------------------------------------------------------------------ */

BOOL WINAPI OpenProcessToken(HANDLE ProcessHandle, DWORD DesiredAccess, HANDLE *TokenHandle);
BOOL WINAPI LookupPrivilegeValueA(LPCSTR lpSystemName, LPCSTR lpName, void *lpLuid);
BOOL WINAPI AdjustTokenPrivileges(HANDLE TokenHandle, BOOL DisableAllPrivileges,
	void *NewState, DWORD BufferLength, void *PreviousState, DWORD *ReturnLength);
BOOL WINAPI GetTokenInformation(HANDLE TokenHandle, int TokenInformationClass, LPVOID TokenInformation,
	DWORD TokenInformationLength, DWORD *ReturnLength);
BOOL WINAPI LookupAccountSidA(LPCSTR lpSystemName, void *Sid, LPCSTR Name, DWORD *cbName,
	void *ReferencedDomainName, DWORD *cbReferencedDomainName, DWORD *pdwUse);
BOOL WINAPI ConvertSidToStringSidA(void *Sid, LPSTR *StringSid);
BOOL WINAPI IsValidSid(void *Sid);
void *WINAPI GetSidSubAuthority(void *pSid, DWORD nSubAuthority);
DWORD WINAPI GetLengthSid(void *pSid);
LPBYTE WINAPI GetSidIdentifierAuthority(void *pSid);
BOOL WINAPI EqualSid(void *pSid1, void *pSid2);
BOOL WINAPI GetUserNameEx(int NameType, LPSTR lpBuffer, DWORD *nSize);

/* ------------------------------------------------------------------------ */
/* Misc                                                                     */
/* ------------------------------------------------------------------------ */

BOOL WINAPI IsProcessorFeaturePresent(DWORD ProcessorFeature);
BOOL WINAPI IsDebuggerPresent(void);
BOOL WINAPI SetFileValidData(HANDLE hFile, LARGE_INTEGER liEndOfFile);
BOOL WINAPI SetEndOfFile(HANDLE hFile);
void WINAPI OutputDebugStringA(LPCSTR lpOutputString);
BOOL WINAPI GetVersionExA(void *lpVersionInformation);
BOOL WINAPI VerifyVersionInfoA(void *pvi, DWORD dwFlags, void *rvi);
ULONGLONG WINAPI VerSetConditionMask(ULONGLONG ConditionMask, DWORD dwTypeMask, BYTE cond);
void WINAPI GetNativeSystemInfo(void *lpSystemInfo);
BOOL WINAPI GetProductInfo(DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwReserved, DWORD *pdwProductType);
BOOL WINAPI RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, ULONG ulOptions, REGSAM samDesired, HKEY *phkResult);
BOOL WINAPI RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions,
	REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes, HKEY *phkResult, LPDWORD lpdwDisposition);
LONG WINAPI RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
	LPBYTE lpData, LPDWORD lpcbData);
LONG WINAPI RegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType,
	const BYTE *lpData, DWORD cbData);
LONG WINAPI RegDeleteKeyA(HKEY hKey, LPCSTR lpSubKey);
LONG WINAPI RegDeleteValueA(HKEY hKey, LPCSTR lpValueName);
LONG WINAPI RegDeleteKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD dwReserved, DWORD dwOptions);
LONG WINAPI RegCloseKey(HKEY hKey);
LONG WINAPI RegEnumKeyExA(HKEY hKey, DWORD dwIndex, LPSTR lpName, LPDWORD lpcchName,
	LPDWORD lpReserved, LPDWORD lpClass, LPDWORD lpcchClass, LPDWORD lpftLastWriteTime);
DWORD WINAPI GetVersion(void);
BOOL WINAPI GetVolumePathNameA(LPCSTR lpFileName, LPSTR lpVolumePathName, DWORD cchBufferLength);
BOOL WINAPI GetVolumeNameForVolumeMountPointA(LPCSTR lpszVolumePath, LPSTR lpszVolumeName, DWORD cchBufferLength);
BOOL WINAPI GetVolumePathNamesForVolumeNameA(LPCSTR lpszVolumeName, LPSTR lpszVolumePathNames, DWORD cchBufferLength);
HANDLE WINAPI CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID);
BOOL WINAPI CreatePipe(HANDLE* phReadPipe, HANDLE* phWritePipe, LPSECURITY_ATTRIBUTES lpPipeAttributes, DWORD nSize);
BOOL WINAPI TerminateProcess(HANDLE hProcess, UINT uExitCode);
BOOL WINAPI PeekNamedPipe(HANDLE hNamedPipe, LPVOID lpBuffer, DWORD nBufferSize, LPDWORD lpBytesRead,
	LPDWORD lpTotalBytesAvail, LPDWORD lpBytesLeftThisMessage);
#define NORMAL_PRIORITY_CLASS 0x00000020
#define HIGH_PRIORITY_CLASS 0x00000080
#define BELOW_NORMAL_PRIORITY_CLASS 0x00004000
#define IDLE_PRIORITY_CLASS 0x00000040
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_BELOW_NORMAL -1
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_LOWEST -2
#define FORMAT_MESSAGE_FROM_HMODULE 0x00000800
#define NOERROR 0
#define LF_FACESIZE 32
typedef struct _LOGFONTA {
	LONG lfHeight;
	LONG lfWidth;
	LONG lfEscapement;
	LONG lfOrientation;
	LONG lfWeight;
	BYTE lfItalic;
	BYTE lfUnderline;
	BYTE lfStrikeOut;
	BYTE lfCharSet;
	BYTE lfOutPrecision;
	BYTE lfClipPrecision;
	BYTE lfQuality;
	BYTE lfPitchAndFamily;
	char lfFaceName[LF_FACESIZE];
} LOGFONTA, *LPLOGFONTA, *PLOGFONTA;

typedef struct _TEXTMETRICA {
	LONG tmHeight;
	LONG tmAscent;
	LONG tmDescent;
	LONG tmInternalLeading;
	LONG tmExternalLeading;
	LONG tmAveCharWidth;
	LONG tmMaxCharWidth;
	LONG tmWeight;
	LONG tmOverhang;
	LONG tmDigitizedAspectX;
	LONG tmDigitizedAspectY;
	char tmFirstChar;
	char tmLastChar;
	char tmDefaultChar;
	char tmBreakChar;
	BYTE tmItalic;
	BYTE tmUnderlined;
	BYTE tmStruckOut;
	BYTE tmPitchAndFamily;
	BYTE tmCharSet;
} TEXTMETRICA, *LPTEXTMETRICA, *PTEXTMETRICA;
typedef LOGFONTA LOGFONT;
typedef TEXTMETRICA TEXTMETRIC;
typedef BOOL (CALLBACK *CALLBACK_ENUMPROC)(const LOGFONT*, const TEXTMETRIC*, DWORD, LPARAM);

int WINAPI EnumFontFamiliesExA(HDC hdc, LPLOGFONTA lpLogfont, CALLBACK_ENUMPROC lpProc, LPARAM param, DWORD dwFlags);

BOOL WINAPI Process32First(HANDLE hSnapshot, void *lppe);
BOOL WINAPI Process32Next(HANDLE hSnapshot, void *lppe);
BOOL WINAPI Process32FirstW(HANDLE hSnapshot, void *lppe);
BOOL WINAPI Process32NextW(HANDLE hSnapshot, void *lppe);
BOOL WINAPI Module32First(HANDLE hSnapshot, void *lpme);
BOOL WINAPI Module32Next(HANDLE hSnapshot, void *lpme);
BOOL WINAPI Thread32First(HANDLE hSnapshot, void *lpte);
BOOL WINAPI Thread32Next(HANDLE hSnapshot, void *lpte);
DWORD WINAPI GetCurrentThreadIdWin32(void);
BOOL WINAPI QueryDosDeviceA(LPCSTR lpDeviceName, LPSTR lpTargetBuffer, DWORD ucchMax);
BOOL WINAPI DefineDosDeviceA(DWORD dwFlags, LPCSTR lpDeviceName, LPCSTR lpTargetSystem);
DWORD WINAPI GetLogicalDriveStringsA(DWORD nBufferLength, LPSTR lpBuffer);
UINT WINAPI GetDriveTypeA(LPCSTR lpRootPathName);
BOOL WINAPI GetDiskFreeSpaceA(LPCSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector,
	LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters);
BOOL WINAPI GetDiskFreeSpaceExW(LPCWSTR lpRootPathName, ULARGE_INTEGER *lpFreeBytesAvailableToCaller,
	ULARGE_INTEGER *lpTotalNumberOfBytes, ULARGE_INTEGER *lpTotalNumberOfFreeBytes);
UINT WINAPI GetVolumeInformation(void);
BOOL WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime);

/* PE/COFF constants */
#define IMAGE_FILE_MACHINE_UNKNOWN 0
#define IMAGE_FILE_MACHINE_I386 0x014C
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_FILE_MACHINE_ARM 0x01C0
#define IMAGE_FILE_MACHINE_ARM64 0xAA64
#define IMAGE_FILE_MACHINE_ARMNT 0x01C4
#define IMAGE_FILE_MACHINE_RISCV32 0x5032
#define IMAGE_FILE_MACHINE_RISCV64 0x5064
#define IMAGE_FILE_MACHINE_LOONGARCH32 0x6232
#define IMAGE_FILE_MACHINE_LOONGARCH64 0x6264
#define IMAGE_NT_OPTIONAL_HDR32_MAGIC 0x010B
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x020B
#define IMAGE_DIRECTORY_ENTRY_RESOURCE 2
#define IMAGE_DIRECTORY_ENTRY_SECURITY 4
#define IMAGE_DOS_SIGNATURE 0x5A4D
#define IMAGE_NT_SIGNATURE 0x00004550
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5

/* PE image structures */
typedef struct _IMAGE_DOS_HEADER {
	WORD e_magic;
	WORD e_cblp;
	WORD e_cp;
	WORD e_crlc;
	WORD e_cparhdr;
	WORD e_minalloc;
	WORD e_maxalloc;
	WORD e_ss;
	WORD e_sp;
	WORD e_csum;
	WORD e_ip;
	WORD e_cs;
	WORD e_lfarlc;
	WORD e_ovno;
	WORD e_res[4];
	WORD e_oemid;
	WORD e_oeminfo;
	WORD e_res2[10];
	LONG e_lfanew;
} IMAGE_DOS_HEADER;

typedef struct _IMAGE_FILE_HEADER {
	WORD Machine;
	WORD NumberOfSections;
	DWORD TimeDateStamp;
	DWORD PointerToSymbolTable;
	DWORD NumberOfSymbols;
	WORD SizeOfOptionalHeader;
	WORD Characteristics;
} IMAGE_FILE_HEADER;

typedef struct _IMAGE_DATA_DIRECTORY {
	DWORD VirtualAddress;
	DWORD Size;
} IMAGE_DATA_DIRECTORY;

typedef struct _IMAGE_OPTIONAL_HEADER32 {
	WORD Magic;
	BYTE MajorLinkerVersion;
	BYTE MinorLinkerVersion;
	DWORD SizeOfCode;
	DWORD SizeOfInitializedData;
	DWORD SizeOfUninitializedData;
	DWORD AddressOfEntryPoint;
	DWORD BaseOfCode;
	DWORD BaseOfData;
	DWORD ImageBase;
	DWORD SectionAlignment;
	DWORD FileAlignment;
	WORD MajorOperatingSystemVersion;
	WORD MinorOperatingSystemVersion;
	WORD MajorImageVersion;
	WORD MinorImageVersion;
	WORD MajorSubsystemVersion;
	WORD MinorSubsystemVersion;
	DWORD Win32VersionValue;
	DWORD SizeOfImage;
	DWORD SizeOfHeaders;
	DWORD CheckSum;
	WORD Subsystem;
	WORD DllCharacteristics;
	DWORD SizeOfStackReserve;
	DWORD SizeOfStackCommit;
	DWORD SizeOfHeapReserve;
	DWORD SizeOfHeapCommit;
	DWORD LoaderFlags;
	DWORD NumberOfRvaAndSizes;
	IMAGE_DATA_DIRECTORY DataDirectory[16];
} IMAGE_OPTIONAL_HEADER32;

typedef struct _IMAGE_OPTIONAL_HEADER64 {
	WORD Magic;
	BYTE MajorLinkerVersion;
	BYTE MinorLinkerVersion;
	DWORD SizeOfCode;
	DWORD SizeOfInitializedData;
	DWORD SizeOfUninitializedData;
	DWORD AddressOfEntryPoint;
	DWORD BaseOfCode;
	ULONGLONG ImageBase;
	DWORD SectionAlignment;
	DWORD FileAlignment;
	WORD MajorOperatingSystemVersion;
	WORD MinorOperatingSystemVersion;
	WORD MajorImageVersion;
	WORD MinorImageVersion;
	WORD MajorSubsystemVersion;
	WORD MinorSubsystemVersion;
	DWORD Win32VersionValue;
	DWORD SizeOfImage;
	DWORD SizeOfHeaders;
	DWORD CheckSum;
	WORD Subsystem;
	WORD DllCharacteristics;
	ULONGLONG SizeOfStackReserve;
	ULONGLONG SizeOfStackCommit;
	ULONGLONG SizeOfHeapReserve;
	ULONGLONG SizeOfHeapCommit;
	DWORD LoaderFlags;
	DWORD NumberOfRvaAndSizes;
	IMAGE_DATA_DIRECTORY DataDirectory[16];
} IMAGE_OPTIONAL_HEADER64;

typedef struct _IMAGE_NT_HEADERS32 {
	DWORD Signature;
	IMAGE_FILE_HEADER FileHeader;
	IMAGE_OPTIONAL_HEADER32 OptionalHeader;
} IMAGE_NT_HEADERS32;

typedef struct _IMAGE_NT_HEADERS64 {
	DWORD Signature;
	IMAGE_FILE_HEADER FileHeader;
	IMAGE_OPTIONAL_HEADER64 OptionalHeader;
} IMAGE_NT_HEADERS64;

#define IMAGE_SIZEOF_SHORT_NAME 8
#define IMAGE_RESOURCE_DATA_IS_DIRECTORY 0x80000000

/* Authenticode certificate constants (the structure itself is declared in
 * efi.h, which is the file that the Windows build uses for it). */
#define WIN_CERT_TYPE_PKCS_SIGNED_DATA 0x00000002
#define WIN_CERT_REVISION_2_0 0x0200

typedef struct _IMAGE_RESOURCE_DIRECTORY {
	WORD Characteristics;
	WORD TimeDateStamp;
	WORD MajorVersion;
	WORD MinorVersion;
	WORD NumberOfNamedEntries;
	WORD NumberOfIdEntries;
} IMAGE_RESOURCE_DIRECTORY;

typedef struct _IMAGE_RESOURCE_DIRECTORY_ENTRY {
	union {
		struct {
			DWORD NameOffset:31;
			DWORD NameIsString:1;
		};
		DWORD Name;
	};
	union {
		struct {
			DWORD OffsetToData:31;
			DWORD SubDirectory:1;
		};
		DWORD OffsetToDirectory;
	};
} IMAGE_RESOURCE_DIRECTORY_ENTRY;

typedef struct _IMAGE_RESOURCE_DATA_ENTRY {
	DWORD OffsetToData;
	DWORD Size;
	DWORD CodePage;
	DWORD Reserved;
} IMAGE_RESOURCE_DATA_ENTRY;

typedef struct _IMAGE_RESOURCE_DIR_STRING_U {
	WORD Length;
	WCHAR NameString[1];
} IMAGE_RESOURCE_DIR_STRING_U;

typedef struct _IMAGE_SECTION_HEADER {
	char Name[8];
	union {
		struct {
			DWORD PhysicalAddress;
			DWORD VirtualSize;
		} Misc;
		struct {
			DWORD PhysicalAddress;
			DWORD VirtualSize;
		};
	};
	DWORD VirtualAddress;
	DWORD SizeOfRawData;
	DWORD PointerToRawData;
	DWORD PointerToRelocations;
	DWORD PointerToLinenumbers;
	WORD NumberOfRelocations;
	WORD NumberOfLinenumbers;
	DWORD Characteristics;
} IMAGE_SECTION_HEADER;

/* Load library flags */
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x00000008

/* HRESULT facility codes. Note: the definition below matches the one used by
 * Rufus itself (see rufus.h), so that the two do not conflict. */
#define FACILITY_STORAGE 0x0000000A
#define FAC(f) ((f)<<16)
#define ERROR_SEVERITY_ERROR 0xC0000000L

/* Registry access rights */
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_CREATE_SUB_KEY 0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_ALL_ACCESS 0xF003F
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_MULTI_SZ 7
#define REG_QWORD 11
#define REG_OPENED_EXISTING_KEY 1
#define REG_OPTION_NON_VOLATILE 0
#define HKEY_CURRENT_USER ((HKEY)0x80000001)
#define HKEY_USERS ((HKEY)0x80000003)
#define HKEY_CURRENT_CONFIG ((HKEY)0x80000005)
typedef LONG LSTATUS;
#define LOCALE_NAME_MAX_LENGTH 85
#define VER_MAJORVERSION 1
#define VER_MINORVERSION 2
#define VER_BUILD 4
#define VER_PLATFORM_WIN32_NT 2
#define VER_EQUAL 0
#define VER_GREATER 1
#define VER_LESS 2
#define VER_AND 3
#define VER_OR 4
#define VER_CONDITION(dwVer, dwMask, dwCond) (0)
#define LANG_ENGLISH 0x09
#define SUBLANG_ENGLISH_US 0x01
#define SUBLANG_ENGLISH_BRITISH 0x02
#define SORT_DEFAULT 0
#define SORT_STRING 1
#define NO_ERROR 0
#define MAKELCID(lgid, sbid) ((WORD)((((WORD)(sbid)) << 10) | ((WORD)(lgid))))
#define HRESULT_FACILITY(hr) (((unsigned long)(hr) & 0x1FFFFF) >> 19)
#define FACILITY_NULL 0
#define FACILITY_ITF 4
#define FACILITY_WIM 0x48
#define INTERNET_ERROR_BASE 12000
#define INTERNET_ERROR_LAST 12055
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define LANG_NEUTRAL_STR "en-US"
#define HKEY_LOCAL_MACHINE ((HKEY)0x80000002)
#define HKEY_CLASSES_ROOT ((HKEY)0x80000000)

/* Resource types */

#ifdef __cplusplus
}
#endif
