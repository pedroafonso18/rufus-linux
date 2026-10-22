/*
 * Stub for the Windows header <intrin.h> - not available on Linux.
 *
 * The CPUID intrinsics are provided by the GCC/Clang x86 intrinsics header.
 */
#pragma once

#if defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#endif
