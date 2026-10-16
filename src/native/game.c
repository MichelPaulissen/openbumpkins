/* SPDX-License-Identifier: GPL-3.0-only */
#include "game_data.h"
#include "native.h"
#include <stdlib.h>
#ifdef BB_MODEL_RUNTIME
#include "../bridge/address.h"
#include "platform_models.h"
#endif
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

struct GameImport {
  uintptr_t slot;
  const char *module, *name;
};
static const struct GameImport imports[] = {
    {0x006ca4c4u, "QMIXER95.dll", "QSWaveMixPlay"},
    {0x006ca4c8u, "QMIXER95.dll", "QSWaveMixConfigureInit"},
    {0x006ca4ccu, "QMIXER95.dll", "QSWaveMixIsChannelDone"},
    {0x006ca4d0u, "QMIXER95.dll", "QSWaveMixFreeWave"},
    {0x006ca4d4u, "QMIXER95.dll", "QSWaveMixSetFrequency"},
    {0x006ca4d8u, "QMIXER95.dll", "QSWaveMixOpenWave"},
    {0x006ca4dcu, "QMIXER95.dll", "QSWaveMixCloseSession"},
    {0x006ca4e0u, "QMIXER95.dll", "QSWaveMixFlushChannel"},
    {0x006ca4e4u, "QMIXER95.dll", "QSWaveMixActivate"},
    {0x006ca4e8u, "QMIXER95.dll", "QSWaveMixSetVolume"},
    {0x006ca4ecu, "QMIXER95.dll", "QSWaveMixGetDirectSound"},
    {0x006ca4f0u, "QMIXER95.dll", "QSWaveMixSetPosition"},
    {0x006ca4f4u, "QMIXER95.dll", "QSWaveMixEnable"},
    {0x006ca4f8u, "QMIXER95.dll", "QSWaveMixPauseChannel"},
    {0x006ca4fcu, "QMIXER95.dll", "QSWaveMixRestartChannel"},
    {0x006ca500u, "QMIXER95.dll", "QSWaveMixGetOptions"},
    {0x006ca504u, "QMIXER95.dll", "QSWaveMixSetOptions"},
    {0x006ca508u, "QMIXER95.dll", "QSWaveMixOpenChannel"},
    {0x006ca374u, "KERNEL32.dll", "OutputDebugStringA"},
    {0x006ca378u, "KERNEL32.dll", "GetTickCount"},
    {0x006ca37cu, "KERNEL32.dll", "CloseHandle"},
    {0x006ca380u, "KERNEL32.dll", "ReadFile"},
    {0x006ca384u, "KERNEL32.dll", "CreateFileA"},
    {0x006ca388u, "KERNEL32.dll", "Sleep"},
    {0x006ca38cu, "KERNEL32.dll", "GetVolumeInformationA"},
    {0x006ca390u, "KERNEL32.dll", "GetDiskFreeSpaceA"},
    {0x006ca394u, "KERNEL32.dll", "GetDriveTypeA"},
    {0x006ca398u, "KERNEL32.dll", "GetLogicalDriveStringsA"},
    {0x006ca39cu, "KERNEL32.dll", "GetModuleHandleA"},
    {0x006ca3a0u, "KERNEL32.dll", "_lclose"},
    {0x006ca3a4u, "KERNEL32.dll", "_lread"},
    {0x006ca3a8u, "KERNEL32.dll", "_lopen"},
    {0x006ca3acu, "KERNEL32.dll", "LockResource"},
    {0x006ca3b0u, "KERNEL32.dll", "LoadResource"},
    {0x006ca3b4u, "KERNEL32.dll", "FindResourceA"},
    {0x006ca3b8u, "KERNEL32.dll", "InterlockedExchange"},
    {0x006ca3bcu, "KERNEL32.dll", "InterlockedIncrement"},
    {0x006ca3c0u, "KERNEL32.dll", "InterlockedDecrement"},
    {0x006ca3c4u, "KERNEL32.dll", "SetThreadPriority"},
    {0x006ca3c8u, "KERNEL32.dll", "GetCurrentThread"},
    {0x006ca3ccu, "KERNEL32.dll", "CreateThread"},
    {0x006ca3d0u, "KERNEL32.dll", "GlobalAlloc"},
    {0x006ca3d4u, "KERNEL32.dll", "GlobalFree"},
    {0x006ca3d8u, "KERNEL32.dll", "SetEnvironmentVariableA"},
    {0x006ca3dcu, "KERNEL32.dll", "CompareStringW"},
    {0x006ca3e0u, "KERNEL32.dll", "CompareStringA"},
    {0x006ca3e4u, "KERNEL32.dll", "IsBadCodePtr"},
    {0x006ca3e8u, "KERNEL32.dll", "SetUnhandledExceptionFilter"},
    {0x006ca3ecu, "KERNEL32.dll", "GetStringTypeW"},
    {0x006ca3f0u, "KERNEL32.dll", "GetStringTypeA"},
    {0x006ca3f4u, "KERNEL32.dll", "LCMapStringW"},
    {0x006ca3f8u, "KERNEL32.dll", "LCMapStringA"},
    {0x006ca3fcu, "KERNEL32.dll", "FlushFileBuffers"},
    {0x006ca400u, "KERNEL32.dll", "SetStdHandle"},
    {0x006ca404u, "KERNEL32.dll", "RaiseException"},
    {0x006ca408u, "KERNEL32.dll", "VirtualAlloc"},
    {0x006ca40cu, "KERNEL32.dll", "HeapFree"},
    {0x006ca410u, "KERNEL32.dll", "GetStdHandle"},
    {0x006ca414u, "KERNEL32.dll", "HeapAlloc"},
    {0x006ca418u, "KERNEL32.dll", "VirtualFree"},
    {0x006ca41cu, "KERNEL32.dll", "HeapCreate"},
    {0x006ca420u, "KERNEL32.dll", "HeapDestroy"},
    {0x006ca424u, "KERNEL32.dll", "GetOEMCP"},
    {0x006ca428u, "KERNEL32.dll", "GetACP"},
    {0x006ca42cu, "KERNEL32.dll", "GetCPInfo"},
    {0x006ca430u, "KERNEL32.dll", "WideCharToMultiByte"},
    {0x006ca434u, "KERNEL32.dll", "GetEnvironmentStringsW"},
    {0x006ca438u, "KERNEL32.dll", "HeapReAlloc"},
    {0x006ca43cu, "KERNEL32.dll", "GetLastError"},
    {0x006ca440u, "KERNEL32.dll", "GetModuleFileNameA"},
    {0x006ca444u, "KERNEL32.dll", "RtlUnwind"},
    {0x006ca448u, "KERNEL32.dll", "HeapValidate"},
    {0x006ca44cu, "KERNEL32.dll", "DebugBreak"},
    {0x006ca450u, "KERNEL32.dll", "IsBadWritePtr"},
    {0x006ca454u, "KERNEL32.dll", "GetVersion"},
    {0x006ca458u, "KERNEL32.dll", "IsBadReadPtr"},
    {0x006ca45cu, "KERNEL32.dll", "GetStartupInfoA"},
    {0x006ca460u, "KERNEL32.dll", "GetLocalTime"},
    {0x006ca464u, "KERNEL32.dll", "GetCommandLineA"},
    {0x006ca468u, "KERNEL32.dll", "GetTimeZoneInformation"},
    {0x006ca46cu, "KERNEL32.dll", "GetCurrentProcess"},
    {0x006ca470u, "KERNEL32.dll", "GetSystemTime"},
    {0x006ca474u, "KERNEL32.dll", "ExitProcess"},
    {0x006ca478u, "KERNEL32.dll", "WaitForSingleObject"},
    {0x006ca47cu, "KERNEL32.dll", "TerminateProcess"},
    {0x006ca480u, "KERNEL32.dll", "SetEndOfFile"},
    {0x006ca484u, "KERNEL32.dll", "FreeEnvironmentStringsW"},
    {0x006ca488u, "KERNEL32.dll", "GetEnvironmentStrings"},
    {0x006ca48cu, "KERNEL32.dll", "MultiByteToWideChar"},
    {0x006ca490u, "KERNEL32.dll", "FreeEnvironmentStringsA"},
    {0x006ca494u, "KERNEL32.dll", "UnhandledExceptionFilter"},
    {0x006ca498u, "KERNEL32.dll", "GetFileType"},
    {0x006ca49cu, "KERNEL32.dll", "SetHandleCount"},
    {0x006ca4a0u, "KERNEL32.dll", "SystemTimeToFileTime"},
    {0x006ca4a4u, "KERNEL32.dll", "CreateEventA"},
    {0x006ca4a8u, "KERNEL32.dll", "LoadLibraryA"},
    {0x006ca4acu, "KERNEL32.dll", "GetProcAddress"},
    {0x006ca4b0u, "KERNEL32.dll", "SetFilePointer"},
    {0x006ca4b4u, "KERNEL32.dll", "WriteFile"},
    {0x006ca4b8u, "KERNEL32.dll", "FileTimeToSystemTime"},
    {0x006ca4bcu, "KERNEL32.dll", "GetFileTime"},
    {0x006ca510u, "USER32.dll", "PostQuitMessage"},
    {0x006ca514u, "USER32.dll", "BeginPaint"},
    {0x006ca518u, "USER32.dll", "UpdateWindow"},
    {0x006ca51cu, "USER32.dll", "WaitMessage"},
    {0x006ca520u, "USER32.dll", "DispatchMessageA"},
    {0x006ca524u, "USER32.dll", "TranslateMessage"},
    {0x006ca528u, "USER32.dll", "GetMessageA"},
    {0x006ca52cu, "USER32.dll", "PeekMessageA"},
    {0x006ca530u, "USER32.dll", "LoadImageA"},
    {0x006ca534u, "USER32.dll", "LoadIconA"},
    {0x006ca538u, "USER32.dll", "LoadCursorA"},
    {0x006ca53cu, "USER32.dll", "RegisterClassA"},
    {0x006ca540u, "USER32.dll", "GetSystemMetrics"},
    {0x006ca544u, "USER32.dll", "CreateWindowExA"},
    {0x006ca548u, "USER32.dll", "PostMessageA"},
    {0x006ca54cu, "USER32.dll", "EndPaint"},
    {0x006ca550u, "USER32.dll", "SetCursorPos"},
    {0x006ca554u, "USER32.dll", "DefWindowProcA"},
    {0x006ca558u, "USER32.dll", "MessageBoxA"},
    {0x006ca34cu, "GDI32.dll", "GetStockObject"},
    {0x006ca350u, "GDI32.dll", "GetPixel"},
    {0x006ca354u, "GDI32.dll", "SetPixel"},
    {0x006ca358u, "GDI32.dll", "CreateCompatibleDC"},
    {0x006ca35cu, "GDI32.dll", "SelectObject"},
    {0x006ca360u, "GDI32.dll", "StretchBlt"},
    {0x006ca364u, "GDI32.dll", "DeleteDC"},
    {0x006ca368u, "GDI32.dll", "GetObjectA"},
    {0x006ca36cu, "GDI32.dll", "DeleteObject"},
    {0x006ca328u, "ADVAPI32.dll", "RegDeleteKeyA"},
    {0x006ca32cu, "ADVAPI32.dll", "RegCloseKey"},
    {0x006ca330u, "ADVAPI32.dll", "RegOpenKeyExA"},
    {0x006ca334u, "ADVAPI32.dll", "RegEnumValueA"},
    {0x006ca33cu, "DDRAW.dll", "DirectDrawCreate"},
    {0x006ca344u, "DSOUND.dll", "DirectSoundCreate"},
    {0x006ca560u, "WINMM.dll", "timeEndPeriod"},
    {0x006ca564u, "WINMM.dll", "mmioRead"},
    {0x006ca568u, "WINMM.dll", "mmioSetBuffer"},
    {0x006ca56cu, "WINMM.dll", "mmioOpenA"},
    {0x006ca570u, "WINMM.dll", "mmioClose"},
    {0x006ca574u, "WINMM.dll", "timeSetEvent"},
    {0x006ca578u, "WINMM.dll", "timeBeginPeriod"},
    {0x006ca57cu, "WINMM.dll", "timeGetDevCaps"},
    {0x006ca580u, "WINMM.dll", "timeKillEvent"},
    {0x006ca584u, "WINMM.dll", "midiOutShortMsg"},
    {0x006ca588u, "WINMM.dll", "midiOutSetVolume"},
    {0x006ca58cu, "WINMM.dll", "midiOutOpen"},
    {0x006ca590u, "WINMM.dll", "midiOutClose"},
    {0x006ca594u, "WINMM.dll", "mciSendStringA"},
};

