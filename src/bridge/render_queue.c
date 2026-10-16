/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "render_models.h"
#ifdef BB_MODEL_RUNTIME
#include "render_calls.h"
#include <limits.h>
#include <signal.h>
#endif

/* 0x00418730..0x00418782: three queue counts and position snapshots, then the
 * current index and shared snapshot. Other queue entries remain untouched.
 */
uint32_t BB_THISCALL bb_render_queue_reset(void *object) {
  uint32_t owner = bb_address_encode(object);
  for (uint32_t i = 0; i < 3; ++i) {
    BB_U32(owner + 0x9600 + i * 4) = 0;
    BB_U32(owner + 0x9614 + i * 4) = BB_U32(0x006024b8);
    BB_U32(owner + 0x9620 + i * 4) = BB_U32(0x00602484);
  }
  BB_U32(owner + 0x960c) = 0;
  BB_U32(owner + 0x9610) = 0;
  BB_U32(owner + 0x962c) = BB_U32(0x006024b8);
  BB_U32(owner + 0x9630) = BB_U32(0x00602484);
  return 0;
}

/* 0x00418790..0x004187e2: keep signed IDIV quotient/remainder and wrapping
 * increment. A zero divisor or INT_MIN/-1 retains the original division fault.
 */
int32_t BB_THISCALL bb_render_queue_advance(void *object) {
  uint32_t owner = bb_address_encode(object);
  uint32_t position = BB_U32(0x006024b8);
  uint32_t index = BB_U32(owner + 0x9610);
  BB_U32(owner + 0x9614 + index * 4) = position;
  index = BB_U32(owner + 0x9610);
  position = BB_U32(0x00602484);
  BB_U32(owner + 0x9620 + index * 4) = position;
  BB_U32(owner + 0x962c) = BB_U32(0x006024b8);
  int32_t next = BB_U32(owner + 0x9610) + 1;
  BB_U32(owner + 0x9630) = BB_U32(0x00602484);
  int32_t divisor = BB_I32(0x00484454);
#ifdef BB_MODEL_RUNTIME
  if (!divisor || (next == INT32_MIN && divisor == -1)) {
    raise(SIGFPE);
    abort();
  }
#endif
  int32_t quotient = next / divisor;
  BB_I32(owner + 0x9610) = next % divisor;
  return quotient;
}

/* 0x004187f0..0x004188e8: compact marked records in the current 512-entry
 * bank. Copy five DWORD fields, retaining each destination's sixth field.
 */
uint32_t BB_THISCALL bb_render_queue_compact(void *object) {
  uint32_t owner = bb_address_encode(object);
  int32_t count = BB_I32(owner + 0x9600 + BB_U32(owner + 0x9610) * 4);
  uint32_t output = 0;
  for (int32_t input = 0; input < count; ++input) {
    uint32_t index = BB_U32(owner + 0x9610);
    uint32_t source = owner + 0x600 + ((index << 9) + (uint32_t)input) * 24;
    if (BB_U32(source + 16)) {
      for (uint32_t field = 0; field < 20; field += 4) {
        index = BB_U32(owner + 0x9610);
        uint32_t value = BB_U32(owner + 0x600 +
                                ((index << 9) + (uint32_t)input) * 24 + field);
        BB_U32(owner + 0x600 + ((index << 9) + output) * 24 + field) = value;
      }
      ++output;
    }
  }
  uint32_t index = BB_U32(owner + 0x9610);
  BB_U32(owner + 0x9600 + index * 4) = output;
  return index;
}

/* These leaf routines expose EAX/EDX even on rejected rectangles. Keep their
 * state explicit in C, with the original ECX restored by the ABI boundary.
 */
static uint32_t queue_record(uint32_t owner, uint32_t item) {
  return owner + 0x600 + ((BB_U32(owner + 0x9610) << 9) + item) * 24;
}
static uint32_t queue_count(uint32_t owner) {
  return owner + 0x9600 + BB_U32(owner + 0x9610) * 4;
}

