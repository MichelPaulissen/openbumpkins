/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include <stddef.h>
#include <string.h>
#ifdef BB_MODEL_RUNTIME
#include "render_calls.h"
#else

typedef void *(BB_STDCALL *find_resource_fn)(void *, const char *,
                                             const char *);
typedef void *(BB_STDCALL *load_resource_fn)(void *, void *);
typedef void *(BB_STDCALL *lock_resource_fn)(void *);
typedef int32_t(BB_STDCALL *open_fn)(const char *, uint32_t);
typedef uint32_t(BB_STDCALL *read_fn)(int32_t, void *, uint32_t);
typedef uint32_t(BB_STDCALL *close_fn)(int32_t);
typedef uint32_t(BB_STDCALL *create_palette_fn)(void *, uint32_t, void *,
                                                void *, void *);
typedef uint32_t(BB_STDCALL *entries_fn)(void *, uint32_t, uint32_t, uint32_t,
                                         void *);
typedef uint32_t(BB_STDCALL *attach_fn)(void *, void *);
typedef int (*format_fn)(char *, const char *, ...);
typedef void *(*file_open_fn)(const char *, const char *);
typedef uint32_t (*file_io_fn)(void *, uint32_t, uint32_t, void *);
typedef uint32_t (*file_close_fn)(void *);

#endif
static unsigned char byte(uint32_t address) { return BB_U8(address); }
static void put(uint32_t address, unsigned char value) {
  BB_U8(address) = value;
}
#ifndef BB_MODEL_RUNTIME
static void *method(void *object, uint32_t offset) {
  return BB_POINTER(BB_U32(bb_address_encode(object)) + offset);
}
#endif
static uint32_t entries(void *palette, uint32_t method_offset, uint32_t start,
                        uint32_t count, uint32_t colors) {
#ifdef BB_MODEL_RUNTIME
  return bb_render_model_method(bb_address_encode(palette), method_offset,
                                (uint32_t[]){0, start, count, colors}, 4);
#else
  return ((entries_fn)BB_CODE(method(palette, method_offset)))(
      palette, 0, start, count, (void *)(uintptr_t)colors);
#endif
}

/* 0x00431120..0x004312cf: default RGB332, then a DIB resource or bitmap file.
 * Resource RGBQUADs lose their reserved byte; file entries retain it. A failed
 * CreatePalette's output is indeterminate unless the callback writes it; the C
 * temporary starts null for that otherwise undefined path. Truncated file
 * headers likewise leave original fields indeterminate; the C header starts
 * zero.
 */
