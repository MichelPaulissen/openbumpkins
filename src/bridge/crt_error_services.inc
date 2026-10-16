/* SPDX-License-Identifier: GPL-3.0-only */
/* Synchronous error-report services. Private C buffers borrow logical stack
 * spans; packed callers retain their original references and aliasing. */
#ifdef BB_MODEL_RUNTIME
#include "model_buffer.h"
static void text_buffer(struct bb_model_buffer *b, const char *text) {
  uint32_t reference;
  bb_model_buffer_begin(b, text, bb_address_try_encode(text, &reference)
                                     ? 0 : strlen(text) + 1);
}
#endif
void bb_crt_errors_break_entry(void) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(BB_U32(0x6ca44c), 0);
#else
  ((void(WIN *)(void))BB_CODE(BB_POINTER(0x6ca44c)))();
#endif
}
static void *load_user(void) {
#ifdef BB_MODEL_RUNTIME
  return (void *)(uintptr_t)BB_GAME_CALL(BB_U32(0x6ca4a8), 0, 0x482db4);
#else
  return ((void *(WIN *)(const char *))BB_CODE(BB_POINTER(0x6ca4a8)))(STR(0x482db4));
#endif
}
static void *resolve(void *module, uint32_t name) {
#ifdef BB_MODEL_RUNTIME
  return (void *)(uintptr_t)BB_GAME_CALL(BB_U32(0x6ca4ac), 0,
                                         bb_address_encode(module), name);
#else
  return ((void *(WIN *)(void *, const char *))BB_CODE(BB_POINTER(0x6ca4ac)))(module,
                                                                          STR(name));
#endif
}
int32_t bb_crt_errors_message_box_entry(const char *text, const char *caption,
                                        uint32_t flags) {
  void *window = NULL;
  if (!BB_U32(0x48e054)) {
    void *module = load_user();
    if (!module)
      return 0;
    BB_SET_POINTER(0x48e054, resolve(module, 0x483474));
    if (!BB_U32(0x48e054))
      return 0;
    BB_SET_POINTER(0x48e058, resolve(module, 0x483464));
    BB_SET_POINTER(0x48e05c, resolve(module, 0x483450));
  }
#ifdef BB_MODEL_RUNTIME
  uint32_t w = 0;
  if (BB_U32(0x48e058))
    w = BB_GAME_CALL(BB_U32(0x48e058), 0);
  if (w && BB_U32(0x48e05c))
    w = BB_GAME_CALL(BB_U32(0x48e05c), 0, w);
  struct bb_model_buffer message, title;
  text_buffer(&message, text);
  text_buffer(&title, caption);
  int32_t result = BB_GAME_CALL(BB_U32(0x48e054), 0, w, message.reference,
                                 title.reference, flags);
  bb_model_buffer_end(&title, 0);
  bb_model_buffer_end(&message, 0);
  (void)window;
  return result;
#else
  if (BB_U32(0x48e058))
    window = ((void *(WIN *)(void))BB_CODE(BB_POINTER(0x48e058)))();
  if (window && BB_U32(0x48e05c))
    window = ((void *(WIN *)(void *))BB_CODE(BB_POINTER(0x48e05c)))(window);
  return ((int32_t(WIN *)(void *, const char *, const char *,
                          uint32_t))BB_CODE(BB_POINTER(0x48e054)))(window, text, caption,
                                                                  flags);
#endif
}
static void debug_output(const char *text) {
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer message;
  text_buffer(&message, text);
  BB_GAME_CALL(BB_U32(0x6ca374), 0, message.reference);
  bb_model_buffer_end(&message, 0);
#else
  ((void(WIN *)(const char *))BB_CODE(BB_POINTER(0x6ca374)))(text);
#endif
}
static int32_t counter(uintptr_t api) {
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(BB_U32(api), 0, 0x48ccc8);
#else
  return ((int32_t(WIN *)(int32_t *))BB_CODE(BB_POINTER(api)))((void *)0x48ccc8);
#endif
}
static void write_text(uint32_t handle, const char *text, uint32_t size) {
  uint32_t written = 0;
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer message, count;
  bb_model_buffer_begin(&message, text, size);
  bb_model_buffer_begin(&count, &written, 4);
  BB_GAME_CALL(BB_U32(0x6ca4b4), 0, handle, message.reference, size,
                count.reference, 0);
  bb_model_buffer_end(&count, 0);
  bb_model_buffer_end(&message, 0);
#else
  ((int32_t(WIN *)(uint32_t, const void *, uint32_t, uint32_t *,
                   void *))BB_CODE(BB_POINTER(0x6ca4b4)))(handle, text, size, &written,
                                                         NULL);
#endif
}
static uint32_t filename(char *out, uint32_t capacity) {
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer buffer;
  bb_model_buffer_begin(&buffer, out, capacity);
  uint32_t result = BB_GAME_CALL(BB_U32(0x6ca440), 0, 0, buffer.reference, capacity);
  bb_model_buffer_end(&buffer, 1);
  return result;
#else
  return ((uint32_t(WIN *)(void *, char *, uint32_t))BB_CODE(BB_POINTER(0x6ca440)))(
      NULL, out, capacity);
#endif
}
static uint32_t stderr_handle(void) {
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(BB_U32(0x6ca410), 0, (uint32_t)-12);
#else
  return ((uint32_t(WIN *)(int32_t))BB_CODE(BB_POINTER(0x6ca410)))(-12);
#endif
}
static int32_t report_callback(int32_t kind, char *message, int32_t *result) {
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer text, value;
  bb_model_buffer_begin(&text, message, 4096);
  bb_model_buffer_begin(&value, result, 4);
  int32_t handled = BB_GAME_CALL(BB_U32(0x6c9bb4), 0, kind, text.reference,
                                  value.reference);
  bb_model_buffer_end(&value, 1);
  bb_model_buffer_end(&text, 1);
  return handled;
#else
  return ((int32_t (*)(int32_t, char *, int32_t *))BB_CODE(BB_POINTER(0x6c9bb4)))(
      kind, message, result);
#endif
}
static int32_t format_message(char *out, uint32_t capacity, const char *format,
                               const uintptr_t *values, size_t count,
                               uint32_t strings) {
  uint32_t words[16] = {0};
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer buffers[16];
#endif
  for (size_t n = 0; n < count; n++) {
#ifdef BB_MODEL_RUNTIME
    if (strings & (1u << n)) {
      text_buffer(&buffers[n], (const char *)values[n]);
      words[n] = buffers[n].reference;
    } else
#endif
      words[n] = values[n];
  }
  int32_t result = bb_crt_output_words(out, capacity, format, words, count);
#ifdef BB_MODEL_RUNTIME
  for (size_t n = count; n-- > 0;)
    if (strings & (1u << n))
      bb_model_buffer_end(&buffers[n], 0);
#else
  (void)strings;
#endif
  return result;
}
static void second_chance(char *out, const char *file, int32_t line) {
#ifdef BB_MODEL_RUNTIME
  struct bb_model_buffer buffer, source;
  bb_model_buffer_begin(&buffer, out, 4096);
  text_buffer(&source, file);
  BB_GAME_CALL(BB_U32(0x48ccfc), 0, buffer.reference, 0x482d74,
                source.reference, line);
  bb_model_buffer_end(&source, 0);
  bb_model_buffer_end(&buffer, 1);
#else
  ((int32_t (*)(char *, const char *, ...))BB_CODE(BB_POINTER(0x48ccfc)))(
      out, STR(0x482d74), file, line);
#endif
}
#ifdef BB_MODEL_RUNTIME
#define REPORT_SIMPLE(kind, file, line, module, format)                        \
  BB_GAME_CALL(0x470ef0, 0, kind, bb_address_encode(file), line,                \
                bb_address_encode(module), bb_address_encode(format))
#else
#define REPORT_SIMPLE bb_crt_errors_report_entry
#endif
