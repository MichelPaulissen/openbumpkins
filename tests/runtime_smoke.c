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
static void address_indexes(void) {
  unsigned char storage[128][32];
  uint32_t encoded;
  void *decoded;
  for (unsigned i = 0; i < 128; ++i)
    CHECK(bb_address_bind(0x72000000u + ((i * 73) % 128) * 64,
                           storage[(i * 37) % 128], 16));
  for (unsigned i = 0; i < 128; ++i) {
    uint32_t logical = 0x72000000u + ((i * 73) % 128) * 64;
    unsigned char *host = storage[(i * 37) % 128];
    CHECK(bb_address_try_encode(host + 15, &encoded) && encoded == logical + 15);
    CHECK(bb_address_try_pointer(logical + 15, 1, &decoded) && decoded == host + 15);
    CHECK(!bb_address_try_pointer(logical + 15, 2, &decoded));
    CHECK(!bb_address_try_pointer(logical + 16, 1, &decoded));
    CHECK(!bb_address_bind(0x73000000u, host + 1, 1));
    CHECK(!bb_address_bind(logical + 8, storage[0] + 24, 1));
  }
  for (unsigned parity = 0; parity < 2; ++parity)
    for (unsigned i = parity; i < 128; i += 2) {
      uint32_t logical = 0x72000000u + ((i * 73) % 128) * 64;
      CHECK(bb_address_unbind(logical));
      CHECK(!bb_address_unbind(logical));
    }
  /* A low host address may itself have a mapping; preserve that precedence
   * over the fallback which accepts already-logical four-byte pointers. */
  CHECK(bb_address_bind(0x74000000u, (void *)(uintptr_t)0x123400, 16));
  CHECK(bb_address_try_encode((void *)(uintptr_t)0x123408, &encoded) &&
        encoded == 0x74000008u);
  CHECK(bb_address_unbind(0x74000000u));
  CHECK(bb_address_try_encode((void *)(uintptr_t)0x123408, &encoded) &&
        encoded == 0x123408);
}
static uint32_t drawing(uint32_t object, uint32_t offset,
                         const uint32_t *tail, size_t count) {
  uint32_t args[8] = {object};
  CHECK(count < 8);
  memcpy(args + 1, tail, count * sizeof *args);
  struct bb_call_state state = {0, 0, 0, 0x202};
  bb_model_invoke(&state, BB_U32(BB_U32(object) + offset), args, count + 1);
  return state.eax;
}
#define D(object, offset, ...) drawing(object, offset, (uint32_t[]){__VA_ARGS__}, \
                                       sizeof((uint32_t[]){__VA_ARGS__}) / 4)