/* 0x004188f0..0x004188fa: reset only the pending-rectangle count. */
void bb_render_queue_pending_clear_c(void *object, uint32_t eax, uint32_t edx,
                                     struct queue_result *result) {
  result->eax = eax;
  result->edx = edx;
  BB_U32(bb_address_encode(object) + 0x960c) = 0;
}

/* 0x00418900..0x00418a08: exclusive bounds, no coordinate clamping. Signed
 * counts below 512 append; a full queue invalidates every display buffer.
 * Re-read index/count between fields, retaining the original alias behavior.
 */
void bb_render_queue_insert_c(void *object, uint32_t eax, uint32_t edx,
                              int32_t left, int32_t top, int32_t right,
                              int32_t bottom, uint32_t marker,
                              struct queue_result *result) {
  uint32_t owner = bb_address_encode(object);
  result->eax = eax;
  result->edx = edx;
  if (right <= 0 || bottom <= 0)
    return;
  result->eax = BB_U32(0x00484444);
  if (left >= (int32_t)result->eax)
    return;
  result->eax = BB_U32(0x00484448);
  if (top >= (int32_t)result->eax || left >= right || top >= bottom)
    return;
  result->edx = BB_U32(owner + 0x9610);
  uint32_t count = BB_U32(owner + 0x9600 + result->edx * 4);
  result->eax = count;
  if ((int32_t)count >= 512) {
    result->eax = bb_display_invalidate(count);
    return;
  }
  BB_U32(owner + 0x600 + ((result->edx << 9) + count) * 24) = left;
  uint32_t values[4] = {top, right, bottom, marker};
  for (uint32_t field = 0; field < 4; ++field) {
    count = BB_U32(queue_count(owner));
    BB_U32(queue_record(owner, count) + 4 + field * 4) = values[field];
  }
  result->eax = marker;
  result->edx = BB_U32(owner + 0x9610);
  ++BB_U32(owner + 0x9600 + result->edx * 4);
}

/* 0x00418a10..0x00418a6e: add to buffers zero and one with marker 2, then
 * restore the original index, exposing the second insertion's registers.
 */
void bb_render_queue_both_c(void *object, uint32_t eax, uint32_t edx,
                            int32_t left, int32_t top, int32_t right,
                            int32_t bottom, struct queue_result *result) {
  (void)eax;
  uint32_t owner = bb_address_encode(object);
  uint32_t index = BB_U32(owner + 0x9610);
  BB_U32(owner + 0x9610) = 0;
  bb_render_queue_insert_c(object, bottom, edx, left, top, right, bottom, 2,
                           result);
  BB_U32(owner + 0x9610) = 1;
  bb_render_queue_insert_c(object, bottom, result->edx, left, top, right,
                           bottom, 2, result);
  BB_U32(owner + 0x9610) = index;
}

/* 0x00418a70..0x00418ace: append an unchecked pending rectangle. Its marker
 * field at +16 remains untouched; the fifth argument goes to +20 instead.
 */
void bb_render_queue_pending_add_c(void *object, uint32_t eax, uint32_t edx,
                                   int32_t left, int32_t top, int32_t right,
                                   int32_t bottom, uint32_t value,
                                   struct queue_result *result) {
  (void)eax;
  (void)edx;
  uint32_t owner = bb_address_encode(object);
  uint32_t values[5] = {left, top, right, bottom, value};
  for (uint32_t field = 0; field < 5; ++field) {
    result->edx = BB_U32(owner + 0x960c);
    BB_U32(owner + result->edx * 24 + (field == 4 ? 20 : field * 4)) =
        values[field];
  }
  ++BB_U32(owner + 0x960c);
  result->eax = value;
}

