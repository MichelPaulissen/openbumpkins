/* SPDX-License-Identifier: GPL-3.0-only */
#include "../bridge/abi.h"
#ifndef BB_NATIVE_H
#define BB_NATIVE_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <SDL.h>
#include <stddef.h>
#include <stdint.h>
#define W BB_STDCALL
#ifdef BB_MODEL_RUNTIME
/* Logical Windows data can have byte alignment after relocation. Native host
 * objects remain private; these fixed-width scalar slots must permit packed
 * descriptor, rectangle and bitmap accesses on every supported CPU. */
typedef uint32_t U32 __attribute__((aligned(1), may_alias));
typedef int32_t I32 __attribute__((aligned(1), may_alias));
typedef uint16_t U16 __attribute__((aligned(1), may_alias));
#else
typedef uint32_t U32;
typedef int32_t I32;
typedef uint16_t U16;
#endif
typedef void *Handle;
typedef struct {
  I32 x, y;
} Point;
typedef struct {
  I32 left, top, right, bottom;
} Rect;
typedef I32(W *WindowProc)(Handle, U32, U32, I32);
typedef struct {
#ifdef BB_MODEL_RUNTIME
  U32 window;
#else
  Handle window;
#endif
  U32 message, wparam;
  I32 lparam;
  U32 time;
  Point point;
} Message;
typedef struct {
  U32 style;
  WindowProc procedure;
  I32 class_extra, window_extra;
  Handle instance, icon, cursor, background;
  const char *menu, *class_name;
} WindowClass;
typedef struct {
  unsigned char *base;
  size_t size;
  U32 entry;
  char name[64];
  int initialized, builtin;
#ifdef BB_MODEL_RUNTIME
  U32 reference;
#endif
} PeModule;
enum { BB_MODULE_MIXER = 1, BB_MODULE_GAME };

extern SDL_Window *bb_window;
extern WindowProc bb_window_proc;
extern int bb_dispatching_mouse_motion;
extern int bb_skip_intro, bb_trace, bb_fast;
extern U32 bb_language;
extern unsigned bb_present_count, bb_stop_after_frames;
extern const char *bb_capture_path;
extern const char *bb_cd_path;

void bb_die(const char *format, ...)
    __attribute__((noreturn, format(printf, 1, 2)));
PeModule *bb_pe_load(const char *path);
PeModule *bb_pe_find(const char *name);
void bb_pe_initialize(PeModule *module);
void *bb_pe_export(PeModule *module, const char *name);
PeModule *bb_pe_module(Handle handle);
void *bb_pe_resource(Handle module, const char *name, const char *type,
                     U32 *size);
void bb_mixer_create(PeModule *module);
void bb_mixer_initialize_resources(void);
void *bb_mixer_export(const char *name);
void *bb_mixer_resource(const char *name, const char *type, U32 *size);
void bb_game_create(PeModule *module);
void bb_game_load_assets(unsigned char *base);
void bb_game_unload_assets(void);
void bb_modules_reset(void);
void bb_resources_reset(void);
Handle bb_module_reference(const PeModule *);
void *bb_module_api(const char *);
void *bb_system_api(const char *);
void *bb_process_api(const char *);
void *bb_exception_filter_get(void);
void *bb_environment_api(const char *);
void *bb_locale_api(const char *);
void *bb_resource_api(const char *);
void *bb_game_resource(const char *name, const char *type, U32 *size);
void *bb_api(const char *dll, const char *name);
const char *bb_library_name(const char *name);
Handle bb_module_handle(const char *name);
const char *bb_module_name(Handle handle);
void *bb_module_export(Handle handle, const char *name);
void *bb_library_api(const char *dll, const char *name);
void *bb_kernel_api(const char *name);
void *bb_settings_api(const char *name);
void *bb_clock_api(const char *name);
void *bb_sync_api(const char *name);
void *bb_timer_api(const char *name);
/* Collect timer records after owners stop and SDL_Quit joins its worker. */
void bb_timer_shutdown(void);
void bb_timer_set_wave_pump(void (*)(void));
void *bb_memory_api(const char *name);
void *bb_window_api(const char *name);
void *bb_draw_api(const char *name);
void *bb_audio_api(const char *name);
I32 W bb_sound_create(const void *guid, Handle *output, Handle outer);
int bb_sound_ready(void);
U32 W bb_cd_command(const char *command, char *result, U32 capacity,
                    Handle window);
void bb_cd_mix(int16_t *output, unsigned frames);
void bb_patch_jump(U32 address, void (*target)(void));
void bb_init_teb(void);
void bb_free_teb(void);
void bb_native_initialize_entries(void);
uintptr_t bb_native_entry(uintptr_t address);
void bb_exception_raise(U32, U32, U32, const U32 *, void *);
void bb_exception_unwind(void *, void *, void *, void *);
char *bb_path(const char *windows_path);
/* Case-insensitive host lookup for UTF-8 paths already owned by native code. */
char *bb_host_path(const char *host_path);
char *bb_ansi_path(const char *host_path);
/* Borrow the descriptor; the caller must duplicate it before taking ownership. */
int bb_file_descriptor(Handle handle);
void bb_pump_events(void);
int bb_native_dispatch_pending(U32 *quit_code);
void bb_display_pixels(const unsigned char *pixels, int width, int height,
                       int pitch, int bits, const unsigned char *palette);
void bb_draw_reset_presentation(void);
U32 W bb_tick(void);
U32 bb_time_zone_information(int64_t seconds, void *output);
U32 bb_profile_integer(const char *section, const char *key, I32 fallback,
                       const char *filename);
#define API_ENTRY(name) {#name, (void *)(uintptr_t)name}
#define API_RETURN(table)                                                      \
  do {                                                                         \
    for (size_t n = 0; n < sizeof(table) / sizeof((table)[0]); n++)            \
      if (!strcmp(name, (table)[n].name))                                      \
        return (table)[n].address;                                             \
    return NULL;                                                               \
  } while (0)
struct ApiEntry {
  const char *name;
  void *address;
};
#endif
