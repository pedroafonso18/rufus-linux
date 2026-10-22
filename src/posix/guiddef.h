/*
 * Stub for the Windows header <guiddef.h> - not available on Linux.
 */
#pragma once

#include "windows.h"

#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
	const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
#define INITGUID
