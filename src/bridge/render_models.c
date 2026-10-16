/* SPDX-License-Identifier: GPL-3.0-only */
#include "render_models.h"
#include "interop.h"
#include <stdlib.h>
#include <string.h>

uint32_t bb_render_model_service(uint32_t identity, uint32_t self,
                                 const uint32_t *arguments, size_t words) {
  struct bb_call_state state = {0, self, 0, 0x202};
  bb_model_invoke(&state, identity, arguments, words);
  return state.eax;
}

uint32_t bb_render_model_method(uint32_t object, uint32_t offset,
                                const uint32_t *tail, size_t words) {
  uint32_t args[16] = {object};
  if (words > 15)
    abort();
  if (words)
    memcpy(args + 1, tail, words * sizeof *tail);
  return bb_render_model_service(BB_U32(BB_U32(object) + offset), 0, args,
                                 words + 1);
}

static void *self(uint32_t *r) { return (void *)(uintptr_t)r[6]; }
static uint32_t arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
static void invalidate(uint32_t *r) { r[7] = bb_display_invalidate(r[7]); }
static void multiply(uint32_t *r) {
  uint64_t value = bb_fixed_multiply_registers(arg(r, 0), arg(r, 1));
  r[7] = value;
  r[5] = value >> 32;
}
static void divide(uint32_t *r) {
  uint64_t value = bb_fixed_divide_registers(arg(r, 0), arg(r, 1));
  r[7] = value;
  r[5] = value >> 32;
}
static void frame(uint32_t *r) { r[7] = bb_render_frame(); }
static void visible(uint32_t *r) { r[7] = bb_terrain_visible(); }
static void construct(uint32_t *r) {
  bb_render_queue_reset(self(r));
  r[7] = r[6];
  r[5] = BB_U32(0x006024b8);
}
static void reset(uint32_t *r) {
  r[7] = bb_render_queue_reset(self(r));
  r[5] = BB_U32(0x006024b8);
}
static void advance(uint32_t *r) {
  r[7] = bb_render_queue_advance(self(r));
  r[5] = BB_U32(r[6] + 0x9610u);
}
static void compact(uint32_t *r) {
  int32_t count = BB_I32(r[6] + 0x9600u + BB_U32(r[6] + 0x9610u) * 4u);
  r[7] = bb_render_queue_compact(self(r));
  r[5] = count > 0 ? (uint32_t)count : 0;
}
static void queue(uint32_t *r, unsigned kind) {
  struct queue_result result;
  switch (kind) {
  case 0:
    bb_render_queue_pending_clear_c(self(r), r[7], r[5], &result);
    break;
  case 1:
    bb_render_queue_insert_c(self(r), r[7], r[5], arg(r, 0), arg(r, 1),
                             arg(r, 2), arg(r, 3), arg(r, 4), &result);
    break;
  case 2:
    bb_render_queue_both_c(self(r), r[7], r[5], arg(r, 0), arg(r, 1), arg(r, 2),
                           arg(r, 3), &result);
    break;
  case 3:
    bb_render_queue_pending_add_c(self(r), r[7], r[5], arg(r, 0), arg(r, 1),
                                  arg(r, 2), arg(r, 3), arg(r, 4), &result);
    break;
  case 4:
    bb_render_queue_translate_c(self(r), r[7], r[5], arg(r, 0), arg(r, 1),
                                &result);
    break;
  default:
    bb_render_queue_merge_c(self(r), r[7], r[5], &result);
  }
  r[7] = result.eax;
  r[5] = result.edx;
}
#define QUEUE(name, kind)                                                      \
  static void name(uint32_t *r) { queue(r, kind); }
QUEUE(pending_clear, 0)
QUEUE(insert, 1)
QUEUE(both, 2)
QUEUE(pending_add, 3)
QUEUE(translate, 4)
QUEUE(merge, 5)
#undef QUEUE
static void region(uint32_t *r) {
  r[7] = bb_render_region(arg(r, 0), arg(r, 1), arg(r, 2), arg(r, 3),
                          (void *)(uintptr_t)arg(r, 4), arg(r, 5), arg(r, 6));
}
static void process(uint32_t *r) {
  r[7] = bb_render_queue_process(self(r), (void *)(uintptr_t)arg(r, 0),
                                 arg(r, 1), arg(r, 2));
}
static void scroll(uint32_t *r) { r[7] = bb_render_queue_scroll(self(r)); }
static void viewport(uint32_t *r) {
  r[7] =
      bb_display_viewport(self(r), arg(r, 0), arg(r, 1), arg(r, 2), arg(r, 3));
  r[5] = arg(r, 1);
}
static void intersect(uint32_t *r) {
  r[7] =
      bb_display_intersect(self(r), arg(r, 0), arg(r, 1), arg(r, 2), arg(r, 3));
}
static void begin(uint32_t *r) { r[7] = bb_entity_begin(self(r)); }
static void project(uint32_t *r) {
  uint32_t start = arg(r, 0);
  int empty = !BB_U32(r[6] + 0x1008u);
  r[7] = bb_entity_project(self(r), start, arg(r, 1));
  if (empty)
    r[6] = start;
}
static void entity_translate(uint32_t *r) {
  if (BB_U32(r[6] + 0x1008u)) {
    uint32_t dx = arg(r, 0), dy = arg(r, 1);
    r[7] = bb_entity_translate_c(self(r), r[7], dx, dy);
    r[5] = dx;
    r[6] = dy;
  }
}
static void sprite(uint32_t *r) { r[7] = bb_entity_sprite(self(r)); }

