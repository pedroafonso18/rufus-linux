/*
 * Rufus: The Reliable USB Formatting Utility
 * Win32 GUI compatibility stubs for POSIX (Linux) platforms.
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
 * The Rufus platform independent logic still speaks in terms of Win32 window
 * messages. On Linux there is no HWND, so all of the GUI entry points end up
 * here, where they are routed to the front-end dispatch table (see
 * win_compat.h). When no front-end is installed, the calls degrade to
 * harmless no-ops so that the core logic can run headless (e.g. from the CLI).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "windows.h"
#include "win_compat.h"

static win_dispatch win_dispatch_table;
static int win_dispatch_installed = 0;

void win_set_dispatch(const win_dispatch* dispatch)
{
	if (dispatch == NULL) {
		memset(&win_dispatch_table, 0, sizeof(win_dispatch_table));
		win_dispatch_installed = 0;
	} else {
		win_dispatch_table = *dispatch;
		win_dispatch_installed = 1;
	}
}

const win_dispatch* win_get_dispatch(void)
{
	return win_dispatch_installed ? &win_dispatch_table : NULL;
}

LRESULT WINAPI SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (win_dispatch_installed && win_dispatch_table.message != NULL)
		return win_dispatch_table.message(hWnd, Msg, wParam, lParam);
	return 0;
}

LRESULT WINAPI SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	return SendMessageA(hWnd, Msg, wParam, lParam);
}

BOOL WINAPI PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (win_dispatch_installed && win_dispatch_table.message != NULL)
		return (BOOL)win_dispatch_table.message(hWnd, Msg, wParam, lParam);
	return TRUE;
}

BOOL WINAPI PostMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	return PostMessageA(hWnd, Msg, wParam, lParam);
}

int WINAPI MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
	if (win_dispatch_installed && win_dispatch_table.message_box != NULL)
		return win_dispatch_table.message_box(hWnd, lpText, lpCaption, uType);
	/* Headless fallback: report on stderr */
	fprintf(stderr, "%s: %s\n", lpCaption != NULL ? lpCaption : "Rufus",
		lpText != NULL ? lpText : "");
	if ((uType & 0x0F) == MB_YESNO)
		return IDNO;
	return IDOK;
}

int WINAPI MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)
{
	char text[4096], caption[512];
	win_wide_to_utf8((const wchar_t*)lpText, -1, text, sizeof(text));
	win_wide_to_utf8((const wchar_t*)lpCaption, -1, caption, sizeof(caption));
	return MessageBoxA(hWnd, text, caption, uType);
}

BOOL WINAPI IsDlgButtonChecked(HWND hDlg, int nIDDlgItem)
{
	(void)hDlg; (void)nIDDlgItem;
	return FALSE;
}

BOOL WINAPI SetDlgItemTextA(HWND hDlg, int nIDDlgItem, LPCSTR lpString)
{
	(void)hDlg; (void)nIDDlgItem; (void)lpString;
	return TRUE;
}

BOOL WINAPI SetDlgItemTextW(HWND hDlg, int nIDDlgItem, LPCWSTR lpString)
{
	(void)hDlg; (void)nIDDlgItem; (void)lpString;
	return TRUE;
}

