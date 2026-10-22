/*
 * Stub for the Windows header <windowsx.h> - not available on Linux.
 *
 * The Win32 mouse/keyboard macros have no equivalent here; the few that the
 * Rufus code uses are provided as no-ops.
 */
#pragma once

#include "windows.h"

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define GET_WHEEL_DELTA_WPARAM(wParam) 0
#define MK_SHIFT(x) 0
#define MK_CONTROL(x) 0
#define MK_LBUTTON(x) 0
#define MK_MBUTTON(x) 0
#define MK_RBUTTON(x) 0
