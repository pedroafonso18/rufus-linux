/*
 * Rufus: The Reliable USB Formatting Utility
 * Win32 compatibility layer for POSIX (Linux) platforms - shared declarations.
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
#include "windows.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Convert a Win32 HANDLE to a POSIX file descriptor (and back). */
int win_handle_to_fd(HANDLE h);
HANDLE win_fd_to_handle(int fd);

/* UTF-8 <-> wide character helpers. Both return the number of characters
 * written (excluding the terminating NUL), or -1 if the buffer was too small.
 * On POSIX systems wchar_t is a 32 bit type, so these work on code points. */
int win_utf8_to_wide(const char* in, int in_len, wchar_t* out, int out_len);
int win_wide_to_utf8(const wchar_t* in, int in_len, char* out, int out_len);

/* The command line of the current process (as a single string). */
char* win_get_command_line(void);

/* Aligned allocation, the POSIX equivalent of the Win32 _mm_malloc(). */
void* win_mm_malloc(size_t size, size_t alignment);
#ifndef _mm_malloc
#define _mm_malloc(size, alignment) win_mm_malloc((size), (alignment))
#endif
#ifndef _mm_free
#define _mm_free(p) free((void*)(p))
#endif

/*
 * Front-end dispatch.
 *
 * The Win32 GUI entry points (SendMessage, PostMessage, MessageBox, ...) are
 * no-ops by default. A Linux front-end (GTK or the CLI) can install handlers
 * to receive them, which allows the platform independent Rufus logic to stay
 * unchanged.
 */
typedef struct {
	/* Handle a window message. Return 0 for "not handled". */
	LRESULT (*message)(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
	/* Display a modal message box. Return one of IDOK/IDCANCEL/IDYES/IDNO. */
	int (*message_box)(HWND hWnd, const char* text, const char* caption, UINT type);
	/* Report progress. 'value' is a percentage in the 0..100 range, or -1 for
	 * an indeterminate ("marquee") state. */
	void (*progress)(void* context, int value, const char* text);
	/* Append a line to the log. */
	void (*log)(void* context, const char* text);
	/* Called when the user asks for the log window. */
	void (*show_log)(void* context, const char* text);
	/* Opaque context passed back to the callbacks. */
	void* context;
} win_dispatch;

/*
 * Log verbosity. When set to 0, the debug level messages (such as the
 * localization loader chatter) are suppressed.
 */
extern int win_log_verbose;

/* Install (or clear, when passed NULL) the front-end dispatch table. */
void win_set_dispatch(const win_dispatch* dispatch);
const win_dispatch* win_get_dispatch(void);

/*
 * File open/save dialog. The handler receives the requested operation, the
 * dialog title, the (Windows style) filter description and a buffer to fill
 * with the selected path. It returns TRUE if the user made a selection.
 */
typedef BOOL (*win_file_dialog_handler)(BOOL save, const char* title, const char* filter,
	char* buffer, DWORD buffer_size);
void win_set_file_dialog(win_file_dialog_handler handler);
BOOL win_file_dialog_impl(BOOL save, const char* title, const char* filter, char* buffer, DWORD buffer_size);

#ifdef __cplusplus
}
#endif
