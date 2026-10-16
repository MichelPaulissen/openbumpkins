/* SPDX-License-Identifier: GPL-3.0-only */
/* Included after the shared SDL implementation: only ABI/handle translation
 * belongs here. Pixel operations, palettes, GDI and presentation stay shared.
 */
#include <pthread.h>

enum { DRAW_CONTEXT = 4, DRAW_BITMAP = 5, DRAW_PROXY_BYTES = 16 };
struct model_bitmap {
  SDL_Surface *surface;
  uint32_t pixels;
};
static uint32_t draw_references[3];
static uint32_t draw_identities[23], surface_identities[36],
    palette_identities[7];
static pthread_once_t draw_model_once = PTHREAD_ONCE_INIT;

static void model_tables(void) {
  uint32_t *tables[] = {draw_identities, surface_identities,
                        palette_identities};
  const unsigned counts[] = {23, 36, 7};
  const uint32_t bases[] = {BB_DRAW_MODEL_DRAW, BB_DRAW_MODEL_SURFACE,
                            BB_DRAW_MODEL_PALETTE};
  for (unsigned i = 0; i < 3; ++i) {
    for (unsigned n = 0; n < counts[i]; ++n)
      tables[i][n] = bases[i] + n * 4u;
#ifdef BB_PORTABLE_RUNTIME
    if (!bb_address_bind_auto(0xf0000000u, UINT64_C(0x100000000), tables[i],
                              counts[i] * 4u, 16, &draw_references[i]))
      bb_die("cannot map native drawing method table");
#else
    draw_references[i] = bb_address_encode(tables[i]);
#endif
  }
}

/* A proxy is provider-owned, with a four-byte vptr followed by a type tag and
 * a full-width host pointer. Game records retain only its logical reference.
 * Opaque host pointers are never narrowed or used as function identities. */
