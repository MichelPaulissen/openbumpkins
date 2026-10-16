/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include "media_devices.h"
#include <stddef.h>
#include <string.h>
#ifdef BB_MODEL_RUNTIME
#include "render_models.h"
#else

typedef uint32_t(BB_STDCALL *cooperative_fn)(void *, void *, uint32_t);
typedef uint32_t(BB_STDCALL *mode_fn)(void *, uint32_t, uint32_t, uint32_t);
typedef uint32_t(BB_STDCALL *callback_fn)(void *, void *);
typedef uint32_t(BB_STDCALL *enumerate_fn)(void *, uint32_t, void *, void *,
                                           callback_fn);
typedef uint32_t(BB_STDCALL *create_surface_fn)(void *, void *, void *, void *);
typedef uint32_t(BB_STDCALL *attached_fn)(void *, void *, void *);
typedef uint32_t(BB_STDCALL *object_fn)(void *);
typedef uint32_t(BB_STDCALL *message_fn)(void *, const char *, const char *,
                                         uint32_t);
typedef uint32_t (*fatal_fn)(uint32_t);

static void *method(void *object, uint32_t offset) {
  return BB_POINTER((uintptr_t)BB_POINTER((uintptr_t)object) + offset);
}
#endif

static uint32_t message(uint32_t box, void *window, uint32_t text) {
#ifdef BB_MODEL_RUNTIME
  uint32_t args[] = {bb_address_encode(window), text, 0x004887e4, 0x10};
  return bb_render_model_service(box, 0, args, 4);
#else
  return ((message_fn)BB_CODE(box))(window, (const char *)(uintptr_t)text,
                                    (const char *)(uintptr_t)0x004887e4, 0x10);
#endif
}
static uint32_t fatal(void) {
#ifdef BB_MODEL_RUNTIME
  return bb_render_model_service(0x00403a40, 0, (uint32_t[]){0}, 1);
#else
  return ((fatal_fn)BB_CODE((uintptr_t)0x00403a40))(0);
#endif
}

/* 0x0042ec00..0x0042ec2c: other fields intentionally remain untouched. */
void *BB_THISCALL bb_display_construct(void *object) {
  uint32_t owner = bb_address_encode(object);
  BB_U32(owner + 0x1c) = 0xffffff00;
  BB_U32(owner + 0x18) = 0xffffff00;
  BB_U8(owner + 0x20ebc) = 255;
  BB_U32(owner + 0x20ec2) = 0;
  BB_U8(owner + 0x20ebd) = 255;
  BB_U32(owner + 0x20ebe) = 0;
  return object;
}

/* 0x0042ede0..0x0042ee24: append width/height/depth, without filtering or a
 * count reset. The boundary returns 1, depth in ECX and byte offset in EDX.
 */
uint32_t bb_display_mode_record(void *descriptor, uint32_t *depth) {
  uint32_t source = bb_address_encode(descriptor);
  uint32_t offset = BB_U32(0x00488784) * 12;
  uint32_t count = BB_U32(0x00488784) + 1;
  BB_U32(0x006b1920 + offset) = BB_U32(source + 12);
  BB_U32(0x006b1924 + offset) = BB_U32(source + 8);
  *depth = BB_U32(source + 0x54);
  BB_U32(0x006b1928 + offset) = *depth;
  BB_U32(0x00488784) = count;
  return offset;
}

/* 0x0042ee50..0x0042ef10: the CreateSurface callback can change the descriptor
 * and output fields. Keep its updated back-buffer count and fresh front
 * pointer.
 */