BOOL WINAPI EnableWindow(HWND hWnd, BOOL bEnable) { (void)hWnd; (void)bEnable; return TRUE; }
BOOL WINAPI ShowWindow(HWND hWnd, int nCmdShow) { (void)hWnd; (void)nCmdShow; return TRUE; }
BOOL WINAPI SetWindowTextA(HWND hWnd, LPCSTR lpString) { (void)hWnd; (void)lpString; return TRUE; }
BOOL WINAPI SetWindowTextW(HWND hWnd, LPCWSTR lpString) { (void)hWnd; (void)lpString; return TRUE; }
int WINAPI GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount)
{
	(void)hWnd;
	if (lpString != NULL && nMaxCount > 0)
		lpString[0] = 0;
	return 0;
}
int WINAPI GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount)
{
	(void)hWnd;
	if (lpString != NULL && nMaxCount > 0)
		lpString[0] = 0;
	return 0;
}
int WINAPI GetWindowTextLengthA(HWND hWnd) { (void)hWnd; return 0; }
HWND WINAPI GetDlgItem(HWND hDlg, int nIDDlgItem) { (void)hDlg; (void)nIDDlgItem; return NULL; }
int WINAPI GetDlgItemTextA(HWND hDlg, int nIDDlgItem, LPSTR lpString, int nMaxCount)
{
	(void)hDlg; (void)nIDDlgItem;
	if (lpString != NULL && nMaxCount > 0)
		lpString[0] = 0;
	return 0;
}
LRESULT WINAPI SendDlgItemMessageA(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	(void)hDlg; (void)nIDDlgItem;
	if (win_dispatch_installed && win_dispatch_table.message != NULL)
		return win_dispatch_table.message(NULL, Msg, wParam, lParam);
	return 0;
}
LRESULT WINAPI SendDlgItemMessageW(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	return SendDlgItemMessageA(hDlg, nIDDlgItem, Msg, wParam, lParam);
}
BOOL WINAPI IsWindowEnabled(HWND hWnd) { (void)hWnd; return TRUE; }
BOOL WINAPI IsWindowVisible(HWND hWnd) { (void)hWnd; return FALSE; }
BOOL WINAPI MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint)
{
	(void)hWnd; (void)X; (void)Y; (void)nWidth; (void)nHeight; (void)bRepaint;
	return TRUE;
}
BOOL WINAPI SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
	(void)hWnd; (void)hWndInsertAfter; (void)X; (void)Y; (void)cx; (void)cy; (void)uFlags;
	return TRUE;
}
BOOL WINAPI UpdateWindow(HWND hWnd) { (void)hWnd; return TRUE; }
BOOL WINAPI InvalidateRect(HWND hWnd, const RECT *lpRect, BOOL bErase)
{
	(void)hWnd; (void)lpRect; (void)bErase;
	return TRUE;
}
BOOL WINAPI GetWindowRect(HWND hWnd, LPRECT lpRect)
{
	(void)hWnd;
	if (lpRect != NULL) {
		lpRect->left = lpRect->top = 0;
		lpRect->right = lpRect->bottom = 0;
	}
	return TRUE;
}
BOOL WINAPI GetClientRect(HWND hWnd, LPRECT lpRect)
{
	(void)hWnd;
	if (lpRect != NULL) {
		lpRect->left = lpRect->top = 0;
		lpRect->right = lpRect->bottom = 0;
	}
	return TRUE;
}
int WINAPI GetSystemMetrics(int nIndex)
{
	switch (nIndex) {
	case 0: return 1024;	/* SM_CXSCREEN */
	case 1: return 768;		/* SM_CYSCREEN */
	case 4: return 15;		/* SM_CXVSCROLL */
	case 5: return 30;		/* SM_CYHSCROLL */
	default: return 0;
	}
}
DWORD WINAPI GetSysColor(int nIndex) { (void)nIndex; return 0xFFFFFF; }
HBRUSH WINAPI GetSysColorBrush(int nIndex) { (void)nIndex; return (HBRUSH)(intptr_t)1; }
int WINAPI GetDeviceCaps(HDC hdc, int nIndex)
{
	(void)hdc;
	switch (nIndex) {
	case 0: return 1024;	/* HORZRES */
	case 1: return 768;		/* VERTRES */
	case 2: return 96;		/* LOGPIXELSX */
	case 3: return 96;		/* LOGPIXELSY */
	default: return 0;
	}
}
HDC WINAPI BeginPaint(HWND hWnd, LPVOID lpPaintStruct) { (void)hWnd; (void)lpPaintStruct; return NULL; }
BOOL WINAPI EndPaint(HWND hWnd, LPVOID lpPaintStruct) { (void)hWnd; (void)lpPaintStruct; return TRUE; }
HDC WINAPI GetDC(HWND hWnd) { (void)hWnd; return NULL; }
HDC WINAPI GetWindowDC(HWND hWnd) { (void)hWnd; return NULL; }
int WINAPI ReleaseDC(HWND hWnd, HDC hDC) { (void)hWnd; (void)hDC; return 1; }
HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ hgdiobj) { (void)hdc; return hgdiobj; }
BOOL WINAPI DeleteObject(HGDIOBJ hObject) { (void)hObject; return TRUE; }
HGDIOBJ WINAPI GetStockObject(int i) { (void)i; return NULL; }
int WINAPI SetWindowLongPtrA(HWND hWnd, int nIndex, LONG_PTR dwNewLong)
{
	(void)hWnd; (void)nIndex;
	return (int)dwNewLong;
}
LONG_PTR WINAPI GetWindowLongPtrA(HWND hWnd, int nIndex) { (void)hWnd; (void)nIndex; return 0; }
int WINAPI SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong) { (void)hWnd; (void)nIndex; return (int)dwNewLong; }
LONG WINAPI GetWindowLongA(HWND hWnd, int nIndex) { (void)hWnd; (void)nIndex; return 0; }
BOOL WINAPI SetForegroundWindow(HWND hWnd) { (void)hWnd; return TRUE; }
BOOL WINAPI BringWindowToTop(HWND hWnd) { (void)hWnd; return TRUE; }
HWND WINAPI GetForegroundWindow(void) { return NULL; }
BOOL WINAPI SetTimer(HWND hWnd, UINT_PTR nIDEvent, UINT uElapse, void *lpTimerFunc)
{
	(void)hWnd; (void)nIDEvent; (void)uElapse; (void)lpTimerFunc;
	return TRUE;
}
BOOL WINAPI KillTimer(HWND hWnd, UINT_PTR uIDEvent) { (void)hWnd; (void)uIDEvent; return TRUE; }
BOOL WINAPI GetCursorPos(LPPOINT lpPoint)
{
	if (lpPoint != NULL) { lpPoint->x = 0; lpPoint->y = 0; }
	return TRUE;
}
HCURSOR WINAPI LoadCursorA(HINSTANCE hInstance, LPCSTR lpCursorName) { (void)hInstance; (void)lpCursorName; return NULL; }
BOOL WINAPI SetCursor(HCURSOR hCursor) { (void)hCursor; return TRUE; }
BOOL WINAPI AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle)
{
	(void)dwStyle; (void)bMenu; (void)dwExStyle;
	if (lpRect != NULL)
		lpRect->right = lpRect->left + 400, lpRect->bottom = lpRect->top + 300;
	return TRUE;
}
int WINAPI GetTextExtentPoint32(HDC hdc, LPCSTR lpString, int cchString, LPSIZE lpSize)
{
	(void)hdc;
	if (lpSize != NULL) {
		lpSize->cx = (cchString > 0) ? cchString * 7 : 0;
		lpSize->cy = 14;
	}
	return TRUE;
}
HIMAGELIST WINAPI ImageList_Create(int cx, int cy, int cGrow, int cInitial, int cFlags)
{
	(void)cx; (void)cy; (void)cGrow; (void)cInitial; (void)cFlags;
	return (HIMAGELIST)(intptr_t)1;
}
int WINAPI ImageList_AddIcon(HIMAGELIST himl, HICON hIcon) { (void)himl; (void)hIcon; return 0; }
BOOL WINAPI ImageList_Destroy(HIMAGELIST himl) { (void)himl; return TRUE; }
HICON WINAPI CreateIconIndirect(void *lplIconInfo) { (void)lplIconInfo; return NULL; }
BOOL WINAPI DestroyIcon(HICON hIcon) { (void)hIcon; return TRUE; }
BOOL WINAPI GetIconInfo(HICON hIcon, void *lppIconInfo) { (void)hIcon; (void)lppIconInfo; return FALSE; }
HICON WINAPI LoadImageA(HINSTANCE hInstance, LPCSTR lpName, UINT uType, int cxDesired, int cyDesired, UINT fuLoad)
{
	(void)hInstance; (void)lpName; (void)uType; (void)cxDesired; (void)cyDesired; (void)fuLoad;
	return NULL;
}
HFONT WINAPI CreateFontIndirectA(const void *lplf) { (void)lplf; return NULL; }
BOOL WINAPI GetTextMetricsA(HDC hdc, void *lpm) { (void)hdc; (void)lpm; return TRUE; }

/* File dialogs ------------------------------------------------------------- */

static win_file_dialog_handler win_file_dialog = NULL;

void win_set_file_dialog(win_file_dialog_handler handler)
{
	win_file_dialog = handler;
}

BOOL win_file_dialog_impl(BOOL save, const char* title, const char* filter, char* buffer, DWORD buffer_size)
{
	if (win_file_dialog == NULL)
		return FALSE;
	return win_file_dialog(save, title, filter, buffer, buffer_size);
}
