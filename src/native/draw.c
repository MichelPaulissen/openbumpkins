/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#ifdef BB_MODEL_RUNTIME
#include "draw_models.h"
#include "../bridge/address.h"
#include "../bridge/heap.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Com {
  void **table;
  U32 references, kind;
#ifdef BB_MODEL_RUNTIME
  U32 reference;
#endif
};
struct Palette {
  struct Com com;
  unsigned char entries[1024];
};
struct Surface {
  struct Com com;
  int width, height, bits, pitch;
  U32 caps, color_low, color_high;
  unsigned char *pixels;
#ifdef BB_MODEL_RUNTIME
  U32 pixels_reference;
#endif
  struct Palette *palette;
  struct Surface *back;
};
struct Context {
  SDL_Surface *bitmap;
  struct Surface *surface;
#ifdef BB_MODEL_RUNTIME
  U32 reference, bitmap_reference;
#endif
};
static void *draw_table[23], *surface_table[36], *palette_table[7];
static int tables_ready, screen_width = 640, screen_height = 480,
                         screen_bits = 8;
static struct Palette *display_palette;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static int texture_width, texture_height;
unsigned bb_present_count;
/* The renderer and texture borrow the current SDL window. Release them before
 * that window is destroyed so a later window can create a fresh presenter. */