static void *load(void *draw, const char *name, uint32_t scratch) {
  unsigned char *colors = BB_SPAN(scratch, 1024);
  for (uint32_t i = 0; i < 256; ++i) {
    colors[4 * i] = ((i >> 5) & 7) * 255 / 7;
    colors[4 * i + 1] = ((i >> 2) & 7) * 255 / 7;
    colors[4 * i + 2] = (i & 3) * 255 / 3;
    colors[4 * i + 3] = 0;
  }
  if (name) {
#ifdef BB_MODEL_RUNTIME
    uint32_t resource = bb_render_model_service(
        BB_U32(0x006ca3b4), 0, (uint32_t[]){0, bb_address_encode(name), 2}, 3);
#else
    void *resource = ((find_resource_fn)BB_CODE(BB_POINTER(0x006ca3b4)))(
        NULL, name, (const char *)(uintptr_t)2);
#endif
    if (resource) {
#ifdef BB_MODEL_RUNTIME
      uint32_t loaded = bb_render_model_service(BB_U32(0x006ca3b0), 0,
                                                (uint32_t[]){0, resource}, 2);
      uint32_t bitmap = bb_render_model_service(BB_U32(0x006ca3ac), 0,
                                                (uint32_t[]){loaded}, 1);
#else
      void *loaded =
          ((load_resource_fn)BB_CODE(BB_POINTER(0x006ca3b0)))(NULL, resource);
      uintptr_t bitmap = (uintptr_t)((lock_resource_fn)BB_CODE(
          BB_POINTER(0x006ca3ac)))(loaded);
#endif
      /* The original reads the header before testing the resource pointer. */
      uint32_t header = BB_U32(bitmap), count = 0;
      if (bitmap && header >= 40) {
        uint16_t bits = BB_U16(bitmap + 14);
        if (bits <= 8) {
          count = BB_U32(bitmap + 32);
          if (!count)
            count = 1u << (bits & 31);
        }
      }
      if ((int32_t)count > 0)
        for (uint32_t i = 0; i < count; ++i) {
          uint32_t entry = bitmap + header + i * 4;
          colors[i * 4] = byte(entry + 2);
          colors[i * 4 + 1] = byte(entry + 1);
          colors[i * 4 + 2] = byte(entry);
          colors[i * 4 + 3] = 0;
        }
    } else {
#ifdef BB_MODEL_RUNTIME
      int32_t file = bb_render_model_service(
          BB_U32(0x006ca3a8), 0, (uint32_t[]){bb_address_encode(name), 0}, 2);
#else
      int32_t file = ((open_fn)BB_CODE(BB_POINTER(0x006ca3a8)))(name, 0);
#endif
      if (file != -1) {
        unsigned char *header = BB_SPAN(scratch + 1040, 40);
        memset(header, 0, 40);
#ifdef BB_MODEL_RUNTIME
        uint32_t read = BB_U32(0x006ca3a4);
        bb_render_model_service(read, 0, (uint32_t[]){file, scratch + 1024, 14},
                                3);
        bb_render_model_service(read, 0, (uint32_t[]){file, scratch + 1040, 40},
                                3);
        bb_render_model_service(read, 0, (uint32_t[]){file, scratch, 1024}, 3);
        bb_render_model_service(BB_U32(0x006ca3a0), 0, (uint32_t[]){file}, 1);
#else
        read_fn read = (read_fn)BB_CODE(BB_POINTER(0x006ca3a4));
        read(file, (void *)(uintptr_t)(scratch + 1024), 14);
        read(file, header, 40);
        read(file, colors, 1024);
        ((close_fn)BB_CODE(BB_POINTER(0x006ca3a0)))(file);
#endif
        uint16_t bits;
        uint32_t size, count;
        memcpy(&size, header, 4);
        memcpy(&bits, header + 14, 2);
        memcpy(&count, header + 32, 4);
        if (size != 40 || bits > 8)
          count = 0;
        else if (!count)
          count = 1u << (bits & 31);
        if ((int32_t)count > 0)
          for (uint32_t i = 0; i < count; ++i) {
            unsigned char red = colors[4 * i];
            colors[4 * i] = colors[4 * i + 2];
            colors[4 * i + 2] = red;
          }
      }
    }
  }
  BB_U32(scratch + 1080) = 0;
#ifdef BB_MODEL_RUNTIME
  bb_render_model_method(bb_address_encode(draw), 0x14,
                         (uint32_t[]){4, scratch, scratch + 1080, 0}, 4);
#else
  ((create_palette_fn)BB_CODE(method(draw, 0x14)))(
      draw, 4, colors, (void *)(uintptr_t)(scratch + 1080), NULL);
#endif
  return BB_POINTER(scratch + 1080);
}

/* 0x00431e80..0x00431f11: ties choose the first index; water colors 80..89
 * never participate in matching. Only the low byte of each input is used.
 */
uint32_t BB_THISCALL bb_palette_nearest(void *object, uint32_t red,
                                        unsigned char green,
                                        unsigned char blue) {
  uint32_t owner = bb_address_encode(object);
  uint32_t result = 0, minimum = UINT32_MAX;
  for (uint32_t i = 0; i < 256; ++i) {
    if (i >= 80 && i < 90)
      continue;
    int32_t r = (red & 255) - byte(owner + 0xabc + i);
    int32_t g = green - byte(owner + 0x9bc + i);
    int32_t b = blue - byte(owner + 0x8bc + i);
    uint32_t distance = r * r + g * g + b * b;
    if (distance < minimum) {
      minimum = distance;
      result = i;
    }
  }
  return result;
}

