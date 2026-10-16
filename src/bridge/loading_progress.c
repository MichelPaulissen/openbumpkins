/* SPDX-License-Identifier: GPL-3.0-only */
#include "interop.h"
#include "game_calls.h"
#include "ui_layout.h"

/* Original asset-loading callers expect ECX/EDX to survive the disabled path.
 * The boundary thunk keeps that behavior, which a C void function cannot
 * promise.
 */
#ifndef BB_MODEL_RUNTIME
void __attribute__((naked)) bb_loading_progress_entry(void) {
  __asm__ volatile("cmpl $0, 0x004843f4\n\t"
                   "jne 1f\n\t"
                   "xorl %eax, %eax\n\t"
                   "ret\n"
#ifdef _WIN32
                   "1: jmp _bb_loading_progress"
#else
                   "1: jmp bb_loading_progress"
#endif
  );
}

#else
void bb_loading_progress_entry(void) { bb_loading_progress(); }
#endif

/* Reconstructed from BEASTS.EXE 0x00406a60..0x00406b22.
 * Original sprite drawing and presentation remain available through the bridge.
 * Keep the order of writes/calls and the x87 floating-point evaluation order.
 */
void bb_loading_progress(void) {
  if (!BB_I32(0x004843f4))
    return;

#ifdef BB_MODEL_RUNTIME
#define draw(p,x,y,f,n) BB_GAME_CALL(0x42fd90u, bb_address_encode(p), x, y, f, n)
#define present(p) BB_GAME_CALL(0x42f240u, bb_address_encode(p))
#else
  bb_draw_sprite_fn draw = (bb_draw_sprite_fn)BB_CODE((uintptr_t)0x0042fd90);
  bb_present_fn present = (bb_present_fn)BB_CODE((uintptr_t)0x0042f240);
#endif
  int32_t dx = bb_ui_canvas_x(), dy = bb_ui_canvas_y();
  draw(BB_POINTER(0x0060461c), dx, dy, 255, 1);

  /* Unsigned increment reproduces the original 32-bit wrapping instruction. */
  BB_U32(0x004843f8) += 1;
#ifdef BB_MODEL_RUNTIME
  volatile double progress = (double)BB_I32(0x004843f8);
  progress = progress * BB_DOUBLE(0x00482130);
  progress = progress * BB_DOUBLE(0x00482138);
#else
  long double progress = (long double)BB_I32(0x004843f8);
  progress *= (long double)BB_DOUBLE(0x00482130);
  progress *= (long double)BB_DOUBLE(0x00482138);
#endif
  int segments = (int)progress;
  if (segments > 91)
    segments = 91;
  if (segments <= BB_I32(0x004843fc))
    return;

  BB_I32(0x004843fc) = segments;
  draw(BB_POINTER(0x006634fc), 134 + dx, 396 + dy, 0, 1);
  int x = 136 + dx;
  for (int i = 0; i < segments; i++, x += 4)
    draw(BB_POINTER(0x006634fc), x, 396 + dy, 1, 1);
  draw(BB_POINTER(0x006634fc), x, 396 + dy, 2, 1);
  present((void *)(uintptr_t)0x004eebc8);
}

#ifdef BB_MODEL_RUNTIME
#undef draw
#undef present
#endif
