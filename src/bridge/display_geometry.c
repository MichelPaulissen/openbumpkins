/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#ifdef BB_MODEL_RUNTIME
#include "render_models.h"
#include <string.h>
#else
typedef uint32_t(BB_STDCALL *blit_fn)(void *, int32_t, int32_t, void *,
                                      const int32_t *, uint32_t);
static void *method(void *object, uint32_t offset) {
  return BB_POINTER((uintptr_t)BB_POINTER((uintptr_t)object) + offset);
}
#endif

/* 0x00403190..0x0040319a: invalidate cached buffers. The original changes
 * only AL, retaining the incoming upper 24 bits and all other registers.
 */
uint32_t bb_display_invalidate(uint32_t previous) {
  unsigned char value = BB_U8(0x00484454);
  BB_U8(0x004847bc) = value;
  return (previous & 0xffffff00) | value;
}

/* 0x0042f410..0x0042f42f. Boundary restores ECX and exposes top in EDX. */
uint32_t BB_THISCALL bb_display_viewport(void *object, int32_t left,
                                         int32_t top, int32_t width,
                                         int32_t height) {
  uint32_t owner = bb_address_encode(object);
  BB_I32(owner) = left;
  BB_I32(owner + 4) = top;
  BB_I32(owner + 8) = width;
  BB_I32(owner + 12) = height;
  return height;
}

static int32_t clamp(int32_t value, int32_t low, int32_t high) {
  if (value < low)
    value = low;
  if (value > high)
    value = high;
  return value;
}

/* 0x0042f430..0x0042f4b5: exclusive bounds, signed comparisons and wrapping
 * addition/subtraction. Clamp order also matters for negative viewport sizes.
 */
uint32_t BB_THISCALL bb_display_intersect(void *object, int32_t left,
                                          int32_t top, int32_t width,
                                          int32_t height) {
  uint32_t owner = bb_address_encode(object);
  int32_t old_left = BB_I32(owner), old_top = BB_I32(owner + 4);
  int32_t old_right = (uint32_t)old_left + BB_U32(owner + 8);
  int32_t old_bottom = (uint32_t)old_top + BB_U32(owner + 12);
  int32_t right = clamp((uint32_t)left + (uint32_t)width, old_left, old_right);
  int32_t bottom = clamp((uint32_t)top + (uint32_t)height, old_top, old_bottom);
  left = clamp(left, old_left, old_right);
  top = clamp(top, old_top, old_bottom);
  BB_I32(owner) = left;
  BB_I32(owner + 4) = top;
  BB_U32(owner + 8) = (uint32_t)right - (uint32_t)left;
  BB_U32(owner + 12) = (uint32_t)bottom - (uint32_t)top;
  return old_bottom;
}

/* 0x0042f4c0..0x0042f59e: copy the front surface into the back surface. The
 * negative-scroll source origin is zero, even with a nonzero viewport origin;
 * empty or inverted rectangles still reach BltFast.
 */
static uint32_t scroll(void *object, int32_t dx, int32_t dy, uint32_t scratch) {
#ifndef BB_MODEL_RUNTIME
  (void)scratch;
#endif
  uint32_t owner = bb_address_encode(object);
  uint32_t left = BB_U32(owner), top = BB_U32(owner + 4);
  uint32_t width = BB_U32(owner + 8), height = BB_U32(owner + 12);
  uint32_t end_x = left + width - 1, end_y = top + height - 1;
  uint32_t x = left, y = top;
  int32_t rect[4] = {left + (uint32_t)dx, top + (uint32_t)dy, end_x, end_y};
  if (rect[0] < (int32_t)left) {
    x -= (uint32_t)rect[0];
    rect[2] = (uint32_t)rect[0] + end_x;
    rect[0] = 0;
  }
  if (rect[1] < (int32_t)top) {
    y -= (uint32_t)rect[1];
    rect[3] = (uint32_t)rect[1] + top + height - 1;
    rect[1] = 0;
  }
  if ((int32_t)((uint32_t)rect[2] - (uint32_t)rect[0] + x) > (int32_t)end_x)
    rect[2] = (uint32_t)rect[0] - x + width + left - 1;
  if ((int32_t)(y - (uint32_t)rect[1] + (uint32_t)rect[3]) > (int32_t)end_y)
    rect[3] = (uint32_t)rect[1] - y + top + height - 1;
  rect[2] = (uint32_t)rect[2] + 1;
  rect[3] = (uint32_t)rect[3] + 1;
  void *destination = BB_POINTER(owner + 0x38);
  void *source = BB_POINTER(owner + 0x34);
#ifdef BB_MODEL_RUNTIME
  memcpy(BB_SPAN(scratch, sizeof rect), rect, sizeof rect);
  uint32_t target = BB_U32(BB_U32(bb_address_encode(destination)) + 0x1cu);
  uint32_t args[] = {bb_address_encode(destination), x,       y,
                     bb_address_encode(source),      scratch, 0x10};
  return bb_render_model_service(target, 0, args, 6);
#else
  return ((blit_fn)BB_CODE(method(destination, 0x1c)))(destination, x, y,
                                                       source, rect, 0x10);
#endif
}

#ifdef BB_MODEL_RUNTIME
void bb_display_scroll_model(uint32_t *input) {
  uint32_t args = input[3] + 24u;
  input[7] = scroll((void *)(uintptr_t)input[6], BB_U32(args),
                    BB_U32(args + 4u), input[3] + 4u);
}
uint32_t BB_THISCALL bb_display_scroll(void *object, int32_t dx, int32_t dy) {
  uint32_t args[] = {(uint32_t)dx, (uint32_t)dy};
  return bb_render_model_service(0x0042f4c0u, bb_address_encode(object), args,
                                 2);
}
#else
uint32_t BB_THISCALL bb_display_scroll(void *object, int32_t dx, int32_t dy) {
  return scroll(object, dx, dy, 0);
}
#endif

#ifndef BB_MODEL_RUNTIME
#ifdef _WIN32
#define BB_DISPLAY_INVALIDATE "_bb_display_invalidate"
#else
#define BB_DISPLAY_INVALIDATE "bb_display_invalidate"
#endif
void __attribute__((naked)) bb_display_invalidate_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "pushl %edx\n\t"
                   "pushl %eax\n\t"
                   "call " BB_DISPLAY_INVALIDATE "\n\t"
                   "addl $4, %esp\n\t"
                   "popl %edx\n\t"
                   "popl %ecx\n\t"
                   "ret");
}
#endif