/* 0x00431f20..0x00431fcc: blue-toned alternate colors, reserved water indices,
 * zero transparency and the two global sprite remap tables.
 */
uint32_t BB_THISCALL bb_palette_remap(void *object) {
  uint32_t owner = bb_address_encode(object);
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t value = (byte(owner + 0xabc + i) + byte(owner + 0x9bc + i) +
                      byte(owner + 0x8bc + i)) /
                     4 * 10 / 8;
    if (value > 255)
      value = 255;
    put(owner + 0xbbc + i, bb_palette_nearest(object, 0, 0, value));
  }
  for (uint32_t i = 80; i < 90; ++i)
    put(owner + 0xbbc + i, i);
  put(owner + 0xbbc, 0);
  for (uint32_t i = 0; i < 256; ++i)
    put(0x006b1f20 + i, byte(owner + 0xbbc + i));
  for (uint32_t i = 0; i < 256; ++i)
    put(0x006b2020 + i, i);
  return 256;
}
/* 0x00431fd0..0x0043202f: subtract 32 per channel, with zero transparency. */
uint32_t BB_THISCALL bb_palette_shade(void *object) {
  uint32_t owner = bb_address_encode(object);
  uint32_t result = 0;
  for (uint32_t i = 0; i < 256; ++i) {
    unsigned char r = byte(owner + 0xabc + i), g = byte(owner + 0x9bc + i),
                  b = byte(owner + 0x8bc + i);
    result = bb_palette_nearest(object, r > 32 ? r - 32 : 0,
                                g > 32 ? g - 32 : 0, b > 32 ? b - 32 : 0);
    put(owner + 0xcbc + i, result);
  }
  put(owner + 0xcbc, 0);
  return result;
}

static uint32_t cache_open(uint32_t filename, uint32_t format, uint32_t mode) {
#ifdef BB_MODEL_RUNTIME
  bb_render_call(0x0046bae0, 0,
                          (uint32_t[]){filename, format, 0x00559a58}, 3);
  return bb_render_model_service(0x0046bdd0, 0, (uint32_t[]){filename, mode},
                                 2);
#else
  ((format_fn)BB_CODE((uintptr_t)0x0046bae0))(
      (char *)(uintptr_t)filename, (const char *)(uintptr_t)format,
      (const char *)(uintptr_t)0x00559a58);
  return bb_address_encode(((file_open_fn)BB_CODE((uintptr_t)0x0046bdd0))(
      (const char *)(uintptr_t)filename, (const char *)(uintptr_t)mode));
#endif
}
static uint32_t cache_close(uint32_t file) {
#ifdef BB_MODEL_RUNTIME
  return bb_render_model_service(0x0046bbd0, 0, (uint32_t[]){file}, 1);
#else
  return ((file_close_fn)BB_CODE((uintptr_t)0x0046bbd0))(
      (void *)(uintptr_t)file);
#endif
}
static void cache_read(uint32_t file, uint32_t table) {
#ifdef BB_MODEL_RUNTIME
  bb_render_model_service(0x0046cf40, 0, (uint32_t[]){table, 32768, 2, file},
                          4);
#else
  ((file_io_fn)BB_CODE((uintptr_t)0x0046cf40))((void *)(uintptr_t)table, 32768,
                                               2, (void *)(uintptr_t)file);
#endif
}
static uint32_t cache_write(uint32_t filename, uint32_t table) {
#ifdef BB_MODEL_RUNTIME
  uint32_t file = bb_render_model_service(
      0x0046bdd0, 0, (uint32_t[]){filename, 0x00486ed4}, 2);
#else
  uint32_t file = bb_address_encode((
      (file_open_fn)BB_CODE((uintptr_t)0x0046bdd0))(
      (const char *)(uintptr_t)filename, (const char *)(uintptr_t)0x00486ed4));
#endif
  if (!file)
    return 0;
#ifdef BB_MODEL_RUNTIME
  bb_render_model_service(0x0046cd20, 0, (uint32_t[]){table, 32768, 2, file},
                          4);
#else
  ((file_io_fn)BB_CODE((uintptr_t)0x0046cd20))((void *)(uintptr_t)table, 32768,
                                               2, (void *)(uintptr_t)file);
#endif
  return cache_close(file);
}

