/* SPDX-License-Identifier: GPL-3.0-only */
#include "mixer_data.h"
#include "native.h"
#include <stdlib.h>
#ifdef BB_MODEL_RUNTIME
#include "../bridge/address.h"
#include "platform_models.h"
#endif
#include "../bridge/qmixer_vtables.h"
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

struct MixerImport {
  uintptr_t slot;
  const char *module, *name;
};
static const struct MixerImport imports[] = {
    {0x100212f8u, "USER32.dll", "TranslateMessage"},
    {0x100212fcu, "USER32.dll", "DestroyWindow"},
    {0x10021300u, "USER32.dll", "wsprintfA"},
    {0x10021304u, "USER32.dll", "CreateWindowExA"},
    {0x10021308u, "USER32.dll", "UnregisterClassA"},
    {0x1002130cu, "USER32.dll", "RegisterClassA"},
    {0x10021310u, "USER32.dll", "PostMessageA"},
    {0x10021314u, "USER32.dll", "DefWindowProcA"},
    {0x10021318u, "USER32.dll", "LoadStringA"},
    {0x1002131cu, "USER32.dll", "GetWindowThreadProcessId"},
    {0x10021320u, "USER32.dll", "GetActiveWindow"},
    {0x10021324u, "USER32.dll", "EnumWindows"},
    {0x10021328u, "USER32.dll", "PeekMessageA"},
    {0x1002132cu, "USER32.dll", "SetWindowLongA"},
    {0x10021330u, "USER32.dll", "CallWindowProcA"},
    {0x10021334u, "USER32.dll", "DispatchMessageA"},
    {0x1002133cu, "WINMM.dll", "waveOutUnprepareHeader"},
    {0x10021340u, "WINMM.dll", "waveOutGetNumDevs"},
    {0x10021344u, "WINMM.dll", "waveOutGetDevCapsA"},
    {0x10021348u, "WINMM.dll", "timeKillEvent"},
    {0x1002134cu, "WINMM.dll", "timeEndPeriod"},
    {0x10021350u, "WINMM.dll", "timeBeginPeriod"},
    {0x10021354u, "WINMM.dll", "timeSetEvent"},
    {0x10021358u, "WINMM.dll", "waveOutClose"},
    {0x1002135cu, "WINMM.dll", "waveOutRestart"},
    {0x10021360u, "WINMM.dll", "waveOutPause"},
    {0x10021364u, "WINMM.dll", "waveOutReset"},
    {0x10021368u, "WINMM.dll", "waveOutWrite"},
    {0x1002136cu, "WINMM.dll", "waveOutPrepareHeader"},
    {0x10021370u, "WINMM.dll", "waveOutOpen"},
    {0x10021374u, "WINMM.dll", "timeGetTime"},
    {0x10021378u, "WINMM.dll", "mmioClose"},
    {0x1002137cu, "WINMM.dll", "mmioOpenA"},
    {0x10021380u, "WINMM.dll", "mmioDescend"},
    {0x10021384u, "WINMM.dll", "mmioRead"},
    {0x10021388u, "WINMM.dll", "mmioAscend"},
    {0x100211f0u, "KERNEL32.dll", "GetProcAddress"},
    {0x100211f4u, "KERNEL32.dll", "LoadLibraryA"},
    {0x100211f8u, "KERNEL32.dll", "MapViewOfFile"},
    {0x100211fcu, "KERNEL32.dll", "GetStdHandle"},
    {0x10021200u, "KERNEL32.dll", "CreateFileMappingA"},
    {0x10021204u, "KERNEL32.dll", "SetHandleCount"},
    {0x10021208u, "KERNEL32.dll", "GetCurrentProcess"},
    {0x1002120cu, "KERNEL32.dll", "GetFileType"},
    {0x10021210u, "KERNEL32.dll", "ExitProcess"},
    {0x10021214u, "KERNEL32.dll", "VirtualAlloc"},
    {0x10021218u, "KERNEL32.dll", "TerminateProcess"},
    {0x1002121cu, "KERNEL32.dll", "HeapCreate"},
    {0x10021220u, "KERNEL32.dll", "HeapDestroy"},
    {0x10021224u, "KERNEL32.dll", "VirtualFree"},
    {0x10021228u, "KERNEL32.dll", "GetVersion"},
    {0x1002122cu, "KERNEL32.dll", "GetModuleHandleA"},
    {0x10021230u, "KERNEL32.dll", "SetUnhandledExceptionFilter"},
    {0x10021234u, "KERNEL32.dll", "LCMapStringW"},
    {0x10021238u, "KERNEL32.dll", "LCMapStringA"},
    {0x1002123cu, "KERNEL32.dll", "GetStringTypeW"},
    {0x10021240u, "KERNEL32.dll", "GetStringTypeA"},
    {0x10021244u, "KERNEL32.dll", "HeapSize"},
    {0x10021248u, "KERNEL32.dll", "HeapReAlloc"},
    {0x1002124cu, "KERNEL32.dll", "IsBadCodePtr"},
    {0x10021250u, "KERNEL32.dll", "IsBadReadPtr"},
    {0x10021254u, "KERNEL32.dll", "WriteFile"},
    {0x10021258u, "KERNEL32.dll", "WideCharToMultiByte"},
    {0x1002125cu, "KERNEL32.dll", "GetEnvironmentStringsW"},
    {0x10021260u, "KERNEL32.dll", "FreeEnvironmentStringsW"},
    {0x10021264u, "KERNEL32.dll", "GetEnvironmentStrings"},
    {0x10021268u, "KERNEL32.dll", "MultiByteToWideChar"},
    {0x1002126cu, "KERNEL32.dll", "FreeEnvironmentStringsA"},
    {0x10021270u, "KERNEL32.dll", "GetOEMCP"},
    {0x10021274u, "KERNEL32.dll", "GetACP"},
    {0x10021278u, "KERNEL32.dll", "GetCPInfo"},
    {0x1002127cu, "KERNEL32.dll", "GetStartupInfoA"},
    {0x10021280u, "KERNEL32.dll", "lstrcpyA"},
    {0x10021284u, "KERNEL32.dll", "lstrlenA"},
    {0x10021288u, "KERNEL32.dll", "CloseHandle"},
    {0x1002128cu, "KERNEL32.dll", "GetLastError"},
    {0x10021290u, "KERNEL32.dll", "LeaveCriticalSection"},
    {0x10021294u, "KERNEL32.dll", "EnterCriticalSection"},
    {0x10021298u, "KERNEL32.dll", "CreateFileA"},
    {0x1002129cu, "KERNEL32.dll", "SizeofResource"},
    {0x100212a0u, "KERNEL32.dll", "LockResource"},
    {0x100212a4u, "KERNEL32.dll", "LoadResource"},
    {0x100212a8u, "KERNEL32.dll", "FindResourceA"},
    {0x100212acu, "KERNEL32.dll", "FreeResource"},
    {0x100212b0u, "KERNEL32.dll", "UnmapViewOfFile"},
    {0x100212b4u, "KERNEL32.dll", "MulDiv"},
    {0x100212b8u, "KERNEL32.dll", "InitializeCriticalSection"},
    {0x100212bcu, "KERNEL32.dll", "DeleteCriticalSection"},
    {0x100212c0u, "KERNEL32.dll", "GetPrivateProfileIntA"},
    {0x100212c4u, "KERNEL32.dll", "QueryPerformanceCounter"},
    {0x100212c8u, "KERNEL32.dll", "OpenFile"},
    {0x100212ccu, "KERNEL32.dll", "GetModuleFileNameA"},
    {0x100212d0u, "KERNEL32.dll", "IsBadWritePtr"},
    {0x100212d4u, "KERNEL32.dll", "GetCurrentProcessId"},
    {0x100212d8u, "KERNEL32.dll", "SearchPathA"},
    {0x100212dcu, "KERNEL32.dll", "RaiseException"},
    {0x100212e0u, "KERNEL32.dll", "RtlUnwind"},
    {0x100212e4u, "KERNEL32.dll", "HeapFree"},
    {0x100212e8u, "KERNEL32.dll", "OutputDebugStringA"},
    {0x100212ecu, "KERNEL32.dll", "HeapAlloc"},
    {0x100212f0u, "KERNEL32.dll", "GetCommandLineA"},
};

