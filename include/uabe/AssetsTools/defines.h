#pragma once
#include <string>
#include <cstdint>

typedef uint64_t QWORD;
// Vendored in-tree build uses static compilation; no import/export attributes required.
#define ASSETSTOOLS_API

#ifndef __AssetsTools_AssetsFileFunctions_Read
#define __AssetsTools_AssetsFileFunctions_Read
typedef void(*AssetsFileVerifyLogger)(const char* message);
#endif

#ifndef __AssetsTools_AssetsReplacerFunctions_FreeCallback
#define __AssetsTools_AssetsReplacerFunctions_FreeCallback
typedef void(*cbFreeMemoryResource)(void* pResource);
typedef void(*cbFreeReaderResource)(class IAssetsReader* pReader);
#endif
#ifndef __AssetsTools_Hash128
#define __AssetsTools_Hash128
union Hash128
{
	uint8_t bValue[16];
	uint16_t wValue[8];
	uint32_t dValue[4];
	QWORD qValue[2];
};
#endif

#include "AssetsFileReader.h"