static uint32_t result_flags(uint32_t flags, uint32_t value, uint32_t sign) {
  uint32_t low = value & 255u;
  low ^= low >> 4;
  return (flags & ~0x8d5u) | (!value ? 0x40u : 0) | (value & sign ? 0x80u : 0) |
         (((0x9669u >> (low & 15u)) & 1u) << 2);
}
static uint32_t comparison_flags(uint32_t flags, uint32_t left,
                                 uint32_t right) {
  uint32_t value = left - right;
  return result_flags(flags, value, 0x80000000u) | (left < right ? 1u : 0) |
         ((left ^ right ^ value) & 0x10u) |
         (((left ^ right) & (left ^ value)) & 0x80000000u ? 0x800u : 0);
}
static void dirty_result(uint32_t *r, uint64_t value) {
  r[7] = value;
  r[5] = value >> 32;
  if (!BB_U8(r[6] + 0x128u))
    r[8] = result_flags(r[8], 0, 0x80u);
  else if (BB_U32(r[6] + 0x28u))
    r[8] = result_flags(r[8], r[7], 0x80000000u);
  else
    r[8] = comparison_flags(r[8], r[7], r[5]);
}
static void dirty(uint32_t *r) {
  dirty_result(r, bb_entity_dirty_c(self(r), r[7], r[5]));
}
static void set_sprite(uint32_t *r) {
  dirty_result(r, bb_entity_set_sprite_c(self(r), arg(r, 0), r[5]));
}
static void bar(uint32_t *r) {
  r[7] = bb_entity_bar_c(r[7], arg(r, 0), arg(r, 1), arg(r, 2), arg(r, 3),
                         arg(r, 4), arg(r, 5));
}
static void selection(uint32_t *r) {
  if (BB_U8(r[6] + 0x128u))
    r[7] = bb_entity_selection(self(r));
  else {
    r[7] &= 0xffff0000u;
    r[8] = result_flags(r[8], 0, 0x8000u);
  }
}
static void reflection(uint32_t *r) {
  r[7] = BB_U32(0x006ad1ec);
  r[5] = BB_U32(r[6] + 0x58u);
  if (r[5] == r[7]) {
    r[6] = 0;
    r[8] = result_flags(r[8], 0, 0x80000000u);
  } else
    r[7] = bb_entity_reflection(self(r));
}
static void background(uint32_t *r) { r[7] = bb_render_background(); }
static void overlays(uint32_t *r) { r[7] = bb_render_overlays(); }
static void entity_draw(uint32_t *r) { r[7] = bb_entity_draw(self(r)); }

const struct bb_model_target *bb_render_model_find(void *unused,
                                                   uint32_t identity) {
  (void)unused;
  switch (identity) {
#define MODEL(id, body, words, reserve)                                        \
  case id: {                                                                   \
    static const struct bb_model_target target = {id, body, words, reserve,    \
                                                  0};                          \
    return &target;                                                            \
  }
    MODEL(0x00401d70u, multiply, 2, 0)
    MODEL(0x00401d90u, divide, 2, 0)
    MODEL(0x00403190u, invalidate, 0, 0)
    MODEL(0x0040b9e0u, frame, 0, 0)
    MODEL(0x0040c5a0u, visible, 0, 0)
    MODEL(0x0040c860u, background, 0, 0)
    MODEL(0x00418720u, construct, 0, 0)
    MODEL(0x00418730u, reset, 0, 0)
    MODEL(0x00418790u, advance, 0, 0)
    MODEL(0x004187f0u, compact, 0, 0)
    MODEL(0x004188f0u, pending_clear, 0, 0)
    MODEL(0x00418900u, insert, 5, 0)
    MODEL(0x00418a10u, both, 4, 0)
    MODEL(0x00418a70u, pending_add, 5, 0)
    MODEL(0x00418ad0u, translate, 2, 0)
    MODEL(0x00418b60u, merge, 0, 0)
    MODEL(0x00418f90u, region, 7, 0)
    MODEL(0x00419070u, process, 3, 0)
    MODEL(0x00419170u, scroll, 0, 0)
    MODEL(0x00429ac0u, overlays, 0, 0)
    MODEL(0x0042f410u, viewport, 4, 0)
    MODEL(0x0042f430u, intersect, 4, 0)
    MODEL(0x0042f4c0u, bb_display_scroll_model, 2, 16)
    MODEL(0x00433b90u, begin, 0, 0)
    MODEL(0x00434830u, project, 2, 0)
    MODEL(0x00434aa0u, entity_translate, 2, 0)
    MODEL(0x00437ee0u, dirty, 0, 0)
    MODEL(0x004384a0u, set_sprite, 1, 0)
    MODEL(0x004384b0u, entity_draw, 0, 0)
    MODEL(0x0043ef00u, bar, 6, 0)
    MODEL(0x0043f170u, selection, 0, 0)
    MODEL(0x0043f310u, reflection, 0, 0)
    MODEL(0x00454370u, sprite, 0, 0)
#undef MODEL
  default:
    return bb_display_model_find(unused, identity);
  }
}
