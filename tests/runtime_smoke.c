/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise real host boundaries without any original game files. */
#include "native.h"
#include "platform_models.h"
#include "address.h"
#include "heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int bb_skip_intro, bb_trace, bb_fast;
uint32_t bb_language;
unsigned bb_stop_after_frames;
const char *bb_capture_path;

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
  exit(1); } } while (0)
static uint32_t invoke(const char *dll, const char *name,
                       const uint32_t *args, size_t count) {
  uint32_t id = bb_platform_model_export(dll, name);
  struct bb_call_state state = {0, 0, 0, 0x202};
  if (!id)
    fprintf(stderr, "Missing host service: %s!%s\n", dll, name);
  CHECK(id);
  bb_model_invoke(&state, id, args, count);
  return state.eax;
}
#define CALL(dll, name, ...) invoke(dll, name, (uint32_t[]){__VA_ARGS__}, \
                                  sizeof((uint32_t[]){__VA_ARGS__}) / 4)
#define K(name, ...) CALL("KERNEL32.dll", name, __VA_ARGS__)
#define U(name, ...) CALL("USER32.dll", name, __VA_ARGS__)
static void worker(uint32_t *r) {
  uint32_t argument = BB_U32(r[3] + 8);
  BB_U32(argument) = 0x12345678;
  r[7] = 0;
}
static const struct bb_model_target worker_target = {
    0x7e000100u, worker, 1, 0, 0};