static uint32_t model_proxy(void *host, uint32_t kind) {
  uint32_t reference = bb_heap_zeroed(1, DRAW_PROXY_BYTES);
  if (!reference)
    return 0;
  pthread_once(&draw_model_once, model_tables);
  BB_U32(reference) = kind <= 3 ? draw_references[kind - 1] : 0;
  BB_U32(reference + 4u) = 0x44520000u | kind;
  BB_U64(reference + 8u) = (uintptr_t)host;
  return reference;
}
static void *model_host(uint32_t reference, uint32_t kind) {
  uint32_t tag = BB_U32(reference + 4u);
  if ((kind && tag != (0x44520000u | kind)) ||
      (!kind && (tag < 0x44520001u || tag > 0x44520003u)))
    bb_die("invalid native drawing object %08x", reference);
  uint64_t host = BB_U64(reference + 8u);
  if (!host || host > UINTPTR_MAX)
    bb_die("invalid native drawing host pointer");
  return (void *)(uintptr_t)host;
}
static uint32_t model_object(struct Com *object) {
  if (!object->reference)
    object->reference = model_proxy(object, object->kind);
  return object->reference;
}
static uint32_t model_context(struct Context *context) {
  if (!context->reference)
    context->reference = model_proxy(context, DRAW_CONTEXT);
  return context->reference;
}
static struct model_bitmap *model_bitmap(uint32_t reference) {
  return model_host(reference, DRAW_BITMAP);
}
static SDL_Surface *model_surface_bitmap(uint32_t reference) {
  return reference <= 0xffffu ? (void *)(uintptr_t)reference
                              : model_bitmap(reference)->surface;
}
static uint32_t model_bitmap_export(SDL_Surface *surface) {
  if ((uintptr_t)surface <= 0xffffu)
    return (uint32_t)(uintptr_t)surface;
  struct model_bitmap *bitmap = calloc(1, sizeof(*bitmap));
  if (!bitmap) {
    SDL_FreeSurface(surface);
    return 0;
  }
  bitmap->surface = surface;
  uint32_t reference = model_proxy(bitmap, DRAW_BITMAP);
  if (!reference) {
    SDL_FreeSurface(surface);
    free(bitmap);
  }
  return reference;
}
static uint32_t model_bitmap_pixels(struct model_bitmap *bitmap) {
  if (!bitmap->pixels) {
#ifdef BB_PORTABLE_RUNTIME
    size_t size = (size_t)bitmap->surface->h * (size_t)bitmap->surface->pitch;
    if (!bb_address_bind_auto(0x20000000u, UINT64_C(0xf0000000),
                              bitmap->surface->pixels, size, 16,
                              &bitmap->pixels))
      bb_die("cannot map native bitmap pixels");
#else
    bitmap->pixels = bb_address_encode(bitmap->surface->pixels);
#endif
  }
  return bitmap->pixels;
}
static uint32_t model_arg(uint32_t *r, unsigned n, unsigned reserve) {
  return BB_U32(r[3] + reserve + 8u + n * 4u);
}
static void *model_span(uint32_t reference, size_t size) {
  if (!reference)
    bb_die("null native drawing data argument");
  return BB_SPAN(reference, size);
}
#define ARG(n) model_arg(r, n, 0)
#define HOST(n, kind) model_host(ARG(n), kind)
#define DATA(n) ((ARG(n)) ? BB_DATA(ARG(n)) : NULL)
static uint32_t model_output(uint32_t slot, struct Com *object) {
  uint32_t reference = model_object(object);
  if (!reference) {
    object_release(object);
    return 0x8007000eu;
  }
  BB_U32(slot) = reference;
  return 0;
}
static void model_query(uint32_t *r) {
  struct Com *output = NULL;
  r[7] = object_query(HOST(0, 0), DATA(1), (void **)&output);
  if (!r[7])
    r[7] = model_output(ARG(2), output);
  else
    BB_U32(ARG(2)) = 0;
}
static void model_addref(uint32_t *r) { r[7] = object_addref(HOST(0, 0)); }
static void model_release(uint32_t *r) { r[7] = object_release(HOST(0, 0)); }
static void model_create(uint32_t *r) {
  struct Com *output = NULL;
  r[7] = DirectDrawCreate(DATA(0), &output, (void *)(uintptr_t)ARG(2));
  if (!r[7])
    r[7] = model_output(ARG(1), output);
}
static void model_create_palette(uint32_t *r) {
  struct Palette *output = NULL;
  r[7] = draw_palette(HOST(0, 1), ARG(1), DATA(2), &output,
                      (void *)(uintptr_t)ARG(4));
  if (!r[7])
    r[7] = model_output(ARG(3), &output->com);
}
static void model_create_surface(uint32_t *r) {
  struct Surface *output = NULL;
  r[7] = draw_surface(HOST(0, 1), DATA(1), &output, (void *)(uintptr_t)ARG(3));
  if (!r[7])
    r[7] = model_output(ARG(2), &output->com);
}
struct model_enumeration {
  uint32_t callback, context, descriptor;
};
static I32 W model_enum_callback(U32 *description, void *context) {
  struct model_enumeration *enumeration = context;
  memcpy(BB_SPAN(enumeration->descriptor, 108), description, 108);
  uint32_t args[] = {enumeration->descriptor, enumeration->context};
  struct bb_call_state state = {0, 0, 0, 0x202};
  bb_model_invoke(&state, enumeration->callback, args, 2);
  return state.eax;
}
static void model_enum(uint32_t *r) {
  uint32_t args[5];
  for (unsigned n = 0; n < 5; ++n)
    args[n] = model_arg(r, n, 128);
  struct model_enumeration enumeration = {args[4], args[3], r[3] + 4u};
  r[7] = draw_enum_modes(model_host(args[0], 1), args[1],
                         args[2] ? BB_DATA(args[2]) : NULL, &enumeration,
                         model_enum_callback);
}
static void model_draw_caps(uint32_t *r) {
  r[7] = draw_caps(HOST(0, 1), DATA(1), DATA(2));
}
static void model_get_mode(uint32_t *r) {
  r[7] = draw_get_mode(HOST(0, 1), model_span(ARG(1), 108));
}
static void model_restore_mode(uint32_t *r) {
  r[7] = draw_restore_mode(HOST(0, 1));
}
static void model_cooperative(uint32_t *r) {
  r[7] = draw_cooperative(HOST(0, 1), (void *)(uintptr_t)ARG(1), ARG(2));
}
static void model_mode(uint32_t *r) {
  r[7] = draw_mode(HOST(0, 1), ARG(1), ARG(2), ARG(3));
}
static void model_vertical_blank(uint32_t *r) {
  r[7] = draw_vertical_blank(HOST(0, 1), ARG(1), (void *)(uintptr_t)ARG(2));
}
static void model_blt(uint32_t *r) {
  r[7] = surface_blt(HOST(0, 2), DATA(1), ARG(2) ? HOST(2, 2) : NULL, DATA(3),
                     ARG(4), DATA(5));
}
static void model_blt_fast(uint32_t *r) {
  r[7] =
      surface_blt_fast(HOST(0, 2), ARG(1), ARG(2), HOST(3, 2), DATA(4), ARG(5));
}
static void model_flip(uint32_t *r) {
  r[7] = surface_flip(HOST(0, 2), ARG(1) ? HOST(1, 2) : NULL, ARG(2));
}
static void model_attached(uint32_t *r) {
  struct Surface *output = NULL;
  r[7] = surface_attached(HOST(0, 2), DATA(1), &output);
  if (!r[7])
    r[7] = model_output(ARG(2), &output->com);
}
static void model_surface_caps(uint32_t *r) {
  r[7] = surface_caps(HOST(0, 2), DATA(1));
}
static void model_get_key(uint32_t *r) {
  r[7] = surface_get_key(HOST(0, 2), ARG(1), DATA(2));
}
static void model_get_dc(uint32_t *r) {
  struct Context *output = NULL;
  r[7] = surface_get_dc(HOST(0, 2), &output);
  if (!r[7]) {
    uint32_t reference = model_context(output);
    if (reference)
      BB_U32(ARG(1)) = reference;
    else {
      surface_release_dc(HOST(0, 2), output);
      r[7] = 0x8007000eu;
    }
  }
}
static void model_get_palette(uint32_t *r) {
  struct Palette *output = NULL;
  r[7] = surface_get_palette(HOST(0, 2), &output);
  if (!r[7])
    r[7] = model_output(ARG(1), &output->com);
}
static void model_get_format(uint32_t *r) {
  r[7] = surface_get_format(HOST(0, 2), DATA(1));
}
static void model_describe(uint32_t *r) {
  r[7] = surface_description(HOST(0, 2), model_span(ARG(1), 108));
}
static void model_lost(uint32_t *r) { r[7] = surface_lost(HOST(0, 2)); }
static void model_lock(uint32_t *r) {
  r[7] = surface_lock(HOST(0, 2), DATA(1), model_span(ARG(2), 108), ARG(3),
                      (void *)(uintptr_t)ARG(4));
}
static void model_release_dc(uint32_t *r) {
  r[7] = surface_release_dc(HOST(0, 2), HOST(1, DRAW_CONTEXT));
}
static void model_restore(uint32_t *r) { r[7] = surface_restore(HOST(0, 2)); }
static void model_set_key(uint32_t *r) {
  r[7] = surface_set_key(HOST(0, 2), ARG(1), DATA(2));
}
static void model_set_palette(uint32_t *r) {
  r[7] = surface_set_palette(HOST(0, 2), ARG(1) ? HOST(1, 3) : NULL);
}
static void model_unlock(uint32_t *r) {
  r[7] = surface_unlock(HOST(0, 2), (void *)(uintptr_t)ARG(1));
}
static void model_palette_caps(uint32_t *r) {
  r[7] = palette_caps(HOST(0, 3), DATA(1));
}
static void model_palette_get(uint32_t *r) {
  r[7] = palette_get(HOST(0, 3), ARG(1), ARG(2), ARG(3), DATA(4));
}
static void model_palette_set(uint32_t *r) {
  r[7] = palette_set(HOST(0, 3), ARG(1), ARG(2), ARG(3), DATA(4));
}
static void model_stock(uint32_t *r) {
  r[7] = (uintptr_t)GetStockObject(ARG(0));
}
static void model_create_dc(uint32_t *r) {
  struct Context *context = CreateCompatibleDC((void *)(uintptr_t)ARG(0));
  r[7] = context ? model_context(context) : 0;
  if (context && !r[7])
    DeleteDC(context);
}
static void model_delete_dc(uint32_t *r) {
  r[7] = DeleteDC(HOST(0, DRAW_CONTEXT));
}
static void model_select(uint32_t *r) {
  struct Context *context = HOST(0, DRAW_CONTEXT);
  uint32_t previous = context->bitmap_reference;
  SelectObject(context, model_surface_bitmap(ARG(1)));
  context->bitmap_reference = ARG(1);
  r[7] = previous;
}
static void model_delete_bitmap(uint32_t *r) {
  uint32_t reference = ARG(0);
  if (reference <= 0xffffu) {
    r[7] = DeleteObject((void *)(uintptr_t)reference);
    return;
  }
  struct model_bitmap *bitmap = model_bitmap(reference);
#ifdef BB_PORTABLE_RUNTIME
  if (bitmap->pixels && !bb_address_unbind(bitmap->pixels))
    bb_die("invalid native bitmap pixel mapping");
#endif
  r[7] = DeleteObject(bitmap->surface);
  free(bitmap);
  if (!bb_heap_release(reference))
    bb_die("invalid native bitmap reference");
}
static void model_get_object(uint32_t *r) {
  struct model_bitmap *bitmap = ARG(0) ? model_bitmap(ARG(0)) : NULL;
  r[7] = GetObjectA(bitmap ? bitmap->surface : NULL, ARG(1), DATA(2));
  if (r[7])
    BB_U32(ARG(2) + 20u) = model_bitmap_pixels(bitmap);
}
static void model_load(uint32_t *r) {
  const char *name = !(ARG(5) & 0x10u) && ARG(1) <= 0xffffu
                         ? (void *)(uintptr_t)ARG(1)
                         : DATA(1);
  r[7] = model_bitmap_export(LoadImageA((void *)(uintptr_t)ARG(0), name, ARG(2),
                                        ARG(3), ARG(4), ARG(5)));
}
static void model_get_pixel(uint32_t *r) {
  r[7] = GetPixel(HOST(0, DRAW_CONTEXT), ARG(1), ARG(2));
}
static void model_set_pixel(uint32_t *r) {
  r[7] = SetPixel(HOST(0, DRAW_CONTEXT), ARG(1), ARG(2), ARG(3));
}
static void model_stretch(uint32_t *r) {
  r[7] = StretchBlt(HOST(0, DRAW_CONTEXT), ARG(1), ARG(2), ARG(3), ARG(4),
                    HOST(5, DRAW_CONTEXT), ARG(6), ARG(7), ARG(8), ARG(9),
                    ARG(10));
}
#undef DATA
#undef HOST
#undef ARG

