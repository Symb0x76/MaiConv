#pragma once

// #define WIN32_LEAN_AND_MEAN
// #ifndef _UNICODE
// #define _UNICODE
// #endif
// #define NOMINMAX
//
// #include <windows.h>

#ifdef _WIN32
#include <malloc.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

// Vendored AssetsTools is built into MaiConv as static sources, not as a DLL.
// Force ASSETSTOOLS_API to be empty to avoid dllimport/dllexport mismatches.
#ifndef ASSETSTOOLS_IMPORTSTATIC
#define ASSETSTOOLS_IMPORTSTATIC
#endif

// Keep legacy UABE code using stricmp/strnicmp portable across toolchains.
#ifndef _WIN32
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif
#endif
#ifndef stricmp
#define stricmp _stricmp
#endif
#ifndef strnicmp
#define strnicmp _strnicmp
#endif

#ifndef ZeroMemory
#define ZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#endif

typedef uint64_t QWORD;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