static uint32_t primary(void *object, uint32_t scratch) {
  uint32_t owner = bb_address_encode(object);
  memset(BB_SPAN(scratch, 112), 0, 112);
  BB_U32(scratch) = 108;
  BB_U32(scratch + 4) = 0x21;
  BB_U32(scratch + 20) = 1;
  BB_U32(scratch + 104) = 0x218;
  void *draw = BB_POINTER(owner + 0x30);
#ifdef BB_MODEL_RUNTIME
  uint32_t result = bb_render_model_method(
      bb_address_encode(draw), 0x18, (uint32_t[]){scratch, owner + 0x34, 0}, 3);
#else
  uint32_t result = ((create_surface_fn)BB_CODE(method(draw, 0x18)))(
      draw, (void *)(uintptr_t)scratch, (void *)(owner + 0x34), NULL);
#endif
  uint32_t box = BB_U32(0x006ca558);
  if (result) {
    message(box, BB_POINTER(owner + 0x2c), 0x00488814);
    fatal();
  }
  BB_U32(0x00484454) = BB_U32(scratch + 20) + 1;
  BB_U32(scratch + 108) = 4;
  void *front = BB_POINTER(owner + 0x34);
#ifdef BB_MODEL_RUNTIME
  result = bb_render_model_method(bb_address_encode(front), 0x30,
                                  (uint32_t[]){scratch + 108, owner + 0x38}, 2);
#else
  result = ((attached_fn)BB_CODE(method(front, 0x30)))(
      front, (void *)(uintptr_t)(scratch + 108), (void *)(owner + 0x38));
#endif
  if (result) {
    message(box, BB_POINTER(owner + 0x2c), 0x004887f4);
    return fatal();
  }
  return result;
}

/* 0x0042ec40..0x0042eddb: DirectDraw, exclusive mode, primary/back surfaces
 * and three cursor surfaces. Mode/enum statuses are ignored. Cursor failures
 * return the message-box result; creation/cooperative failures invoke fatal.
 * The second stack argument is unused, but remains part of the original ABI.
 */
static uint32_t initialize(void *object, void *window, uint32_t unused,
                           uint32_t scratch) {
  (void)unused;
  uint32_t owner = bb_address_encode(object);
#ifdef BB_MODEL_RUNTIME
  uint32_t result = bb_render_model_service(
      BB_U32(0x006ca33c), 0, (uint32_t[]){0, owner + 0x30, 0}, 3);
#else
  uint32_t result =
      bb_media_draw_create_entry(NULL, (void **)(owner + 0x30), NULL);
#endif
  uint32_t box = BB_U32(0x006ca558);
  if (result) {
    message(box, window, 0x004887c8);
    fatal();
  }
  void *draw = BB_POINTER(owner + 0x30);
  BB_SET_POINTER(owner + 0x2c, window);
#ifdef BB_MODEL_RUNTIME
  uint32_t status =
      bb_render_model_method(bb_address_encode(draw), 0x50,
                             (uint32_t[]){bb_address_encode(window), 0x53}, 2);
#else
  uint32_t status =
      ((cooperative_fn)BB_CODE(method(draw, 0x50)))(draw, window, 0x53);
#endif
  if (status) {
    message(box, window, 0x004887a4);
    fatal();
  }
  draw = BB_POINTER(owner + 0x30);
#ifdef BB_MODEL_RUNTIME
  bb_render_model_method(bb_address_encode(draw), 0x20,
                         (uint32_t[]){0, 0, 0, 0x0042ede0}, 4);
#else
  ((enumerate_fn)BB_CODE(method(draw, 0x20)))(
      draw, 0, NULL, NULL, (callback_fn)bb_display_mode_callback_entry);
#endif
  draw = BB_POINTER(owner + 0x30);
#ifdef BB_MODEL_RUNTIME
  bb_render_model_method(
      bb_address_encode(draw), 0x54,
      (uint32_t[]){BB_U32(0x00484444), BB_U32(0x00484448), 8}, 3);
#else
  ((mode_fn)BB_CODE(method(draw, 0x54)))(draw, BB_U32(0x00484444),
                                         BB_U32(0x00484448), 8);
#endif
  bb_display_primary(object);
  memset(BB_SPAN(scratch, 108), 0, 108);
  BB_U32(scratch) = 108;
  BB_U32(scratch + 4) = 7;
  BB_U32(scratch + 8) = BB_U32(scratch + 12) = 48;
  BB_U32(scratch + 104) = 0x840;
  for (uint32_t offset = 0x40; offset <= 0x48; offset += 4) {
    draw = BB_POINTER(owner + 0x30);
#ifdef BB_MODEL_RUNTIME
    status =
        bb_render_model_method(bb_address_encode(draw), 0x18,
                               (uint32_t[]){scratch, owner + offset, 0}, 3);
#else
    status = ((create_surface_fn)BB_CODE(method(draw, 0x18)))(
        draw, (void *)(uintptr_t)scratch, (void *)(owner + offset), NULL);
#endif
    if (status)
      return message(box, window, 0x00488788);
  }
  BB_U32(owner + 0x10) = 0;
  BB_U32(owner) = 0;
  BB_U32(owner + 4) = 0;
  BB_U32(owner + 8) = BB_U32(0x00484444);
  BB_U32(owner + 12) = BB_U32(0x00484448);
  return 0;
}

