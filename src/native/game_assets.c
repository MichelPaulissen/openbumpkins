/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#ifdef BB_MODEL_RUNTIME
#include "../bridge/address.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Embedded artwork stays in a user-generated asset file, not in the engine.
 * The little-endian format is shared with tools/extract_native_assets.py. */
struct AssetHeader {
  char magic[8];
  U32 width, height, console, count;
};
struct AssetResource {
  U32 type, id, offset, size;
  char name[32];
};
_Static_assert(sizeof(struct AssetHeader) == 24, "native asset header");
_Static_assert(sizeof(struct AssetResource) == 48, "native asset record");
static unsigned char *assets;
#ifdef BB_MODEL_RUNTIME
static uint32_t assets_reference;
#endif
static struct AssetHeader *header;
static struct AssetResource *resources;

void bb_game_load_assets(unsigned char *base) {
  char *path = bb_path("bumpkins-assets.bin");
  FILE *file = fopen(path, "rb");
  free(path);
  if (!file)
    bb_die(
        "cannot open bumpkins-assets.bin; run tools/extract_native_assets.py "
        "--game-dir DIR on your original/patched game directory");
  if (fseek(file, 0, SEEK_END))
    bb_die("cannot size native asset file");
  long length = ftell(file);
  if (length < (long)sizeof(*header) || length > 64 * 1024 * 1024)
    bb_die("invalid native asset file size");
  rewind(file);
  assets = calloc((size_t)length + 1, 1);
  if (!assets || fread(assets, 1, (size_t)length, file) != (size_t)length)
    bb_die("cannot read native asset file");
  fclose(file);
  header = (void *)assets;
  resources = (void *)(assets + sizeof(*header));
  if (memcmp(header->magic, "BBASSET1", 8) || !header->count ||
      header->count > 4096 || header->width < 100 || header->height < 100 ||
      header->console > 1)
    bb_die("invalid native asset header");
  size_t start = sizeof(*header) + header->count * sizeof(*resources);
  if (start > (size_t)length)
    bb_die("truncated native asset index");
  for (unsigned n = 0; n < header->count; ++n) {
    const struct AssetResource *r = &resources[n];
    if (r->type > 0xffff || (r->id > 0xffff && r->id != UINT32_MAX) ||
        !memchr(r->name, 0, sizeof(r->name)) || r->offset < start ||
        r->offset > (size_t)length || r->size > (size_t)length - r->offset)
      bb_die("invalid native resource %u", n);
  }
#ifdef BB_MODEL_RUNTIME
  if (!bb_address_bind_auto(0x20000000u, UINT64_C(0xf0000000), assets,
                            (size_t)length + 1, 16, &assets_reference))
    bb_die("cannot map native asset contents");
#endif
  memcpy(base + 0x84444, &header->width, 4);
  memcpy(base + 0x84448, &header->height, 4);
  memcpy(base + 0x842a3, &header->console, 4);
}

void *bb_game_resource(const char *name, const char *type, U32 *size) {
  if (!header || (uintptr_t)type > 0xffff)
    return NULL;
  for (unsigned n = 0; n < header->count; ++n) {
    const struct AssetResource *r = &resources[n];
    if (r->type != (uintptr_t)type)
      continue;
    if (r->id == UINT32_MAX) {
      if ((uintptr_t)name <= 0xffff || strcasecmp(r->name, name))
        continue;
    } else if (r->id != (uintptr_t)name)
      continue;
    if (size)
      *size = r->size;
    return assets + r->offset;
  }
  return NULL;
}

void bb_game_unload_assets(void) {
#ifdef BB_MODEL_RUNTIME
  if (assets_reference && !bb_address_unbind(assets_reference))
    bb_die("asset contents lost their logical mapping");
  assets_reference = 0;
#endif
  free(assets);
  assets = NULL;
  header = NULL;
  resources = NULL;
}