/* 0x00432030..0x00432231: the original uses the row's blue channel in both
 * blue terms. Keep its double stores and extended intermediates before __ftol.
 * Cache reads/writes deliberately ignore short transfers.
 */
static uint32_t mix(void *object, uint32_t filename) {
  uint32_t owner = bb_address_encode(object), table = owner + 0xebc;
  uint32_t file = cache_open(filename, 0x00488920, 0x0048416c);
  if (file) {
    cache_read(file, table);
    return cache_close(file);
  }
  for (uint32_t row = 0; row < 256; ++row) {
    volatile double r =
        (long double)byte(owner + 0xabc + row) * BB_DOUBLE(0x00482260);
    volatile double g =
        (long double)byte(owner + 0x9bc + row) * BB_DOUBLE(0x00482260);
    volatile double b =
        (long double)byte(owner + 0x8bc + row) * BB_DOUBLE(0x00482260);
    for (uint32_t column = 0; column < 256; ++column) {
      long double red =
          (long double)byte(owner + 0xabc + column) * BB_DOUBLE(0x00482260) + r;
      long double green =
          (long double)byte(owner + 0x9bc + column) * BB_DOUBLE(0x00482260) + g;
      long double blue =
          (long double)byte(owner + 0x8bc + row) * BB_DOUBLE(0x00482260) + b;
      put(table + row * 256 + column,
          bb_palette_nearest(object, (int32_t)red,
                             (unsigned char)(int32_t)green,
                             (unsigned char)(int32_t)blue));
    }
  }
  for (uint32_t row = 80; row < 90; ++row)
    memset(BB_SPAN(table + row * 256, 256), row, 256);
  return cache_write(filename, table);
}

/* 0x00432240..0x004324bb: most reflection entries copy the background index.
 * Columns 136..158 mix 10% row with 90% column. The blue term again uses the
 * row. Intermediate 64-bit stores affect truncation at integer boundaries.
 */
static uint32_t reflect(void *object, uint32_t filename) {
  uint32_t owner = bb_address_encode(object), table = owner + 0x10ebc;
  uint32_t file = cache_open(filename, 0x0048892c, 0x0048416c);
  if (file) {
    cache_read(file, table);
    return cache_close(file);
  }
  for (uint32_t i = 0; i < 65536; ++i)
    put(table + i, i);
  for (uint32_t row = 0; row < 256; ++row) {
    volatile double r =
        (long double)byte(owner + 0xabc + row) * BB_DOUBLE(0x00482268);
    volatile double g =
        (long double)byte(owner + 0x9bc + row) * BB_DOUBLE(0x00482268);
    volatile double b =
        (long double)byte(owner + 0x8bc + row) * BB_DOUBLE(0x00482268);
    for (uint32_t column = 136; column < 159; ++column) {
      long double red =
          (long double)byte(owner + 0xabc + column) * BB_DOUBLE(0x00482270) + r;
      volatile double inner_green =
          (long double)byte(owner + 0x9bc + column) * BB_DOUBLE(0x00482270);
      volatile double inner_blue =
          (long double)byte(owner + 0x8bc + row) * BB_DOUBLE(0x00482270);
      long double green = (long double)inner_green + g,
                  blue = (long double)inner_blue + b;
      volatile double final_red = red > BB_DOUBLE(0x00482280) ? 255 : red;
      volatile double final_green = green > BB_DOUBLE(0x00482280) ? 255 : green;
      volatile double final_blue = blue > BB_DOUBLE(0x00482280) ? 255 : blue;
      put(table + row * 256 + column,
          bb_palette_nearest(object, (int32_t)final_red,
                             (unsigned char)(int32_t)final_green,
                             (unsigned char)(int32_t)final_blue));
    }
  }
  return cache_write(filename, table);
}

/* 0x00432c60..0x00432db7: approach target by delta/4 +/-3, then snap. Only RGB
 * bytes change; the fourth byte of every outgoing entry remains untouched.
 */
