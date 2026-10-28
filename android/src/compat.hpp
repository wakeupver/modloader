/*
 *  Mod Loader - Android port
 *      Portability shims for the (few) upstream headers that are written against MSVC.
 *
 *      Upstream's "container.hpp" uses _stricmp / _strnicmp, everything else that we reuse from the
 *      original source tree (modloader/modloader.h, modloader/modloader.hpp, modloader/util/hash.hpp,
 *      ini_parser/ini_parser.hpp) is plain standard C++ and needs nothing.
 *
 *      Include this header BEFORE any upstream header.
 *
 *  Licensed under the MIT License (same as the original Mod Loader).
 */
#pragma once

#include <strings.h>
#include <cstdint>
#include <cstddef>

#ifndef _stricmp
#define _stricmp    strcasecmp
#endif

#ifndef _strnicmp
#define _strnicmp   strncasecmp
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MLA_PRINTF_FMT(fmtidx, argidx) __attribute__((format(printf, fmtidx, argidx)))
#else
#define MLA_PRINTF_FMT(fmtidx, argidx)
#endif