/* Typed C callers need no incidental register result from pending append. */
void bb_render_queue_pending_add(void *object, int32_t left, int32_t top,
                                 int32_t right, int32_t bottom, uint32_t value) {
  struct queue_result result;
  bb_render_queue_pending_add_c(object, 0, 0, left, top, right, bottom, value,
                                &result);
}

/* 0x00418ad0..0x00418b57: translate every record, including unmarked ones,
 * with wrapping 32-bit coordinates and a live count/index between fields.
 */
void bb_render_queue_translate_c(void *object, uint32_t eax, uint32_t edx,
                                 uint32_t dx, uint32_t dy,
                                 struct queue_result *result) {
  (void)eax;
  uint32_t owner = bb_address_encode(object);
  result->eax = BB_U32(owner + 0x9610);
  result->edx = edx;
  if (BB_I32(owner + 0x9600 + result->eax * 4) <= 0)
    return;
  result->edx = dx;
  uint32_t item = 0;
  do {
    BB_U32(queue_record(owner, item)) += dx;
    BB_U32(queue_record(owner, item) + 4) += dy;
    BB_U32(queue_record(owner, item) + 8) += dx;
    result->eax = (BB_U32(owner + 0x9610) << 9) + item;
    BB_U32(owner + 0x600 + result->eax * 24 + 12) += dy;
    ++item;
  } while ((int32_t)item < BB_I32(queue_count(owner)));
}

