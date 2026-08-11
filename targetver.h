#pragma once

// Keep the import surface compatible with Windows 7. Newer APIs must be
// resolved dynamically and must provide a Windows 7 fallback.
#ifndef WINVER
#define WINVER 0x0601
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <WinSDKVer.h>
#include <SDKDDKVer.h>