uint32_t BB_THISCALL bb_palette_fade(void *object, uint32_t force) {
  uint32_t owner = bb_address_encode(object);
  int32_t current = byte(owner + 0x20ebc), target = byte(owner + 0x20ebd);
  if (current == target && !force)
    return 0;
  if (current < target - 3)
    current += (target - current) / 4 + 3;
  else if (current > target + 3)
    current += (target - current) / 4 - 3;
  else
    current = target;
  put(owner + 0x20ebc, current);
  for (uint32_t i = 0; i < 256; ++i)
    for (uint32_t channel = 0; channel < 3; ++channel) {
      uint32_t source = 0xabc - channel * 256;
#ifdef BB_MODEL_RUNTIME
      /* Game startup selects 53-bit x87 precision. Round each multiplication
       * explicitly so the portable result does not depend on the host's
       * long-double format or ambient extended-precision control word. */
      volatile double factor =
          (double)byte(owner + 0x20ebc) * BB_DOUBLE(0x00482288);
      volatile double value = factor * byte(owner + source + i);
      put(owner + 0x4bc + i * 4 + channel, (int32_t)value);
#else
      long double factor =
          (long double)byte(owner + 0x20ebc) * BB_DOUBLE(0x00482288);
      put(owner + 0x4bc + i * 4 + channel,
          (int32_t)(factor * byte(owner + source + i)));
#endif
    }
  return entries(BB_POINTER(owner + 0xb8), 0x18, 0, 256, owner + 0x4bc);
}

/* 0x0042ef20..0x0042f026: capture water and full RGB entries, apply the fade
 * and attach the palette. The base PALETTE prepares lookup maps only once.
 */
static uint32_t install(void *object, const char *name, uint32_t scratch) {
  uint32_t owner = bb_address_encode(object);
  BB_SET_POINTER(owner + 0xb8, bb_palette_load(BB_POINTER(owner + 0x30), name));
  entries(BB_POINTER(owner + 0xb8), 0x10, 80, 10, owner + 0xbc);
  /* A failed getter that writes no entries leaves indeterminate original data.
   */
  unsigned char *colors = BB_SPAN(scratch, 1024);
  memset(colors, 0, 1024);
  entries(BB_POINTER(owner + 0xb8), 0x10, 0, 256, scratch);
  for (uint32_t i = 0; i < 256; ++i) {
    put(owner + 0xabc + i, colors[i * 4]);
    put(owner + 0x9bc + i, colors[i * 4 + 1]);
    put(owner + 0x8bc + i, colors[i * 4 + 2]);
  }
  bb_palette_fade(object, 1);
  void *surface = BB_POINTER(owner + 0x34);
#ifdef BB_MODEL_RUNTIME
  bb_render_model_method(bb_address_encode(surface), 0x7c,
                         (uint32_t[]){BB_U32(owner + 0xb8)}, 1);
#else
  ((attach_fn)BB_CODE(method(surface, 0x7c)))(surface,
                                              BB_POINTER(owner + 0xb8));
#endif
  const unsigned char *left = BB_DATA(bb_address_encode(name));
  const unsigned char *right = BB_DATA(0x00486dd8);
  while (*left && *left == *right) {
    ++left;
    ++right;
  }
  if (*left != *right)
    return *left < *right ? UINT32_MAX : 1;
  uint32_t result = BB_U32(owner + 0x20ebe);
  if (!result) {
    bb_palette_remap(object);
    bb_palette_shade(object);
    bb_palette_reflect(object);
    result = bb_palette_mix(object);
    BB_U32(owner + 0x20ebe) = 1;
  }
  return result;
}

/* 0x0042f030..0x0042f07b. */
static uint32_t black(void *object, uint32_t scratch) {
  uint32_t owner = bb_address_encode(object);
  memset(BB_SPAN(scratch, 1024), 0, 1024);
  void *palette = BB_POINTER(owner + 0xb8);
  return palette ? entries(palette, 0x18, 0, 256, scratch) : 0;
}
/* 0x0042f080..0x0042f143: rotate the ten water colors. The extra saved entry
 * after them remains part of the shared object's state.
 */