static int32_t minimum(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t maximum(int32_t a, int32_t b) { return a > b ? a : b; }
static uint32_t difference(int32_t a, int32_t b) {
  return (uint32_t)a - (uint32_t)b;
}

/* 0x00418b60..0x00418f86: ordered rectangle merging, restarting the entire
 * scan after every merge. Preserve the original cross-width/cross-height
 * expression and signed comparisons of wrapping 32-bit areas; substituting
 * ordinary union-area arithmetic would change the merging decisions.
 */
void bb_render_queue_merge_c(void *object, uint32_t eax, uint32_t edx,
                             struct queue_result *result) {
  (void)eax;
  (void)edx;
  uint32_t owner = bb_address_encode(object);
  result->eax = BB_U32(owner + 0x9610);
  result->edx = BB_U32(owner + 0x9600 + result->eax * 4);
  if ((int32_t)result->edx <= 0)
    return;
  uint32_t first = 0;
  do {
    uint32_t a = queue_record(owner, first);
    if (BB_U32(a + 16)) {
      uint32_t second = first + 1;
      uint32_t b = queue_record(owner, second);
      while ((int32_t)second < BB_I32(queue_count(owner))) {
        if (BB_U32(b + 16)) {
          int32_t l1 = BB_I32(a), t1 = BB_I32(a + 4), r1 = BB_I32(a + 8),
                  b1 = BB_I32(a + 12), l2 = BB_I32(b), t2 = BB_I32(b + 4),
                  r2 = BB_I32(b + 8), b2 = BB_I32(b + 12);
          int32_t left = minimum(l1, l2), top = minimum(t1, t2),
                  right = maximum(r1, r2), bottom = maximum(b1, b2);
          uint32_t bounds = difference(right, left) * difference(bottom, top);
          int merge;
          if (l1 < r2 && t1 < b2 && r1 > l2 && b1 > t2) {
            uint32_t intersection =
                difference(minimum(r1, r2), maximum(l1, l2)) *
                difference(minimum(b1, b2), maximum(t1, t2));
            uint32_t expression = difference(t1, b1) * difference(b2, t2) +
                                  difference(l2, r2) * difference(r1, l1) +
                                  intersection + bounds;
            merge = (int32_t)expression <= (int32_t)intersection;
          } else {
            uint32_t areas = difference(r1, l1) * difference(b1, t1) +
                             difference(r2, l2) * difference(b2, t2);
            merge = (int32_t)(areas * 2) > (int32_t)bounds &&
                    (int32_t)bounds < 10000;
          }
          if (merge) {
            BB_I32(a) = left;
            BB_I32(queue_record(owner, first) + 4) = top;
            BB_I32(queue_record(owner, first) + 8) = right;
            BB_I32(queue_record(owner, first) + 12) = bottom;
            uint32_t marker = queue_record(owner, first) + 16;
            BB_I32(marker) = maximum(BB_I32(marker),
                                     BB_I32(queue_record(owner, second) + 16));
            BB_U32(queue_record(owner, second) + 16) = 0;
            first = UINT32_MAX;
            break;
          }
        }
        ++second;
        b += 24;
      }
    }
    ++first;
    result->eax = first;
    result->edx = BB_U32(owner + 0x9610);
  } while ((int32_t)first < BB_I32(owner + 0x9600 + result->edx * 4));
}

/* 0x00418f90..0x0041906c: render background, visible entities intersecting
 * the inclusive rectangle, then overlays. List links and debug state remain
 * live across drawing/logging callbacks. Debug formatting/logging remains
 * bridged; terrain, entities and overlays call C directly.
 */
uint32_t bb_render_region(int32_t left, int32_t top, int32_t right,
                          int32_t bottom, void *first, uint32_t start,
                          uint32_t extent) {
#ifndef BB_MODEL_RUNTIME
  typedef void(BB_THISCALL * log_fn)(void *, const char *);
  typedef int (*format_fn)(char *, const char *, ...);
#endif
#ifdef BB_MODEL_RUNTIME
  bb_render_model_service(0x0040c860u, 0, NULL, 0);
#else
  bb_render_background();
#endif
  uint32_t node = bb_address_encode(first);
  while (node) {
    if (BB_U8(node + 0x128) &&
        left <= BB_I16(node + 0xc2) &&
        top <= BB_I16(node + 0xc4) &&
        BB_I16(node + 0xbe) <= right &&
        BB_I16(node + 0xc0) <= bottom)
#ifdef BB_MODEL_RUNTIME
      bb_render_model_service(0x004384b0u, node, NULL, 0);
#else
      bb_entity_draw((void *)(uintptr_t)node);
#endif
    uint32_t next = BB_U32(node + 4);
    if ((!next || (int32_t)(start + extent) < BB_I32(next + 0x86)) &&
        BB_U32(0x00484268)) {
#ifdef BB_MODEL_RUNTIME
      uint32_t values[] = {0x006033d8u, 0x00486f3cu,
          BB_U32(bb_address_encode(first) + 0x8e), BB_U32(node + 0x8e), start,
          start + extent};
      bb_render_call(0x0046bae0u, 0, values, 6);
      uint32_t text = 0x006033d8u;
      bb_render_model_service(0x0041de40u, 0x00662c98u, &text, 1);
#else
      ((format_fn)BB_CODE((uintptr_t)0x0046bae0))(
          (char *)(uintptr_t)0x006033d8, (const char *)(uintptr_t)0x00486f3c,
          BB_U32(bb_address_encode(first) + 0x8e), BB_U32(node + 0x8e), start,
          start + extent);
      ((log_fn)BB_CODE((uintptr_t)0x0041de40))((void *)(uintptr_t)0x00662c98,
                                      (const char *)(uintptr_t)0x006033d8);
#endif
    }
    node = BB_U32(node + 4);
    if (node && (int32_t)(start + extent) < BB_I32(node + 0x86))
      node = 0;
  }
#ifdef BB_MODEL_RUNTIME
  return bb_render_model_service(0x00429ac0u, 0, NULL, 0);
#else
  return bb_render_overlays();
#endif
}

/* 0x00419070..0x0041916d: reset the viewport for each marked rectangle,
 * decrement its marker before intersecting, then render its inclusive bounds.
 * Drawing can change the current bank/count, so re-read both during iteration.
 */
uint32_t BB_THISCALL bb_render_queue_process(void *object,
                                                           void *first,
                                                           uint32_t start,
                                                           uint32_t extent) {
  uint32_t owner = bb_address_encode(object);
  for (uint32_t i = 0; (int32_t)i < BB_I32(queue_count(owner)); ++i) {
    uint32_t record = queue_record(owner, i);
    if (BB_U32(record + 16)) {
      bb_display_viewport((void *)(uintptr_t)0x004eebc8, BB_I32(0x00484494),
                          BB_I32(0x00484498), BB_I32(0x00604638),
                          BB_I32(0x00513440));
      record = queue_record(owner, i);
      uint32_t left = BB_U32(record), top = BB_U32(record + 4),
               right = BB_U32(record + 8), bottom = BB_U32(record + 12);
      --BB_U32(record + 16);
      bb_display_intersect((void *)(uintptr_t)0x004eebc8, left, top,
                           right - left + 1, bottom - top + 1);
      bb_render_region(left, top, right, bottom, first, start, extent);
    }
  }
  return BB_U32(owner + 0x9610);
}

/* C callers do not consume the original leaf volatile-register results. */
void bb_render_queue_insert(void *object, int32_t left, int32_t top,
                            int32_t right, int32_t bottom, uint32_t marker) {
  struct queue_result ignored;
  bb_render_queue_insert_c(object, 0, 0, left, top, right, bottom, marker,
                           &ignored);
}
void bb_render_queue_both(void *object, int32_t left, int32_t top,
                          int32_t right, int32_t bottom) {
  struct queue_result ignored;
  bb_render_queue_both_c(object, 0, 0, left, top, right, bottom, &ignored);
}
void bb_render_queue_merge(void *object) {
  struct queue_result ignored;
  bb_render_queue_merge_c(object, 0, 0, &ignored);
}

static void insert_exposed(void *object, uint32_t left, uint32_t top,
                           uint32_t right, uint32_t bottom) {
  struct queue_result ignored;
  bb_render_queue_insert_c(object, 0, 0, left, top, right, bottom, 1, &ignored);
}

/* 0x00419170..0x004193cc: scroll the display, shift the queue and entities,
 * and enqueue the cursor, exposed viewport edges and pending rectangle strips.
 * The bank snapshots decide whether work is needed; the shared snapshots
 * supply the offsets. This routine does not update either set of snapshots.
 */
uint32_t BB_THISCALL bb_render_queue_scroll(void *object) {
  uint32_t owner = bb_address_encode(object);
  uint32_t left = BB_U32(0x004eebc8), top = BB_U32(0x004eebcc),
           right = left + BB_U32(0x004eebd0), bottom = top + BB_U32(0x004eebd4);
  uint32_t bank = BB_U32(owner + 0x9610);
  if (BB_U32(owner + 0x9614 + bank * 4) == BB_U32(0x006024b8) &&
      BB_U32(owner + 0x9620 + bank * 4) == BB_U32(0x00602484))
    return BB_U32(0x00602484);
  uint32_t dx = BB_U32(0x006024b8) - BB_U32(owner + 0x962c),
           dy = BB_U32(0x00602484) - BB_U32(owner + 0x9630);
  bb_display_scroll((void *)(uintptr_t)0x004eebc8, dx, dy);
  dx = 0u - dx;
  dy = 0u - dy;
  struct queue_result ignored;
  bb_render_queue_translate_c(object, 0, 0, dx, dy, &ignored);
  bb_entity_translate_c((void *)(uintptr_t)0x00553f90, 0, dx, dy);
  uint32_t cursor = BB_U32(0x00484700) ? 0x004846c8 : 0x004846b8;
  uint32_t x = BB_U32(cursor) + dx, y = BB_U32(cursor + 4) + dy;
  insert_exposed(object, x - 24, y - 24, x + 48, y + 48);
  if ((int32_t)dx > 0)
    insert_exposed(object, left, top, left + dx, bottom);
  else if ((int32_t)dx < 0)
    insert_exposed(object, right + dx, top, right, bottom);
  if ((int32_t)dy > 0)
    insert_exposed(object, left, top, right, top + dy);
  else if ((int32_t)dy < 0)
    insert_exposed(object, left, bottom + dy, right, bottom);
  uint32_t i = 0;
  if (BB_I32(owner + 0x960c) <= 0)
    return BB_U32(owner + 0x960c);
  do {
    uint32_t pending = owner + i * 24;
    uint32_t l = BB_U32(pending), t = BB_U32(pending + 4),
             r = BB_U32(pending + 8), b = BB_U32(pending + 12);
    if ((int32_t)dx > 0)
      insert_exposed(object, r, (int32_t)dy > 0 ? t : t + dy, r + dx,
                     (int32_t)dy > 0 ? b + dy : b);
    else
      insert_exposed(object, l + dx, (int32_t)dy > 0 ? t : t + dy, l,
                     (int32_t)dy > 0 ? b + dy : b);
    l = BB_U32(pending);
    t = BB_U32(pending + 4);
    r = BB_U32(pending + 8);
    b = BB_U32(pending + 12);
    if ((int32_t)dy > 0)
      insert_exposed(object, (int32_t)dx > 0 ? l : l + dx, b,
                     (int32_t)dx > 0 ? r + dx : r, b + dy);
    else
      insert_exposed(object, (int32_t)dx > 0 ? l : l + dx, t + dy,
                     (int32_t)dx > 0 ? r + dx : r, t);
    if (BB_U32(pending + 20))
      insert_exposed(object, BB_U32(pending), BB_U32(pending + 4),
                     BB_U32(pending + 8), BB_U32(pending + 12));
    ++i;
  } while ((int32_t)i < BB_I32(owner + 0x960c));
  return i;
}

#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_RENDER_QUEUE_RESET "_bb_render_queue_reset"
#define BB_RENDER_QUEUE_ADVANCE "_bb_render_queue_advance"
#define BB_RENDER_QUEUE_COMPACT "_bb_render_queue_compact"
#define BB_QUEUE_PREFIX "_"
#define BB_RENDER_QUEUE_PROCESS "_bb_render_queue_process"
#define BB_RENDER_QUEUE_SCROLL "_bb_render_queue_scroll"
#else
#define BB_RENDER_QUEUE_RESET "bb_render_queue_reset"
#define BB_RENDER_QUEUE_ADVANCE "bb_render_queue_advance"
#define BB_RENDER_QUEUE_COMPACT "bb_render_queue_compact"
#define BB_QUEUE_PREFIX ""
#define BB_RENDER_QUEUE_PROCESS "bb_render_queue_process"
#define BB_RENDER_QUEUE_SCROLL "bb_render_queue_scroll"
#endif

/* Known leaf register results retained at the original entry boundaries. */
void __attribute__((naked)) bb_render_queue_construct_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "call " BB_RENDER_QUEUE_RESET "\n\t"
                   "popl %ecx\n\t"
                   "movl %ecx, %eax\n\t"
                   "movl 0x006024b8, %edx\n\t"
                   "ret");
}
void __attribute__((naked)) bb_render_queue_reset_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "call " BB_RENDER_QUEUE_RESET "\n\t"
                   "popl %ecx\n\t"
                   "movl 0x006024b8, %edx\n\t"
                   "ret");
}
void __attribute__((naked)) bb_render_queue_advance_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "call " BB_RENDER_QUEUE_ADVANCE "\n\t"
                   "popl %ecx\n\t"
                   "movl 0x9610(%ecx), %edx\n\t"
                   "ret");
}
void __attribute__((naked)) bb_render_queue_compact_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "movl 0x9610(%ecx), %eax\n\t"
                   "movl 0x9600(%ecx,%eax,4), %edx\n\t"
                   "testl %edx, %edx\n\t"
                   "jg 1f\n\t"
                   "xorl %edx, %edx\n"
                   "1: pushl %edx\n\t"
                   "call " BB_RENDER_QUEUE_COMPACT "\n\t"
                   "popl %edx\n\t"
                   "popl %ecx\n\t"
                   "ret");
}

