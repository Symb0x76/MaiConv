#pragma once

//#define WIN32_LEAN_AND_MEAN
//#ifndef _UNICODE
//#define _UNICODE
//#endif
//#define NOMINMAX
//
//#include <windows.h>

#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Vendored AssetsTools is built into MaiConv as static sources, not as a DLL.
// Force ASSETSTOOLS_API to be empty to avoid dllimport/dllexport mismatches.
#ifndef ASSETSTOOLS_IMPORTSTATIC
#define ASSETSTOOLS_IMPORTSTATIC
#endif

// Keep legacy UABE code using stricmp/strnicmp portable under MSVC/IntelliSense.
#ifndef stricmp
#define stricmp _stricmp
#endif
#ifndef strnicmp
#define strnicmp _strnicmp
#endif

typedef uint64_t QWORD;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
