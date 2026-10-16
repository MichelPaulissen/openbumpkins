/* SPDX-License-Identifier: GPL-3.0-only */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "engine_hooks.h"
#include "qmixer_hooks.h"

/* This import makes Windows/Wine load the bridge before the original entry point. */
__declspec(dllexport) void bb_bridge_start(void) {}

static BOOL replace_entry(uintptr_t address, void (*target)(void)) {
    unsigned char *entry = (unsigned char *)address;
    unsigned char replacement[5] = {0xe9, 0, 0, 0, 0};
    uint32_t displacement = (uint32_t)(uintptr_t)target - ((uint32_t)address + 5);
    memcpy(replacement + 1, &displacement, sizeof(displacement));
    DWORD previous;
    if (!VirtualProtect(entry, sizeof(replacement), PAGE_EXECUTE_READWRITE, &previous))
        return FALSE;
    memcpy(entry, replacement, sizeof(replacement));
    DWORD ignored;
    return VirtualProtect(entry, sizeof(replacement), previous, &ignored) &&
           FlushInstructionCache(GetCurrentProcess(), entry, sizeof(replacement));
}

static BOOL install_hooks(void) {
    unsigned char *image = (unsigned char *)GetModuleHandleA(NULL);
    if ((uintptr_t)image != 0x00400000)
        return FALSE;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return FALSE;
    const IMAGE_NT_HEADERS32 *pe = (const IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->OptionalHeader.AddressOfEntryPoint != 0x0006d3d0)
        return FALSE;

    for (size_t n = 0; n < sizeof(bb_engine_hooks) / sizeof(bb_engine_hooks[0]); n++)
        if (memcmp((const void *)bb_engine_hooks[n].address, bb_engine_hooks[n].signature,
                   bb_engine_hooks[n].size) != 0)
            return FALSE;

    unsigned char *mixer = (unsigned char *)GetModuleHandleA("QMIXER95.DLL");
    if ((uintptr_t)mixer != 0x10000000)
        return FALSE;
    const IMAGE_DOS_HEADER *mixer_dos = (const IMAGE_DOS_HEADER *)mixer;
    if (mixer_dos->e_magic != IMAGE_DOS_SIGNATURE)
        return FALSE;
    const IMAGE_NT_HEADERS32 *mixer_pe =
        (const IMAGE_NT_HEADERS32 *)(mixer + mixer_dos->e_lfanew);
    if (mixer_pe->Signature != IMAGE_NT_SIGNATURE ||
        mixer_pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        mixer_pe->OptionalHeader.SizeOfImage != 0x25000)
        return FALSE;
    for (size_t n = 0; n < sizeof(bb_qmixer_hooks) / sizeof(bb_qmixer_hooks[0]); n++)
        if (memcmp((const void *)bb_qmixer_hooks[n].address, bb_qmixer_hooks[n].signature,
                   bb_qmixer_hooks[n].size) != 0)
            return FALSE;

    char disabled[2];
    if (GetEnvironmentVariableA("BB_BRIDGE_DISABLE", disabled, sizeof(disabled)) == 1 && disabled[0] == '1')
        return TRUE;

    for (size_t n = 0; n < sizeof(bb_engine_hooks) / sizeof(bb_engine_hooks[0]); n++)
        if (!replace_entry(bb_engine_hooks[n].address, bb_engine_hooks[n].replacement))
            return FALSE;
    for (size_t n = 0; n < sizeof(bb_qmixer_hooks) / sizeof(bb_qmixer_hooks[0]); n++)
        if (!replace_entry(bb_qmixer_hooks[n].address, bb_qmixer_hooks[n].replacement))
            return FALSE;
    bb_qmix_rebind_static_vtables();
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        /* No threads, registry operations, or loading other DLLs under the loader lock. */
        return install_hooks();
    }
    return TRUE;
}