void bb_draw_reset_presentation(void) {
  if (texture)
    SDL_DestroyTexture(texture);
  if (renderer)
    SDL_DestroyRenderer(renderer);
  texture = NULL;
  renderer = NULL;
  texture_width = texture_height = 0;
}
static U32 W object_addref(struct Com *object) { return ++object->references; }
static U32 W object_release(struct Com *object) {
  if (--object->references)
    return object->references;
  if (object->kind == 2) {
    struct Surface *surface = (struct Surface *)object;
#ifdef BB_MODEL_RUNTIME
    if (!bb_heap_release(surface->pixels_reference))
      bb_die("invalid native surface pixel allocation");
#else
    free(surface->pixels);
#endif
    if (surface->back)
      object_release(&surface->back->com);
    if (surface->palette)
      object_release(&surface->palette->com);
  }
  if (object->kind == 3 && display_palette == (struct Palette *)object)
    display_palette = NULL;
#ifdef BB_MODEL_RUNTIME
  if (!bb_heap_release(object->reference))
    bb_die("invalid native drawing object reference");
#endif
  free(object);
  return 0;
}
static I32 W object_query(struct Com *object, const U32 *guid, void **output) {
  if (bb_trace)
    fprintf(stderr, "COM QueryInterface %08x\n", guid[0]);
  if (guid[0] != 0 && guid[0] != 0x6c14db80 && guid[0] != 0x6c14db81 &&
      guid[0] != 0x6c14db84) {
    *output = NULL;
    return (I32)0x80004002;
  }
  *output = object;
  object_addref(object);
  return 0;
}
static void pixel_format(U32 *format, int bits) {
  memset(format, 0, 32);
  format[0] = 32;
  format[1] = bits == 8 ? 0x60 : 0x40;
  format[3] = (U32)bits;
  if (bits == 16) {
    format[4] = 0xf800;
    format[5] = 0x7e0;
    format[6] = 0x1f;
  }
  if (bits == 32) {
    format[4] = 0xff0000;
    format[5] = 0xff00;
    format[6] = 0xff;
  }
}
static I32 W surface_description(struct Surface *surface, U32 *description) {
  memset(description, 0, 108);
  description[0] = 108;
  description[1] = 0x100f;
  description[2] = (U32)surface->height;
  description[3] = (U32)surface->width;
  description[4] = (U32)surface->pitch;
#ifdef BB_MODEL_RUNTIME
  description[9] = surface->pixels_reference;
#else
  description[9] = (U32)(uintptr_t)surface->pixels;
#endif
  pixel_format(description + 18, surface->bits);
  description[26] = surface->caps;
  return 0;
}
static struct Surface *surface_new(int width, int height, int bits, U32 caps) {
  if (width < 1 || height < 1 || width > 16384 || height > 16384)
    bb_die("invalid surface %dx%d", width, height);
  struct Surface *surface = calloc(1, sizeof(*surface));
  surface->com.table = surface_table;
  surface->com.references = 1;
  surface->com.kind = 2;
  surface->width = width;
  surface->height = height;
  surface->bits = bits;
  surface->pitch = (width * (bits / 8) + 3) & ~3;
  surface->caps = caps;
#ifdef BB_MODEL_RUNTIME
  surface->pixels_reference =
      bb_heap_zeroed((size_t)height, (size_t)surface->pitch);
  surface->pixels = surface->pixels_reference
                        ? BB_DATA(surface->pixels_reference) : NULL;
#else
  surface->pixels = calloc((size_t)surface->height, (size_t)surface->pitch);
#endif
  if (!surface->pixels)
    bb_die("surface allocation failed");
  return surface;
}
static I32 W surface_lock(struct Surface *surface, const Rect *rect,
                          U32 *description, U32 flags, Handle event) {
  surface_description(surface, description);
  if (rect)
    description[9] +=
        (U32)(rect->top * surface->pitch + rect->left * (surface->bits / 8));
  return 0;
}
static I32 W surface_unlock(struct Surface *surface, void *pixels) {
  /* Primary-surface writes are visible without a subsequent Flip or Blt.
   * The movie decoder updates this surface through Lock/Unlock directly.
   * https://learn.microsoft.com/en-us/windows/win32/api/ddraw/ns-ddraw-ddscaps
   */
  if (surface->caps & 0x200) {
    struct Palette *palette =
        surface->palette ? surface->palette : display_palette;
    bb_display_pixels(surface->pixels, surface->width, surface->height,
                      surface->pitch, surface->bits,
                      palette ? palette->entries : NULL);
  }
  return 0;
}
void bb_display_pixels(const unsigned char *pixels, int width, int height,
                       int pitch, int bits, const unsigned char *palette) {
  /* Moving the software cursor restores its previous rectangle with Blt and
   * draws its replacement through Lock/Unlock. Both paths must defer output
   * to the following game frame. An extra full-window presentation here can
   * consume another refresh interval for every input-bearing frame. */
  if (!bb_window || bb_dispatching_mouse_motion)
    return;
  if (!renderer) {
    renderer = SDL_CreateRenderer(bb_window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer)
      bb_die("SDL renderer: %s", SDL_GetError());
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  }
  if (!texture || texture_width != width || texture_height != height) {
    if (texture)
      SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture)
      bb_die("SDL texture: %s", SDL_GetError());
    texture_width = width;
    texture_height = height;
    if (SDL_RenderSetLogicalSize(renderer, width, height))
      bb_die("SDL logical resolution: %s", SDL_GetError());
  }
  U32 *rgba = malloc((size_t)width * (size_t)height * 4);
  if (!rgba)
    bb_die("frame allocation failed");
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++) {
      const unsigned char *p = pixels + y * pitch + x * (bits / 8);
      U32 color = 0;
      if (bits == 8) {
        const unsigned char *entry = palette ? palette + *p * 4 : NULL;
        color = entry ? ((U32)entry[0] << 16) | ((U32)entry[1] << 8) | entry[2]
                      : (U32)*p * 0x10101;
      } else if (bits == 16) {
        U16 value;
        memcpy(&value, p, 2);
        color = (((U32)value >> 11) * 255 / 31 << 16) |
                ((((U32)value >> 5) & 63) * 255 / 63 << 8) |
                ((U32)(value & 31) * 255 / 31);
      } else if (bits == 32)
        memcpy(&color, p, 4);
      else
        bb_die("unsupported pixel depth %d", bits);
      rgba[y * width + x] = 0xff000000 | color;
    }
  SDL_UpdateTexture(texture, NULL, rgba, width * 4);
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
  bb_present_count++;
  if (bb_stop_after_frames && bb_present_count >= bb_stop_after_frames) {
    if (bb_capture_path) {
      SDL_Surface *capture = SDL_CreateRGBSurfaceWithFormatFrom(
          rgba, width, height, 32, width * 4, SDL_PIXELFORMAT_ARGB8888);
      if (SDL_SaveBMP(capture, bb_capture_path))
        bb_die("capture failed: %s", SDL_GetError());
      SDL_FreeSurface(capture);
    }
    fprintf(stderr, "Native checkpoint: %u frames presented\n",
            bb_present_count);
    free(rgba);
    exit(0);
  }
  free(rgba);
}
static I32 W surface_flip(struct Surface *surface, struct Surface *target,
                          U32 flags) {
  struct Surface *back = target ? target : surface->back;
  if (back) {
    unsigned char *swap = surface->pixels;
    surface->pixels = back->pixels;
    back->pixels = swap;
#ifdef BB_MODEL_RUNTIME
    U32 reference = surface->pixels_reference;
    surface->pixels_reference = back->pixels_reference;
    back->pixels_reference = reference;
#endif
  }
  struct Palette *palette =
      surface->palette ? surface->palette : display_palette;
  bb_display_pixels(surface->pixels, surface->width, surface->height,
                    surface->pitch, surface->bits,
                    palette ? palette->entries : NULL);
  return 0;
}
static I32 W surface_attached(struct Surface *surface, const U32 *caps,
                              struct Surface **output) {
  if (!surface->back)
    return (I32)0x8876010a;
  *output = surface->back;
  object_addref(&surface->back->com);
  return 0;
}
static I32 W surface_lost(struct Surface *surface) { return 0; }
static I32 W surface_restore(struct Surface *surface) { return 0; }
static I32 W surface_set_palette(struct Surface *surface,
                                 struct Palette *palette) {
  if (surface->palette)
    object_release(&surface->palette->com);
  surface->palette = palette;
  if (palette)
    object_addref(&palette->com);
  display_palette = palette;
  if (surface->back)
    surface_set_palette(surface->back, palette);
  return 0;
}
static I32 W surface_get_palette(struct Surface *surface,
                                 struct Palette **output) {
  if (!surface->palette)
    return (I32)0x88760234;
  *output = surface->palette;
  object_addref(&surface->palette->com);
  return 0;
}
static I32 W surface_get_format(struct Surface *surface, U32 *format) {
  pixel_format(format, surface->bits);
  return 0;
}
static I32 W surface_caps(struct Surface *surface, U32 *caps) {
  *caps = surface->caps;
  return 0;
}
static I32 W surface_set_key(struct Surface *surface, U32 flags,
                             const U32 *key) {
  surface->color_low = key ? key[0] : 0;
  surface->color_high = key ? key[1] : 0;
  return 0;
}
static I32 W surface_get_key(struct Surface *surface, U32 flags, U32 *key) {
  key[0] = surface->color_low;
  key[1] = surface->color_high;
  return 0;
}
static I32 W surface_blt(struct Surface *destination,
                         const Rect *destination_rect, struct Surface *source,
                         const Rect *source_rect, U32 flags,
                         const U32 *effects) {
  Rect d = destination_rect
               ? *destination_rect
               : (Rect){0, 0, destination->width, destination->height};
  Rect s = source_rect ? *source_rect
           : source    ? (Rect){0, 0, source->width, source->height}
                       : (Rect){0, 0, 0, 0};
  if (d.right <= d.left || d.bottom <= d.top)
    return 0;
  int bytes = destination->bits / 8;
  if (source && source->bits != destination->bits)
    bb_die("mixed-depth DirectDraw blit %d -> %d", source->bits,
           destination->bits);
  for (int y = d.top; y < d.bottom; y++)
    for (int x = d.left; x < d.right; x++) {
      if (x < 0 || y < 0 || x >= destination->width || y >= destination->height)
        continue;
      U32 pixel = 0;
      if (flags & 0x400)
        pixel = effects ? effects[20] : 0;
      else if (source) {
        int sx =
                s.left + (x - d.left) * (s.right - s.left) / (d.right - d.left),
            sy = s.top + (y - d.top) * (s.bottom - s.top) / (d.bottom - d.top);
        if (sx < 0 || sy < 0 || sx >= source->width || sy >= source->height)
          continue;
        memcpy(&pixel, source->pixels + sy * source->pitch + sx * bytes,
               (size_t)bytes);
        if ((flags & 0x8000) && pixel >= source->color_low &&
            pixel <= source->color_high)
          continue;
      } else
        bb_die("unsupported DirectDraw blit flags %08x", flags);
      memcpy(destination->pixels + y * destination->pitch + x * bytes, &pixel,
             (size_t)bytes);
    }
  if (destination->caps & 0x200) {
    struct Palette *p =
        destination->palette ? destination->palette : display_palette;
    bb_display_pixels(destination->pixels, destination->width,
                      destination->height, destination->pitch,
                      destination->bits, p ? p->entries : NULL);
  }
  return 0;
}
static I32 W surface_blt_fast(struct Surface *destination, U32 x, U32 y,
                              struct Surface *source, const Rect *source_rect,
                              U32 flags) {
  Rect s =
      source_rect ? *source_rect : (Rect){0, 0, source->width, source->height};
  Rect d = {(I32)x, (I32)y, (I32)x + s.right - s.left,
            (I32)y + s.bottom - s.top};
  return surface_blt(destination, &d, source, &s, (flags & 1) ? 0x8000 : 0,
                     NULL);
}
static I32 W surface_get_dc(struct Surface *surface, struct Context **output) {
  struct Context *context = calloc(1, sizeof(*context));
  context->surface = surface;
  *output = context;
  return 0;
}
static I32 W surface_release_dc(struct Surface *surface,
                                struct Context *context) {
#ifdef BB_MODEL_RUNTIME
  if (!bb_heap_release(context->reference))
    bb_die("invalid native drawing context reference");
#endif
  free(context);
  return 0;
}
static I32 W palette_set(struct Palette *palette, U32 flags, U32 start,
                         U32 count, const void *entries) {
  if (start > 256 || count > 256 - start)
    return (I32)0x80070057;
  memcpy(palette->entries + start * 4, entries, count * 4);
  return 0;
}
static I32 W palette_get(struct Palette *palette, U32 flags, U32 start,
                         U32 count, void *entries) {
  if (start > 256 || count > 256 - start)
    return (I32)0x80070057;
  memcpy(entries, palette->entries + start * 4, count * 4);
  return 0;
}
static I32 W palette_caps(struct Palette *palette, U32 *caps) {
  *caps = 0x44;
  return 0;
}
static I32 W draw_palette(struct Com *draw, U32 flags, const void *entries,
                          struct Palette **output, Handle outer) {
  struct Palette *palette = calloc(1, sizeof(*palette));
  palette->com.table = palette_table;
  palette->com.references = 1;
  palette->com.kind = 3;
  if (entries)
    memcpy(palette->entries, entries, 1024);
  *output = palette;
  return 0;
}
static I32 W draw_surface(struct Com *draw, const U32 *description,
                          struct Surface **output, Handle outer) {
  U32 flags = description[1], caps = description[26];
  int width = (flags & 4) ? (int)description[3] : screen_width;
  int height = (flags & 2) ? (int)description[2] : screen_height;
  int bits = (flags & 0x1000) ? (int)description[21] : screen_bits;
  struct Surface *surface = surface_new(width, height, bits, caps);
  if ((flags & 0x20) && description[5])
    surface->back = surface_new(width, height, bits, 4 | 0x10);
  *output = surface;
  if (bb_trace)
    fprintf(stderr, "Surface %dx%d/%d caps=%08x\n", width, height, bits, caps);
  return 0;
}
static I32 W draw_cooperative(struct Com *draw, Handle window, U32 flags) {
  return 0;
}
static I32 W draw_mode(struct Com *draw, U32 width, U32 height, U32 bits) {
  screen_width = (int)width;
  screen_height = (int)height;
  screen_bits = (int)bits;
  fprintf(stderr, "Native display mode: %ux%u/%u\n", width, height, bits);
  return 0;
}
static I32 W draw_restore_mode(struct Com *draw) { return 0; }
static I32 W draw_vertical_blank(struct Com *draw, U32 flags, Handle event) {
  static U32 next_frame;
  if (!bb_fast) {
    U32 now = SDL_GetTicks();
    if ((I32)(next_frame - now) > 0)
      SDL_Delay(next_frame - now);
    next_frame = SDL_GetTicks() + 16;
  }
  return 0;
}
static I32 W draw_get_mode(struct Com *draw, U32 *description) {
  struct Surface temporary = {0};
  temporary.width = screen_width;
  temporary.height = screen_height;
  temporary.bits = screen_bits;
  temporary.pitch = screen_width * (screen_bits / 8);
  return surface_description(&temporary, description);
}
static I32 W draw_enum_modes(struct Com *draw, U32 flags, const U32 *filter,
                             void *context, I32(W *callback)(U32 *, void *)) {
  const int depths[] = {8, 16, 32};
  for (unsigned n = 0; n < sizeof(depths) / sizeof(depths[0]); n++) {
    if (filter && (((filter[1] & 4) && filter[3] != 640) ||
                   ((filter[1] & 2) && filter[2] != 480) ||
                   ((filter[1] & 0x1000) && filter[21] != (U32)depths[n])))
      continue;
    struct Surface mode = {0};
    mode.width = 640;
    mode.height = 480;
    mode.bits = depths[n];
    mode.pitch = 640 * (depths[n] / 8);
    U32 description[27];
    surface_description(&mode, description);
    description[1] = 0x4000 | 0x1000 | 0x8 | 0x4 | 0x2;
    description[6] = 60;
    if (!callback(description, context))
      break;
  }
  return 0;
}
static I32 W draw_caps(struct Com *draw, U32 *driver, U32 *software) {
  U32 *outputs[] = {driver, software};
  for (unsigned n = 0; n < 2; n++)
    if (outputs[n]) {
      U32 size = outputs[n][0];
      memset(outputs[n], 0, size);
      outputs[n][0] = size;
      if (size > 68) {
        outputs[n][1] = 0x04000340;
        outputs[n][6] = 0x44;
        outputs[n][15] = 64 * 1024 * 1024;
        outputs[n][16] = 64 * 1024 * 1024;
      }
    }
  return 0;
}
static void initialize_tables(void) {
  if (tables_ready)
    return;
  tables_ready = 1;
#ifndef BB_MODEL_RUNTIME
  char method[32];
  for (unsigned n = 0; n < 23; n++) {
    snprintf(method, sizeof(method), "method_%u", n);
    draw_table[n] = bb_api("IDirectDraw", method);
  }
  for (unsigned n = 0; n < 36; n++) {
    snprintf(method, sizeof(method), "method_%u", n);
    surface_table[n] = bb_api("IDirectDrawSurface", method);
  }
  for (unsigned n = 0; n < 7; n++) {
    snprintf(method, sizeof(method), "method_%u", n);
    palette_table[n] = bb_api("IDirectDrawPalette", method);
  }
  void **tables[] = {draw_table, surface_table, palette_table};
  for (unsigned n = 0; n < 3; n++) {
    tables[n][0] = object_query;
    tables[n][1] = object_addref;
    tables[n][2] = object_release;
  }
  draw_table[5] = draw_palette;
  draw_table[6] = draw_surface;
  draw_table[8] = draw_enum_modes;
  draw_table[11] = draw_caps;
  draw_table[12] = draw_get_mode;
  draw_table[19] = draw_restore_mode;
  draw_table[20] = draw_cooperative;
  draw_table[21] = draw_mode;
  draw_table[22] = draw_vertical_blank;
  surface_table[5] = surface_blt;
  surface_table[7] = surface_blt_fast;
  surface_table[11] = surface_flip;
  surface_table[12] = surface_attached;
  surface_table[14] = surface_caps;
  surface_table[16] = surface_get_key;
  surface_table[17] = surface_get_dc;
  surface_table[20] = surface_get_palette;
  surface_table[21] = surface_get_format;
  surface_table[22] = surface_description;
  surface_table[24] = surface_lost;
  surface_table[25] = surface_lock;
  surface_table[26] = surface_release_dc;
  surface_table[27] = surface_restore;
  surface_table[29] = surface_set_key;
  surface_table[31] = surface_set_palette;
  surface_table[32] = surface_unlock;
  palette_table[3] = palette_caps;
  palette_table[4] = palette_get;
  palette_table[6] = palette_set;
#endif
}
static I32 W DirectDrawCreate(const void *guid, struct Com **output,
                              Handle outer) {
  initialize_tables();
  struct Com *draw = calloc(1, sizeof(*draw));
  draw->table = draw_table;
  draw->references = 1;
  draw->kind = 1;
  *output = draw;
  return 0;
}
static Handle W GetStockObject(int object) { return (void *)1; }
static struct Context *W CreateCompatibleDC(Handle existing) {
  return calloc(1, sizeof(struct Context));
}
static int W DeleteDC(struct Context *context) {
#ifdef BB_MODEL_RUNTIME
  if (!bb_heap_release(context->reference))
    bb_die("invalid native drawing context reference");
#endif
  free(context);
  return 1;
}
static SDL_Surface *W SelectObject(struct Context *context,
                                   SDL_Surface *bitmap) {
  SDL_Surface *old = context->bitmap;
  context->bitmap = bitmap;
  return old;
}
static int W DeleteObject(SDL_Surface *bitmap) {
  if ((uintptr_t)bitmap > 0xffff)
    SDL_FreeSurface(bitmap);
  return 1;
}
static int W GetObjectA(SDL_Surface *bitmap, int size, void *output) {
  if (!bitmap || size < 24)
    return 0;
  memset(output, 0, 24);
  U32 *fields = output;
  fields[1] = (U32)bitmap->w;
  fields[2] = (U32)bitmap->h;
  fields[3] = (U32)bitmap->pitch;
  U16 *words = output;
  words[8] = 1;
  words[9] = bitmap->format->BitsPerPixel;
#ifdef BB_MODEL_RUNTIME
  /* The logical adapter supplies the borrowed pixel mapping. */
  fields[5] = 0;
#else
  fields[5] = (U32)(uintptr_t)bitmap->pixels;
#endif
  return 24;
}
static SDL_Surface *W LoadImageA(Handle instance, const char *name, U32 type,
                                 int width, int height, U32 flags) {
  if (type != 0)
    return (SDL_Surface *)1;
  if (flags & 0x10) {
    char *path = bb_path(name);
    SDL_Surface *result = SDL_LoadBMP(path);
    free(path);
    return result;
  }
  U32 size = 0;
  unsigned char *dib = bb_pe_resource(instance, name, (const char *)2, &size);
  if (!dib)
    return NULL;
  unsigned char *data = calloc(1, (size_t)size + 14);
  data[0] = 'B';
  data[1] = 'M';
  U32 total = size + 14;
  memcpy(data + 2, &total, 4);
  U32 header;
  memcpy(&header, dib, 4);
  U16 bits;
  memcpy(&bits, dib + 14, 2);
  U32 colors = 0;
  if (bits <= 8) {
    memcpy(&colors, dib + 32, 4);
    if (!colors)
      colors = 1U << bits;
  }
  U32 offset = 14 + header + colors * 4;
  memcpy(data + 10, &offset, 4);
  memcpy(data + 14, dib, size);
  SDL_Surface *result = SDL_LoadBMP_RW(SDL_RWFromConstMem(data, (int)total), 1);
  free(data);
  return result;
}
static SDL_Surface *context_bitmap(struct Context *context) {
  if (context->bitmap)
    return context->bitmap;
  if (!context->surface)
    return NULL;
  struct Surface *surface = context->surface;
  SDL_Surface *bitmap =
      SDL_CreateRGBSurfaceFrom(surface->pixels, surface->width, surface->height,
                               surface->bits, surface->pitch,
                               surface->bits == 16   ? 0xf800
                               : surface->bits == 32 ? 0xff0000
                                                     : 0,
                               surface->bits == 16   ? 0x7e0
                               : surface->bits == 32 ? 0xff00
                                                     : 0,
                               surface->bits == 16   ? 0x1f
                               : surface->bits == 32 ? 0xff
                                                     : 0,
                               0);
  struct Palette *palette =
      surface->palette ? surface->palette : display_palette;
  if (surface->bits == 8 && palette) {
    SDL_Color colors[256];
    for (unsigned n = 0; n < 256; n++)
      colors[n] =
          (SDL_Color){palette->entries[n * 4], palette->entries[n * 4 + 1],
                      palette->entries[n * 4 + 2], 255};
    SDL_SetPaletteColors(bitmap->format->palette, colors, 0, 256);
  }
  return bitmap;
}
static void context_bitmap_release(struct Context *context, SDL_Surface *bitmap) {
  if (bitmap && !context->bitmap)
    SDL_FreeSurface(bitmap);
}
static U32 W GetPixel(struct Context *context, int x, int y) {
  SDL_Surface *bitmap = context_bitmap(context);
  if (!bitmap || x < 0 || y < 0 || x >= bitmap->w || y >= bitmap->h) {
    context_bitmap_release(context, bitmap);
    return UINT32_MAX;
  }
  U32 pixel = 0;
  memcpy(&pixel,
         (char *)bitmap->pixels + y * bitmap->pitch +
             x * bitmap->format->BytesPerPixel,
         bitmap->format->BytesPerPixel);
  unsigned char r, g, b;
  SDL_GetRGB(pixel, bitmap->format, &r, &g, &b);
  context_bitmap_release(context, bitmap);
  return (U32)r | ((U32)g << 8) | ((U32)b << 16);
}
static U32 W SetPixel(struct Context *context, int x, int y, U32 color) {
  SDL_Surface *bitmap = context_bitmap(context);
  if (!bitmap || x < 0 || y < 0 || x >= bitmap->w || y >= bitmap->h) {
    context_bitmap_release(context, bitmap);
    return UINT32_MAX;
  }
  U32 pixel =
      SDL_MapRGB(bitmap->format, (unsigned char)color,
                 (unsigned char)(color >> 8), (unsigned char)(color >> 16));
  memcpy((char *)bitmap->pixels + y * bitmap->pitch +
             x * bitmap->format->BytesPerPixel,
         &pixel, bitmap->format->BytesPerPixel);
  context_bitmap_release(context, bitmap);
  return color;
}
static int W StretchBlt(struct Context *destination, int dx, int dy, int dw,
                        int dh, struct Context *source, int sx, int sy, int sw,
                        int sh, U32 operation) {
  SDL_Surface *d = context_bitmap(destination), *s = context_bitmap(source);
  if (!d || !s) {
    context_bitmap_release(destination, d);
    context_bitmap_release(source, s);
    return 0;
  }
  SDL_Rect dr = {dx, dy, dw, dh}, sr = {sx, sy, sw, sh};
  int result = !SDL_BlitScaled(s, &sr, d, &dr);
  if (!result)
    fprintf(stderr, "Native GDI blit failed: %s\n", SDL_GetError());
  context_bitmap_release(destination, d);
  context_bitmap_release(source, s);
  return result;
}
#ifndef BB_MODEL_RUNTIME
void *bb_draw_api(const char *name) {
  static const struct ApiEntry table[] = {
      API_ENTRY(DirectDrawCreate),   API_ENTRY(GetStockObject),
      API_ENTRY(CreateCompatibleDC), API_ENTRY(DeleteDC),
      API_ENTRY(SelectObject),       API_ENTRY(DeleteObject),
      API_ENTRY(GetObjectA),         API_ENTRY(LoadImageA),
      API_ENTRY(GetPixel),           API_ENTRY(SetPixel),
      API_ENTRY(StretchBlt)};
  API_RETURN(table);
}
#else
#include "draw_models.inc"
#endif