/* Marshal thiscall arguments into C's explicit register result. Original
 * arguments remain on the caller's stack until the final ret N. */
#define BB_QUEUE_PUSH_0 ""
#define BB_QUEUE_PUSH_2 "pushl 32(%esp)\n\tpushl 32(%esp)\n\t"
#define BB_QUEUE_PUSH_4                                                        \
  "pushl 40(%esp)\n\tpushl 40(%esp)\n\tpushl 40(%esp)\n\tpushl 40(%esp)\n\t"
#define BB_QUEUE_PUSH_5                                                        \
  "pushl 44(%esp)\n\tpushl 44(%esp)\n\tpushl 44(%esp)\n\tpushl 44(%esp)\n\t"   \
  "pushl 44(%esp)\n\t"
#define BB_QUEUE_BOUNDARY(name, helper, count, previous, owner, cleanup)       \
  void __attribute__((naked)) name(void) {                                     \
    __asm__ volatile(                                                          \
        "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tsubl $8, %esp\n\t"          \
        "pushl %esp\n\t" BB_QUEUE_PUSH_##count                                 \
        "pushl " #previous "(%esp)\n\tpushl " #previous "(%esp)\n\t"           \
        "pushl " #owner "(%esp)\n\tcall " BB_QUEUE_PREFIX #helper "\n\t"       \
        "addl $" #previous ", %esp\n\tpopl %eax\n\tpopl %edx\n\t"              \
        "addl $8, %esp\n\tpopl %ecx\n\tret $" #cleanup);                       \
  }
BB_QUEUE_BOUNDARY(bb_render_queue_pending_clear_entry,
                  bb_render_queue_pending_clear_c, 0, 16, 28, 0)
BB_QUEUE_BOUNDARY(bb_render_queue_insert_entry, bb_render_queue_insert_c, 5, 36,
                  48, 20)
BB_QUEUE_BOUNDARY(bb_render_queue_both_entry, bb_render_queue_both_c, 4, 32, 44,
                  16)
BB_QUEUE_BOUNDARY(bb_render_queue_pending_add_entry,
                  bb_render_queue_pending_add_c, 5, 36, 48, 20)
BB_QUEUE_BOUNDARY(bb_render_queue_translate_entry, bb_render_queue_translate_c,
                  2, 24, 36, 8)
BB_QUEUE_BOUNDARY(bb_render_queue_merge_entry, bb_render_queue_merge_c, 0, 16,
                  28, 0)
#undef BB_QUEUE_BOUNDARY

/* Empty processing and unchanged scrolling do not call child functions.
 * Preserve their known volatile-register results at the entry boundary.
 */
void __attribute__((naked)) bb_render_queue_process_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "movl 0x9610(%ecx), %eax\n\t"
                   "movl 0x9600(%ecx,%eax,4), %ecx\n\t"
                   "testl %ecx, %ecx\n\t"
                   "jg 1f\n\t"
                   "addl $4, %esp\n\tret $12\n"
                   "1: popl %ecx\n\tjmp " BB_RENDER_QUEUE_PROCESS);
}
void __attribute__((naked)) bb_render_queue_scroll_entry(void) {
  __asm__ volatile("movl 0x9610(%ecx), %eax\n\t"
                   "leal (%ecx,%eax,4), %edx\n\t"
                   "movl 0x9614(%edx), %eax\n\t"
                   "cmpl 0x006024b8, %eax\n\t"
                   "jne 1f\n\t"
                   "movl 0x00602484, %eax\n\t"
                   "cmpl 0x9620(%edx), %eax\n\t"
                   "jne 1f\n\t"
                   "movl %edx, %ecx\n\tmovl %eax, %edx\n\tret\n"
                   "1: jmp " BB_RENDER_QUEUE_SCROLL);
}

#endif