/* 0x0042ee30..0x0042ee48: restore desktop mode, release the current draw
 * object, leave its stored pointer and other resources unchanged.
 */
uint32_t BB_THISCALL bb_display_release(void *object) {
  uint32_t owner = bb_address_encode(object);
  void *draw = BB_POINTER(owner + 0x30);
#ifdef BB_MODEL_RUNTIME
  bb_render_model_method(bb_address_encode(draw), 0x4c, NULL, 0);
  draw = BB_POINTER(owner + 0x30);
  return bb_render_model_method(bb_address_encode(draw), 8, NULL, 0);
#else
  ((object_fn)BB_CODE(method(draw, 0x4c)))(draw);
  draw = BB_POINTER(owner + 0x30);
  return ((object_fn)BB_CODE(method(draw, 8)))(draw);
#endif
}

#ifdef BB_MODEL_RUNTIME
void bb_display_primary_model(uint32_t *r) {
  r[7] = primary((void *)(uintptr_t)r[6], r[3] + 4u);
}
uint32_t BB_THISCALL bb_display_primary(void *object) {
  return bb_render_model_service(0x0042ee50, bb_address_encode(object), NULL,
                                 0);
}
void bb_display_initialize_model(uint32_t *r) {
  uint32_t args = r[3] + BB_DISPLAY_SETUP_BYTES + 8u;
  r[7] = initialize((void *)(uintptr_t)r[6], (void *)(uintptr_t)BB_U32(args),
                    BB_U32(args + 4), r[3] + 4u);
}
uint32_t BB_THISCALL bb_display_initialize(void *object, void *window,
                                           uint32_t unused) {
  return bb_render_model_service(
      0x0042ec40, bb_address_encode(object),
      (uint32_t[]){bb_address_encode(window), unused}, 2);
}
#else
uint32_t BB_THISCALL bb_display_primary(void *object) {
  uint32_t scratch[28];
  return primary(object, bb_address_encode(scratch));
}
uint32_t BB_THISCALL bb_display_initialize(void *object, void *window,
                                           uint32_t unused) {
  uint32_t scratch[28];
  return initialize(object, window, unused, bb_address_encode(scratch));
}

#ifdef _WIN32
#define BB_DISPLAY_CONSTRUCT "_bb_display_construct"
#define BB_DISPLAY_MODE_RECORD "_bb_display_mode_record"
#else
#define BB_DISPLAY_CONSTRUCT "bb_display_construct"
#define BB_DISPLAY_MODE_RECORD "bb_display_mode_record"
#endif

void __attribute__((naked)) bb_display_construct_entry(void) {
  __asm__ volatile("pushl %ecx\n\t"
                   "call " BB_DISPLAY_CONSTRUCT "\n\t"
                   "popl %ecx\n\t"
                   "movl $255, %edx\n\t"
                   "ret");
}
/* 0x0042ec30: the original destructor has no work. */
void __attribute__((naked)) bb_display_destroy_entry(void) {
  __asm__ volatile("ret");
}
void __attribute__((naked)) bb_display_mode_callback_entry(void) {
  __asm__ volatile("subl $8, %esp\n\t"
                   "pushl %esp\n\t"
                   "pushl 16(%esp)\n\t"
                   "call " BB_DISPLAY_MODE_RECORD "\n\t"
                   "addl $8, %esp\n\t"
                   "movl %eax, 4(%esp)\n\t"
                   "popl %ecx\n\t"
                   "popl %edx\n\t"
                   "movl $1, %eax\n\t"
                   "ret $8");
}

#endif
