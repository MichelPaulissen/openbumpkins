/* SPDX-License-Identifier: GPL-3.0-only */
/* Windows descriptors/messages retain their packed widths. The window system,
 * message ordering, SDL input and procedure lifetimes stay in shared code. */
#include "window_models.h"
#include "format.h"
static U32 window_arg(U32 *r, unsigned n) { return BB_U32(r[3] + 8u + n * 4u); }
static void *window_data(U32 *r, unsigned n, size_t size) {
  U32 reference = window_arg(r, n);
  return reference ? BB_SPAN(reference, size) : NULL;
}
static const char *window_name(U32 reference) {
  return reference <= 0xffff ? (const char *)(uintptr_t)reference
                             : BB_DATA(reference);
}
#define A(n) window_arg(r, n)
#define P(n, bytes) window_data(r, n, bytes)
#define H(n) ((Handle)(uintptr_t)A(n))
static void model_register(U32 *r) {
  if (!A(0)) {
    r[7] = RegisterClassA(NULL);
    return;
  }
  U32 p = A(0);
  WindowClass descriptor = {
      .style = BB_U32(p),
      .procedure = (WindowProc)(uintptr_t)BB_U32(p + 4),
      .class_extra = BB_I32(p + 8),
      .window_extra = BB_I32(p + 12),
      .instance = (Handle)(uintptr_t)BB_U32(p + 16),
      .icon = (Handle)(uintptr_t)BB_U32(p + 20),
      .cursor = (Handle)(uintptr_t)BB_U32(p + 24),
      .background = (Handle)(uintptr_t)BB_U32(p + 28),
      .menu = (const char *)(uintptr_t)BB_U32(p + 32),
      .class_name = BB_U32(p + 36) ? BB_DATA(BB_U32(p + 36)) : NULL};
  r[7] = RegisterClassA(&descriptor);
}
static void model_unregister(U32 *r) {
  r[7] = UnregisterClassA(window_name(A(0)), H(1));
}
static void model_create(U32 *r) {
  r[7] = (U32)(uintptr_t)CreateWindowExA(
      A(0), window_name(A(1)), bb_window || (!A(6) && !A(7)) ? NULL : P(2, 1),
      A(3), (I32)A(4), (I32)A(5), (I32)A(6), (I32)A(7), H(8), H(9), H(10),
      (void *)(uintptr_t)A(11));
}
static void model_destroy(U32 *r) { r[7] = DestroyWindow(H(0)); }
static void model_default(U32 *r) {
  r[7] = DefWindowProcA(H(0), A(1), A(2), (I32)A(3));
}
static void model_call(U32 *r) {
  r[7] =
      CallWindowProcA((WindowProc)(uintptr_t)A(0), H(1), A(2), A(3), (I32)A(4));
}
static void model_set_long(U32 *r) {
  r[7] = SetWindowLongA(H(0), (I32)A(1), (I32)A(2));
}
static void model_post(U32 *r) {
  r[7] = PostMessageA(H(0), A(1), A(2), (I32)A(3));
}
static void model_quit(U32 *r) {
  PostQuitMessage((I32)A(0));
  (void)r;
}
static void model_peek(U32 *r) {
  r[7] = PeekMessageA(P(0, 28), H(1), A(2), A(3), A(4));
}
static void model_get(U32 *r) {
  r[7] = GetMessageA(P(0, 28), H(1), A(2), A(3));
}
static void model_translate(U32 *r) { r[7] = TranslateMessage(P(0, 28)); }
static void model_dispatch(U32 *r) { r[7] = DispatchMessageA(P(0, 28)); }
static void model_wait(U32 *r) { r[7] = WaitMessage(); }
static void model_begin(U32 *r) {
  r[7] = (U32)(uintptr_t)BeginPaint(H(0), P(1, 64));
}
static void model_end(U32 *r) {
  r[7] = EndPaint(H(0), (void *)(uintptr_t)A(1));
}
static void model_update(U32 *r) { r[7] = UpdateWindow(H(0)); }
static void model_cursor_position(U32 *r) {
  r[7] = SetCursorPos((I32)A(0), (I32)A(1));
}
static void model_metric(U32 *r) { r[7] = GetSystemMetrics((I32)A(0)); }
static void model_icon(U32 *r) {
  r[7] = (U32)(uintptr_t)LoadIconA(H(0), (const char *)(uintptr_t)A(1));
}
static void model_cursor(U32 *r) {
  r[7] = (U32)(uintptr_t)LoadCursorA(H(0), (const char *)(uintptr_t)A(1));
}
static void model_box(U32 *r) {
  r[7] = MessageBoxA(H(0), P(1, 1), P(2, 1), A(3));
}
static void model_active(U32 *r) { r[7] = (U32)(uintptr_t)GetActiveWindow(); }
static void model_process(U32 *r) {
  r[7] = GetWindowThreadProcessId(H(0), P(1, 4));
}
static void model_enumerate(U32 *r) {
  r[7] = EnumWindows((void *)(uintptr_t)A(0), (I32)A(1));
}
static void model_string(U32 *r) {
  r[7] = LoadStringA(H(0), A(1), (I32)A(3) > 0 ? P(2, A(3)) : NULL, (I32)A(3));
}
static void model_format(U32 *r) {
  r[7] = bb_packed_sprintf(P(0, 1), P(1, 1), r[3] + 16u);
}
#undef A
#undef P
#undef H
static const struct {
  const char *name;
  struct bb_model_target target;
} window_targets[] = {
#define T(i, name, body, words)                                                \
  {name, {BB_WINDOW_MODEL_API + i * 4u, body, words, 0, 0}}
    T(0, "RegisterClassA", model_register, 1),
    T(1, "UnregisterClassA", model_unregister, 2),
    T(2, "CreateWindowExA", model_create, 12),
    T(3, "DestroyWindow", model_destroy, 1),
    T(4, "DefWindowProcA", model_default, 4),
    T(5, "CallWindowProcA", model_call, 5),
    T(6, "SetWindowLongA", model_set_long, 3),
    T(7, "PostMessageA", model_post, 4),
    T(8, "PostQuitMessage", model_quit, 1),
    T(9, "PeekMessageA", model_peek, 5),
    T(10, "GetMessageA", model_get, 4),
    T(11, "TranslateMessage", model_translate, 1),
    T(12, "DispatchMessageA", model_dispatch, 1),
    T(13, "WaitMessage", model_wait, 0),
    T(14, "BeginPaint", model_begin, 2),
    T(15, "EndPaint", model_end, 2),
    T(16, "UpdateWindow", model_update, 1),
    T(17, "SetCursorPos", model_cursor_position, 2),
    T(18, "GetSystemMetrics", model_metric, 1),
    T(19, "LoadIconA", model_icon, 2),
    T(20, "LoadCursorA", model_cursor, 2),
    T(21, "MessageBoxA", model_box, 4),
    T(22, "GetActiveWindow", model_active, 0),
    T(23, "GetWindowThreadProcessId", model_process, 2),
    T(24, "EnumWindows", model_enumerate, 2),
    T(25, "LoadStringA", model_string, 4),
    T(26, "wsprintfA", model_format, 2 | BB_MODEL_VARIADIC)
#undef T
};
uint32_t bb_window_model_export(const char *name) {
  for (unsigned n = 0; n < sizeof(window_targets) / sizeof(*window_targets);
       n++)
    if (!strcmp(name, window_targets[n].name))
      return window_targets[n].target.identity;
  return 0;
}
const struct bb_model_target *bb_window_model_find(void *unused, uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof(window_targets) / sizeof(*window_targets);
       n++)
    if (id == window_targets[n].target.identity)
      return &window_targets[n].target;
  return NULL;
}