static uint32_t make_surface(uint32_t draw, uint32_t scratch, unsigned width,
                              unsigned height, unsigned bits) {
  memset(BB_SPAN(scratch, 108), 0, 108);
  BB_U32(scratch) = 108;
  BB_U32(scratch + 4) = 7 | 0x1000;
  BB_U32(scratch + 8) = height;
  BB_U32(scratch + 12) = width;
  BB_U32(scratch + 84) = bits;
  BB_U32(scratch + 104) = 0x840;
  CHECK(!D(draw, 0x18, scratch, scratch + 400, 0));
  return BB_U32(scratch + 400);
}
static unsigned char *lock_surface(uint32_t surface, uint32_t scratch,
                                    uint32_t *pitch) {
  CHECK(!D(surface, 0x64, 0, scratch, 1, 0));
  *pitch = BB_U32(scratch + 16);
  return BB_DATA(BB_U32(scratch + 36));
}
static void blit_pixels(uint32_t scratch) {
  CHECK(!CALL("DDRAW.dll", "DirectDrawCreate", 0, scratch + 400, 0));
  uint32_t draw = BB_U32(scratch + 400);
  uint32_t random = 0x12345678;
  for (unsigned bits = 8; bits <= 32; bits *= 2) {
    unsigned bytes = bits / 8;
    uint32_t source = make_surface(draw, scratch, 17, 13, bits);
    uint32_t target = make_surface(draw, scratch, 19, 11, bits);
    uint32_t sp, dp;
    unsigned char *input = lock_surface(source, scratch, &sp);
    unsigned char *output = lock_surface(target, scratch, &dp);
    unsigned char original[13 * 68], expected[11 * 76];
    for (unsigned i = 0; i < 13 * sp; ++i)
      original[i] = input[i] = (unsigned char)(i * 37);
    CHECK(!D(source, 0x80, 0) && !D(target, 0x80, 0));
    BB_U32(scratch + 288) = 32;
    BB_U32(scratch + 292) = 160;
    CHECK(!D(source, 0x74, 8, scratch + 288));
    CHECK(!D(target, 0x74, 8, scratch + 288));
    for (unsigned test = 0; test < 240; ++test) {
      random = random * 1664525u + 1013904223u;
      int left = (int)(random % 37) - 12, top = (int)((random >> 8) % 23) - 8;
      int width = 1 + (int)((random >> 16) % 24);
      int height = 1 + (int)((random >> 24) % 19);
      int sx = (int)((random >> 5) % 25) - 5;
      int sy = (int)((random >> 13) % 19) - 4;
      int sw = width + (test % 5 == 0 ? 3 : 0);
      int sh = height + (test % 9 == 0 ? 2 : 0);
      int32_t dest[4] = {left, top, left + width, top + height};
      int32_t src[4] = {sx, sy, sx + sw, sy + sh};
      memcpy(BB_SPAN(scratch + 256, 16), src, 16);
      memcpy(BB_SPAN(scratch + 272, 16), dest, 16);
      int alias = test % 11 == 0;
      uint32_t flags = test % 2 ? 0x8000 : 0;
      if (test % 7 == 0)
        flags |= 0x400;
      memset(BB_SPAN(scratch + 512, 108), 0, 108);
      BB_U32(scratch + 512 + 80) = 0x12345678;
      output = lock_surface(target, scratch, &dp);
      memset(output, 0xa5, dp * 11);
      memset(expected, 0xa5, dp * 11);
      CHECK(!D(target, 0x80, 0));
      for (int y = top; y < top + height; ++y)
        for (int x = left; x < left + width; ++x) {
          if (x < 0 || y < 0 || x >= 19 || y >= 11)
            continue;
          uint32_t pixel = 0x12345678;
          if (!(flags & 0x400)) {
            int px = sx + (x - left) * sw / width;
            int py = sy + (y - top) * sh / height;
            if (px < 0 || py < 0 || px >= (alias ? 19 : 17) ||
                py >= (alias ? 11 : 13))
              continue;
            pixel = 0;
            memcpy(&pixel, (alias ? expected : original) +
                   py * (alias ? dp : sp) + px * bytes, bytes);
            if ((flags & 0x8000) && pixel >= 32 && pixel <= 160)
              continue;
          }
          memcpy(expected + y * dp + x * bytes, &pixel, bytes);
        }
      CHECK(!D(target, 0x14, scratch + 272, alias ? target : source,
                scratch + 256, flags, scratch + 512));
      output = lock_surface(target, scratch, &dp);
      CHECK(!memcmp(expected, output, dp * 11));
      CHECK(!D(target, 0x80, 0));
    }
    CHECK(!D(source, 8, 0) && !D(target, 8, 0));
  }
  CHECK(!D(draw, 8, 0));
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
  address_indexes();
  uint32_t stack = bb_heap_allocate(1024 * 1024);
  uint32_t scratch = bb_heap_zeroed(1, 4096);
  CHECK(stack && scratch && bb_model_attach(lookup, NULL, stack, 1024 * 1024));
  blit_pixels(scratch);

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
  static const struct { uint16_t year, month, day; uint64_t ticks; } dates[] = {
      {1601, 1, 1, 0},
      {1899, 12, 31, UINT64_C(94353984000000000)},
      {1900, 3, 1, UINT64_C(94405824000000000)},
      {1969, 12, 31, UINT64_C(116443872000000000)},
      {1970, 1, 1, UINT64_C(116444736000000000)},
      {2000, 2, 29, UINT64_C(125962560000000000)},
      {2038, 1, 19, UINT64_C(137919456000000000)},
      {2100, 3, 1, UINT64_C(157520160000000000)},
      {9999, 12, 31, UINT64_C(2650466880000000000)}};
  for (unsigned i = 0; i < sizeof(dates) / sizeof(*dates); ++i) {
    BB_U16(scratch + 1400) = dates[i].year;
    BB_U16(scratch + 1402) = dates[i].month;
    BB_U16(scratch + 1406) = dates[i].day;
    CHECK(K("SystemTimeToFileTime", scratch + 1400, scratch + 1424));
    CHECK(BB_U64(scratch + 1424) == dates[i].ticks);
    CHECK(K("FileTimeToSystemTime", scratch + 1424, scratch + 1440));
    CHECK(BB_U16(scratch + 1440) == dates[i].year &&
          BB_U16(scratch + 1442) == dates[i].month &&
          BB_U16(scratch + 1446) == dates[i].day);
  }
  BB_U32(scratch + 1600) = 64;
  CHECK(!CALL("ADVAPI32.dll", "RegEnumValueA", 0, 0, scratch + 1616,
              scratch + 1600, 0, 0, 0, scratch + 1604));
  CHECK(!strcmp(BB_DATA(scratch + 1616), "Audio") &&
        BB_U32(scratch + 1604) == 10);

  uint32_t event = K("CreateEventA", 0, 0, 0, 0);
  if (!event)
    fprintf(stderr, "CreateEventA failed: %u\n",
            invoke("KERNEL32.dll", "GetLastError", NULL, 0));
  CHECK(event);
  uint32_t waited = K("WaitForSingleObject", event, 2);
  if (waited != 258)
    fprintf(stderr, "Timed wait returned %u, error %u\n", waited,
            invoke("KERNEL32.dll", "GetLastError", NULL, 0));
  CHECK(waited == 258);
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
  CHECK(SDL_ShowCursor(SDL_QUERY) == SDL_DISABLE);
  /* SDL converts window events to game coordinates, including letterboxing.
   * Exercise both pillarboxing and letterboxing at larger internal sizes. */
  static const int sizes[][2] = {{640, 480}, {1280, 720}, {1920, 1080}};
  SDL_SetWindowSize(bb_window, 1000, 600);
  for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; ++i) {
    int width = sizes[i][0], height = sizes[i][1];
    unsigned char *pixels = calloc((size_t)width, (size_t)height);
    CHECK(pixels);
    bb_display_pixels(pixels, width, height, width, 8, NULL);
    free(pixels);
    SDL_Event click = {0};
    click.type = SDL_MOUSEBUTTONDOWN;
    click.button.windowID = SDL_GetWindowID(bb_window);
    click.button.button = SDL_BUTTON_LEFT;
    click.button.x = 500;
    click.button.y = 300;
    CHECK(SDL_PushEvent(&click) == 1);
    bb_pump_events();
    CHECK(U("PeekMessageA", scratch + 1200, 0, 0x201, 0x201, 1));
    uint32_t point = BB_U32(scratch + 1212);
    CHECK(abs((int)(point & 0xffff) - width / 2) <= 1);
    CHECK(abs((int)(point >> 16) - height / 2) <= 1);
    /* Clicks outside the logical image must not activate game controls. */
    click.button.x = 0;
    click.button.y = 0;
    CHECK(SDL_PushEvent(&click) == 1);
    bb_pump_events();
    CHECK(!U("PeekMessageA", scratch + 1200, 0, 0x201, 0x201, 1));
  }
  CHECK(U("DestroyWindow", 1));
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