static const struct bb_model_target drawing_targets[] = {
#define TARGET(base, slot, name, words, reserve)                               \
  {base + slot * 4u, model_##name, words, reserve, 0}
#define COMMON(base)                                                           \
  TARGET(base, 0, query, 3, 0), TARGET(base, 1, addref, 1, 0),                 \
      TARGET(base, 2, release, 1, 0)
    COMMON(BB_DRAW_MODEL_DRAW),
    TARGET(BB_DRAW_MODEL_DRAW, 5, create_palette, 5, 0),
    TARGET(BB_DRAW_MODEL_DRAW, 6, create_surface, 4, 0),
    TARGET(BB_DRAW_MODEL_DRAW, 8, enum, 5, 128),
    TARGET(BB_DRAW_MODEL_DRAW, 11, draw_caps, 3, 0),
    TARGET(BB_DRAW_MODEL_DRAW, 12, get_mode, 2, 0),
    TARGET(BB_DRAW_MODEL_DRAW, 19, restore_mode, 1, 0),
    TARGET(BB_DRAW_MODEL_DRAW, 20, cooperative, 3, 0),
    TARGET(BB_DRAW_MODEL_DRAW, 21, mode, 4, 0),
    TARGET(BB_DRAW_MODEL_DRAW, 22, vertical_blank, 3, 0),
    COMMON(BB_DRAW_MODEL_SURFACE),
    TARGET(BB_DRAW_MODEL_SURFACE, 5, blt, 6, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 7, blt_fast, 6, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 11, flip, 3, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 12, attached, 3, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 14, surface_caps, 2, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 16, get_key, 3, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 17, get_dc, 2, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 20, get_palette, 2, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 21, get_format, 2, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 22, describe, 2, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 24, lost, 1, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 25, lock, 5, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 26, release_dc, 2, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 27, restore, 1, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 29, set_key, 3, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 31, set_palette, 2, 0),
    TARGET(BB_DRAW_MODEL_SURFACE, 32, unlock, 2, 0),
    COMMON(BB_DRAW_MODEL_PALETTE),
    TARGET(BB_DRAW_MODEL_PALETTE, 3, palette_caps, 2, 0),
    TARGET(BB_DRAW_MODEL_PALETTE, 4, palette_get, 5, 0),
    TARGET(BB_DRAW_MODEL_PALETTE, 6, palette_set, 5, 0),
    TARGET(BB_DRAW_MODEL_API, 0, create, 3, 0),
    TARGET(BB_DRAW_MODEL_API, 1, stock, 1, 0),
    TARGET(BB_DRAW_MODEL_API, 2, create_dc, 1, 0),
    TARGET(BB_DRAW_MODEL_API, 3, delete_dc, 1, 0),
    TARGET(BB_DRAW_MODEL_API, 4, select, 2, 0),
    TARGET(BB_DRAW_MODEL_API, 5, delete_bitmap, 1, 0),
    TARGET(BB_DRAW_MODEL_API, 6, get_object, 3, 0),
    TARGET(BB_DRAW_MODEL_API, 7, load, 6, 0),
    TARGET(BB_DRAW_MODEL_API, 8, get_pixel, 3, 0),
    TARGET(BB_DRAW_MODEL_API, 9, set_pixel, 4, 0),
    TARGET(BB_DRAW_MODEL_API, 10, stretch, 11, 0)
#undef COMMON
#undef TARGET
};
uint32_t bb_draw_model_export(const char *name) {
  static const char *const names[] = {
      "DirectDrawCreate", "GetStockObject", "CreateCompatibleDC", "DeleteDC",
      "SelectObject",     "DeleteObject",   "GetObjectA",         "LoadImageA",
      "GetPixel",         "SetPixel",       "StretchBlt"};
  for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i)
    if (!strcmp(name, names[i]))
      return BB_DRAW_MODEL_API + i * 4u;
  return 0;
}
const struct bb_model_target *bb_draw_model_find(void *unused,
                                                 uint32_t identity) {
  (void)unused;
  for (unsigned i = 0; i < sizeof(drawing_targets) / sizeof(*drawing_targets);
       ++i)
    if (drawing_targets[i].identity == identity)
      return drawing_targets + i;
  return NULL;
}