void bb_mixer_create(PeModule *module) {
#ifdef BB_MODEL_RUNTIME
  unsigned char *base = calloc(1, 0x25000);
  if (!base || !bb_address_bind(0x10000000u, base, 0x25000))
    bb_die("cannot allocate portable module data");
  module->reference = 0x10000000u;
#else
  unsigned char *base =
      mmap((void *)0x10000000, 0x25000, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (base == MAP_FAILED)
    bb_die("cannot map native mixer compatibility state at 0x10000000");
#endif
  module->base = base;
  module->size = 0x25000;
  module->entry = 0x11f70;
  module->builtin = BB_MODULE_MIXER;
  snprintf(module->name, sizeof(module->name), "QMIXER95.DLL");
  bb_mixer_initialize_data(base);
  bb_mixer_initialize_resources();
  /* Logical code identities never execute from the compatibility arena. */
#ifndef BB_MODEL_RUNTIME
  memset(base + 0x1000, 0xcc, 0x19000);
  if (mprotect(base + 0x1000, 0x19000, PROT_NONE))
    bb_die("cannot protect retired code arena");
#endif
#ifdef BB_MODEL_RUNTIME
  for (size_t n = 0; n < sizeof(imports) / sizeof(imports[0]); ++n)
    BB_U32(imports[n].slot) =
        bb_platform_model_require(imports[n].module, imports[n].name);
#else
  for (size_t n = 0; n < sizeof(imports) / sizeof(imports[0]); ++n)
    *(U32 *)imports[n].slot =
        (uintptr_t)bb_api(imports[n].module, imports[n].name);
#endif
  static const uintptr_t descriptors[] = {0x1001e040, 0x1001e138, 0x1001f438};
  for (size_t n = 0; n < sizeof(descriptors) / sizeof(descriptors[0]); ++n)
#ifdef BB_MODEL_RUNTIME
    BB_U32(descriptors[n]) = BB_QMIX_VTABLE(bb_qmix_vtable_exception_object);
#else
    *(U32 *)descriptors[n] = (uintptr_t)bb_qmix_vtable_exception_object;
#endif
  fprintf(stderr, "Created native C mixer (no QMIXER95.DLL file)\n");
}