static const struct bb_model_target *lookup(void *context, uint32_t id) {
  return id == worker_target.identity ? &worker_target
                                     : bb_platform_model_find(context, id);
}
static void key(Uint32 type, SDL_Keycode code, SDL_Keymod modifiers, int repeat) {
  SDL_Event e = {0};
  e.type = type;
  e.key.windowID = SDL_GetWindowID(bb_window);
  e.key.keysym.sym = code;
  e.key.keysym.scancode = SDL_GetScancodeFromKey(code);
  e.key.keysym.mod = modifiers;
  e.key.repeat = repeat;
  CHECK(SDL_PushEvent(&e) == 1);
  bb_pump_events();
}
int main(void) {
#ifdef _WIN32
  bb_host_initialize();
  SDL_SetMainReady();
#endif
  uint32_t stack = bb_heap_allocate(1024 * 1024);
  uint32_t scratch = bb_heap_zeroed(1, 4096);
  CHECK(stack && scratch && bb_model_attach(lookup, NULL, stack, 1024 * 1024));

  uint32_t pages = K("VirtualAlloc", 0, 65536, 0x2000, 4);
  CHECK(pages && K("IsBadReadPtr", pages, 1));
  CHECK(K("VirtualAlloc", pages, 4096, 0x1000, 4) == pages);
  memset(BB_SPAN(pages, 4096), 0x5a, 4096);
  CHECK(K("VirtualFree", pages, 4096, 0x4000));
  CHECK(K("IsBadReadPtr", pages, 1));
  CHECK(K("VirtualAlloc", pages, 4096, 0x1000, 4) == pages);
  for (unsigned i = 0; i < 4096; ++i)
    CHECK(BB_U8(pages + i) == 0);
  CHECK(K("VirtualFree", pages, 0, 0x8000));

  uint32_t pid = invoke("KERNEL32.dll", "GetCurrentProcessId", NULL, 0);
  snprintf(BB_DATA(scratch), 128, ".bb-ci-%u-\xe9.dat", pid);
  for (unsigned i = 0; i < 256; ++i)
    BB_U8(scratch + 256 + i) = (unsigned char)i;
  uint32_t file = K("CreateFileA", scratch, 0xc0000000u, 0, 0, 1, 0, 0);
  CHECK(file && file != UINT32_MAX);
  CHECK(K("WriteFile", file, scratch + 256, 256, scratch + 800, 0));
  CHECK(BB_U32(scratch + 800) == 256 && K("FlushFileBuffers", file));
  CHECK(K("SetFilePointer", file, 0, 0, 0) == 0);
  CHECK(K("ReadFile", file, scratch + 512, 256, scratch + 800, 0));
  CHECK(BB_U32(scratch + 800) == 256);
  CHECK(!memcmp(BB_SPAN(scratch + 256, 256), BB_SPAN(scratch + 512, 256), 256));
  CHECK(K("GetFileTime", file, scratch + 816, scratch + 824, scratch + 832));
  CHECK(BB_U64(scratch + 832) > UINT64_C(116444736000000000));
  uint32_t mapping = K("CreateFileMappingA", file, 0, 2, 0, 0, 0);
  CHECK(mapping);
  uint32_t view = K("MapViewOfFile", mapping, 4, 0, 0, 0);
  CHECK(view && !memcmp(BB_SPAN(view, 256), BB_SPAN(scratch + 256, 256), 256));
  CHECK(K("CloseHandle", mapping));
  CHECK(BB_U8(view + 26) == 26);
  CHECK(K("UnmapViewOfFile", view));
  CHECK(K("CloseHandle", file));
  char *path = bb_path(BB_DATA(scratch));
  CHECK(unlink(path) == 0);
  free(path);
  CHECK(K("GetDiskFreeSpaceA", 0, scratch + 800, scratch + 804,
          scratch + 808, scratch + 812));
  CHECK(BB_U32(scratch + 800) && BB_U32(scratch + 804));
  CHECK(K("GetTimeZoneInformation", scratch + 1024) != UINT32_MAX);
  BB_U16(scratch + 1400) = 1601;
  BB_U16(scratch + 1402) = 1;
  BB_U16(scratch + 1406) = 1;
  CHECK(K("SystemTimeToFileTime", scratch + 1400, scratch + 1424));
  CHECK(BB_U64(scratch + 1424) == 0);
  CHECK(K("FileTimeToSystemTime", scratch + 1424, scratch + 1440));
  CHECK(BB_U16(scratch + 1440) == 1601 && BB_U16(scratch + 1442) == 1 &&
        BB_U16(scratch + 1446) == 1);
  BB_U32(scratch + 1600) = 64;
  CHECK(!CALL("ADVAPI32.dll", "RegEnumValueA", 0, 0, scratch + 1616,
              scratch + 1600, 0, 0, 0, scratch + 1604));
  CHECK(!strcmp(BB_DATA(scratch + 1616), "Audio") &&
        BB_U32(scratch + 1604) == 10);

  uint32_t event = K("CreateEventA", 0, 0, 0, 0);
  CHECK(event && K("WaitForSingleObject", event, 2) == 258);
  CHECK(K("SetEvent", event));
  CHECK(K("WaitForSingleObject", event, 0) == 0);
  CHECK(K("WaitForSingleObject", event, 0) == 258 && K("CloseHandle", event));
  BB_U32(scratch + 900) = 0;
  uint32_t thread = K("CreateThread", 0, 0, worker_target.identity,
                       scratch + 900, 0, scratch + 904);
  CHECK(thread && BB_U32(scratch + 904));
  CHECK(K("WaitForSingleObject", thread, 2000) == 0);
  CHECK(BB_U32(scratch + 900) == 0x12345678 && K("CloseHandle", thread));

  CHECK(U("CreateWindowExA", 0, 0, 0, 0, 0, 0, 640, 480, 0, 0, 0, 0) == 1);
  bb_pump_events();
  key(SDL_KEYDOWN, SDLK_F11, KMOD_NONE, 0);
  key(SDL_KEYUP, SDLK_F11, KMOD_NONE, 0);
  CHECK(U("PeekMessageA", scratch + 1200, 0, 0x100, 0x102, 1));
  CHECK(BB_U32(scratch + 1204) == 0x100 && BB_U32(scratch + 1208) == 0x7a);
  CHECK(U("PeekMessageA", scratch + 1200, 0, 0x100, 0x102, 1));
  CHECK(BB_U32(scratch + 1204) == 0x101);
  key(SDL_KEYDOWN, SDLK_RETURN, KMOD_LALT, 0);
  CHECK(SDL_GetWindowFlags(bb_window) & SDL_WINDOW_FULLSCREEN);
  key(SDL_KEYDOWN, SDLK_RETURN, KMOD_NONE, 1);
  CHECK(SDL_GetWindowFlags(bb_window) & SDL_WINDOW_FULLSCREEN);
  key(SDL_KEYUP, SDLK_RETURN, KMOD_NONE, 0);
  key(SDL_KEYDOWN, SDLK_RETURN, KMOD_LALT, 0);
  CHECK(!(SDL_GetWindowFlags(bb_window) & SDL_WINDOW_FULLSCREEN));
  key(SDL_KEYUP, SDLK_RETURN, KMOD_NONE, 0);
  CHECK(!U("PeekMessageA", scratch + 1200, 0, 0x100, 0x102, 1));
  CHECK(SDL_ShowCursor(SDL_QUERY) == SDL_DISABLE && U("DestroyWindow", 1));
  CHECK(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0);
  SDL_AudioSpec desired = {0}, actual;
  desired.freq = 22050;
  desired.format = AUDIO_S16SYS;
  desired.channels = 2;
  desired.samples = 512;
  SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &desired, &actual, 0);
  CHECK(audio);
  int16_t samples[1024] = {0};
  CHECK(!SDL_QueueAudio(audio, samples, sizeof samples));
  SDL_CloseAudioDevice(audio);
  SDL_Quit();
  CHECK(bb_model_detach() && bb_heap_release(scratch) && bb_heap_release(stack));
  size_t count, bytes;
  for (unsigned i = 0; i < 2000; ++i) {
    bb_heap_usage(&count, &bytes);
    if (!count)
      break;
    SDL_Delay(1);
  }
  CHECK(!count && !bytes);
  puts("Host memory, binary files, mappings, Unicode paths, time, threads, input and audio pass");
  return 0;
}
