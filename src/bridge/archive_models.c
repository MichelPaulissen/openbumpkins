/* SPDX-License-Identifier: GPL-3.0-only */
#include "archive_models.h"
#include "game_calls.h"
static void *self(uint32_t *r) { return (void *)(uintptr_t)r[6]; }
static uint32_t arg(uint32_t *r, unsigned n, unsigned reserve) {
  return BB_U32(r[3] + reserve + 8u + n * 4u);
}
static void construct(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_archive_construct(self(r), (const char *)(uintptr_t)arg(r, 0, 0)));
}
static void name(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_archive_name(self(r), (const char *)(uintptr_t)arg(r, 0, 0)));
}
static void destroy(uint32_t *r) { r[7] = bb_archive_destroy(self(r)); }
static void clear(uint32_t *r) {
  r[7] = bb_address_encode(bb_archive_clear(self(r)));
}
static void directory(uint32_t *r) {
  r[7] = bb_archive_directory_c(self(r), r[3] + 4u);
}
static void find_name(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_archive_find_name(self(r), (const char *)(uintptr_t)arg(r, 0, 0)));
}
static void find_index(uint32_t *r) {
  r[7] = bb_address_encode(bb_archive_find_index(self(r), arg(r, 0, 0)));
}
static void open_archive(uint32_t *r) { r[7] = bb_archive_open(self(r)); }
static void close_archive(uint32_t *r) {
  r[7] = bb_address_encode(bb_archive_close(self(r)));
}
static void member(uint32_t *r) {
  r[7] = bb_address_encode(
      bb_archive_member(self(r), (const char *)(uintptr_t)arg(r, 0, 0)));
}
static void release(uint32_t *r) {
  uint32_t view = arg(r, 0, 0);
  if (view)
    r[7] = bb_archive_release((void *)(uintptr_t)view);
}
static void seek(uint32_t *r) {
  r[7] = bb_archive_seek(self(r), arg(r, 0, 8), arg(r, 1, 8), r[9]);
}
static void view(uint32_t *r) {
  r[7] = bb_address_encode(bb_archive_view(self(r),
                                           (void *)(uintptr_t)arg(r, 0, 0),
                                           (void *)(uintptr_t)arg(r, 1, 0)));
}
static void read_member(uint32_t *r) {
  r[7] = bb_archive_read_c(self(r), (void *)(uintptr_t)arg(r, 0, 4),
                           arg(r, 1, 4), r[3] + 4u);
}
static void nothing(uint32_t *r) { (void)r; }
static void view_delete(uint32_t *r) {
  r[7] = bb_archive_view_delete(self(r), arg(r, 0, 0));
}
static void buffer(uint32_t *r) {
  if (!BB_U32(0x006c1b70u))
    r[7] = bb_archive_buffer();
}
static void add(uint32_t *r) {
  r[7] = bb_archive_add_c(
      self(r), (const char *)(uintptr_t)arg(r, 0, BB_ARCHIVE_ADD_BYTES),
      r[3] + 4u);
}
static void replace(uint32_t *r) {
  r[7] = bb_archive_replace_c(
      self(r), (void *)(uintptr_t)arg(r, 0, BB_ARCHIVE_REPLACE_BYTES),
      r[3] + 4u);
}
static void refresh(uint32_t *r) {
  r[7] = bb_archive_refresh_c(self(r), r[3] + 4u);
}
uint32_t bb_archive_directory(void *object) {
  return BB_GAME_CALL(0x004660e3u, bb_address_encode(object));
}
uint32_t bb_archive_read(void *object, void *out, uint32_t count) {
  return BB_GAME_CALL(0x00466e10u, bb_address_encode(object),
                      bb_address_encode(out), count);
}
uint32_t bb_archive_add(void *object, const char *path) {
  return BB_GAME_CALL(0x004662e1u, bb_address_encode(object),
                      bb_address_encode(path));
}
uint32_t bb_archive_replace(void *object, void *entry) {
  return BB_GAME_CALL(0x00466730u, bb_address_encode(object),
                      bb_address_encode(entry));
}
uint32_t bb_archive_refresh(void *object) {
  return BB_GAME_CALL(0x004665eeu, bb_address_encode(object));
}
const struct bb_model_target *bb_archive_model_find(void *unused, uint32_t id) {
  (void)unused;
  switch (id) {
#define MODEL(id, fn, argc, reserve, local)                                    \
  case id: {                                                                   \
    static const struct bb_model_target target = {id, fn, argc, reserve,       \
                                                  local};                      \
    return &target;                                                            \
  }
    MODEL(0x00465fb0u, construct, 1, 0, 0)
    MODEL(0x00465ffbu, destroy, 0, 0, 0)
    MODEL(0x00466040u, name, 1, 0, 0)
    MODEL(0x00466068u, clear, 0, 0, 0)
    MODEL(0x004660e3u, directory, 0, BB_ARCHIVE_DIRECTORY_BYTES, 0)
    MODEL(0x004662e1u, add, 1, BB_ARCHIVE_ADD_BYTES, 0)
    MODEL(0x004665eeu, refresh, 0, BB_ARCHIVE_REFRESH_BYTES, 0)
    MODEL(0x004666cau, nothing, 1, 0, 0)
    MODEL(0x004666e2u, buffer, 0, 0, 0)
    MODEL(0x00466730u, replace, 1, BB_ARCHIVE_REPLACE_BYTES, 0)
    MODEL(0x00466ad9u, find_name, 1, 0, 0)
    MODEL(0x00466b3au, find_index, 1, 0, 0)
    MODEL(0x00466b99u, open_archive, 0, 0, 0)
    MODEL(0x00466c1eu, close_archive, 0, 0, 0)
    MODEL(0x00466c66u, member, 1, 0, 0)
    MODEL(0x00466d59u, release, 1, 0, 0)
    MODEL(0x00466da0u, view, 2, 0, 0)
    MODEL(0x00466dfau, nothing, 0, 0, 0)
    MODEL(0x00466e10u, read_member, 2, 4, 0)
    MODEL(0x00466ec2u, seek, 2, 8, 1)
    MODEL(0x00466fa0u, view_delete, 1, 0, 0)
#undef MODEL
  default:
    return NULL;
  }
}
