/*
 * Rufus: The Reliable USB Formatting Utility
 * Aligned allocation helpers for POSIX platforms.
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
 * A few of the vendored libraries (Syslinux' libfat and ms-sys) use the
 * MSVC/MinGW specific _mm_malloc()/_mm_free() pair, which allocate memory
 * aligned on a specific boundary. This header provides a POSIX equivalent so
 * that those libraries can be built unmodified on Linux.
 */

#pragma once

#include <stdlib.h>
#include <string.h>

#ifndef _mm_malloc
#define _mm_malloc(size, alignment) posix_mm_malloc((size), (alignment))
#endif
#ifndef _mm_free
#define _mm_free(p) do { free((void*)(p)); (p) = NULL; } while(0)
#endif

#if defined(__GNUC__)
__attribute__((unused))
#endif
static __inline void* posix_mm_malloc(size_t size, size_t alignment)
{
	void* p = NULL;
	if (alignment < sizeof(void*))
		alignment = sizeof(void*);
	/* posix_memalign() requires a power of two multiple of sizeof(void*) */
	if (alignment & (alignment - 1))
		return NULL;
	if (posix_memalign(&p, alignment, size) != 0)
		return NULL;
	memset(p, 0, size);
	return p;
}