uint32_t BB_THISCALL bb_palette_cycle(void *object) {
  uint32_t owner = bb_address_encode(object);
  for (uint32_t c = 0; c < 4; ++c)
    put(owner + 0xe4 + c, byte(owner + 0xbc + c));
  for (int32_t i = 9; i >= 0; --i)
    for (uint32_t c = 0; c < 4; ++c)
      put(owner + 0xbc + ((i + 1) % 10) * 4 + c,
          byte(owner + 0xbc + i * 4 + c));
  for (uint32_t c = 0; c < 4; ++c)
    put(owner + 0xc0 + c, byte(owner + 0xe4 + c));
  return entries(BB_POINTER(owner + 0xb8), 0x18, 80, 10, owner + 0xbc);
}

/* 0x00432c40..0x00432c55: EDX retains its high bytes and receives the target.
 */
uint32_t BB_THISCALL bb_palette_ready(void *object) {
  uint32_t owner = bb_address_encode(object);
  uint8_t target = byte(owner + 0x20ebd);
  return byte(owner + 0x20ebc) == target;
}
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_palette_ready_entry(void) {
  __asm__ volatile("movb 0x20ebd(%ecx), %dl\n\t"
                   "xorl %eax, %eax\n\t"
                   "cmpb %dl, 0x20ebc(%ecx)\n\t"
                   "sete %al\n\t"
                   "ret");
}

#endif

#ifdef BB_MODEL_RUNTIME
void bb_palette_load_model(uint32_t *r) {
  uint32_t args = r[3] + BB_PALETTE_LOAD_BYTES + 8u;
  r[7] = bb_address_encode(load((void *)(uintptr_t)BB_U32(args),
                                (const char *)(uintptr_t)BB_U32(args + 4),
                                r[3] + 4u));
}
void *bb_palette_load(void *draw, const char *name) {
  return (void *)(uintptr_t)bb_render_model_service(
      0x00431120, 0,
      (uint32_t[]){bb_address_encode(draw), bb_address_encode(name)}, 2);
}
void bb_palette_install_model(uint32_t *r) {
  r[7] = install(
      (void *)(uintptr_t)r[6],
      (const char *)(uintptr_t)BB_U32(r[3] + BB_PALETTE_COLORS_BYTES + 8u),
      r[3] + 4u);
}
uint32_t BB_THISCALL bb_palette_install(void *object, const char *name) {
  return bb_render_model_service(0x0042ef20, bb_address_encode(object),
                                 (uint32_t[]){bb_address_encode(name)}, 1);
}
void bb_palette_black_model(uint32_t *r) {
  r[7] = black((void *)(uintptr_t)r[6], r[3] + 4u);
}
uint32_t BB_THISCALL bb_palette_black(void *object) {
  return bb_render_model_service(0x0042f030, bb_address_encode(object), NULL,
                                 0);
}
void bb_palette_mix_model(uint32_t *r) {
  r[7] = mix((void *)(uintptr_t)r[6], r[3] + 4u);
}
uint32_t BB_THISCALL bb_palette_mix(void *object) {
  return bb_render_model_service(0x00432030, bb_address_encode(object), NULL,
                                 0);
}
void bb_palette_reflect_model(uint32_t *r) {
  r[7] = reflect((void *)(uintptr_t)r[6], r[3] + 4u);
}
uint32_t BB_THISCALL bb_palette_reflect(void *object) {
  return bb_render_model_service(0x00432240, bb_address_encode(object), NULL,
                                 0);
}
#else
void *bb_palette_load(void *draw, const char *name) {
  uint32_t scratch[271];
  return load(draw, name, bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_palette_install(void *object, const char *name) {
  uint32_t scratch[256];
  return install(object, name, bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_palette_black(void *object) {
  uint32_t scratch[256];
  return black(object, bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_palette_mix(void *object) {
  char filename[256];
  return mix(object, bb_address_encode(filename));
}
uint32_t BB_THISCALL bb_palette_reflect(void *object) {
  char filename[256];
  return reflect(object, bb_address_encode(filename));
}
#endif