void bb_game_create(PeModule *module) {
#ifdef BB_MODEL_RUNTIME
  unsigned char *base = calloc(1, 0x31f000);
  if (!base || !bb_address_bind(0x00400000u, base, 0x31f000))
    bb_die("cannot allocate portable module data");
  module->reference = 0x00400000u;
#else
  unsigned char *base =
      mmap((void *)0x00400000, 0x31f000, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (base == MAP_FAILED)
    bb_die("cannot map native game compatibility state at 0x00400000");
#endif
  module->base = base;
  module->size = 0x31f000;
  module->entry = 0x6d3d0;
  module->builtin = BB_MODULE_GAME;
  snprintf(module->name, sizeof(module->name), "BEASTS.EXE");
  bb_game_initialize_data(base);
  bb_game_load_assets(base);
#ifndef BB_MODEL_RUNTIME
  memset(base + 0x1000, 0xcc, 0x81000);
  if (mprotect(base + 0x1000, 0x81000, PROT_NONE))
    bb_die("cannot protect retired code arena");
#endif
  PeModule *mixer = bb_pe_load("QMIXER95.DLL");
  for (size_t n = 0; n < sizeof(imports) / sizeof(imports[0]); ++n) {
    const struct GameImport *i = &imports[n];
    void *target =
        !strcmp(i->module, "QMIXER95.dll")
            ? bb_pe_export(mixer, i->name)
#ifdef BB_MODEL_RUNTIME
            : (void *)(uintptr_t)bb_platform_model_require(i->module, i->name);
#else
            : bb_api(i->module, i->name);
#endif
    if (!target)
      bb_die("missing %s export %s", i->module, i->name);
#ifdef BB_MODEL_RUNTIME
    BB_U32(i->slot) = (U32)(uintptr_t)target;
#else
    *(U32 *)i->slot = (uintptr_t)target;
#endif
  }
  fprintf(stderr, "Created native C game (no BEASTS.EXE file)\n");
}
