/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "crt_errors.h"
#include "crt_signals.h"
#include "crt_bootstrap.h"
#include "crt_output.h"
#include "crt_primitives.h"
#include "game_calls.h"
#include <stddef.h>
#define BYTE(a) BB_U8(a)
#define STR(a) ((const char *)(uintptr_t)(a))
#define WIN BB_STDCALL
void bb_crt_errors_digits_entry(uint32_t value, char *out, uint32_t radix,
                                int32_t negative) {
  if (negative) {
    BB_U8(out) = '-';
    ++out;
    value = 0u - value;
  }
  uintptr_t cursor = (uintptr_t)out, first = cursor, last;
  do {
    last = cursor;
    uint32_t digit = value % radix;
    value /= radix;
    BYTE(cursor++) = digit < 10 ? digit + '0' : digit + 'W';
  } while (value);
  BYTE(cursor) = 0;
  do {
    uint8_t c = BYTE(last);
    BYTE(last) = BYTE(first);
    BYTE(first) = c;
    --last;
    ++first;
  } while (first < last);
}
char *bb_crt_errors_itoa_entry(int32_t value, char *out, uint32_t radix) {
  bb_crt_errors_digits_entry(value, out, radix, radix == 10 && value < 0);
  return out;
}
#include "crt_error_services.inc"
static int32_t report(int32_t kind, const char *file, int32_t line,
                      const char *module, const char *format,
                      const void *arguments, int32_t result) {
  /* Preserve the original adjacent buffers and callback-result word. */
  struct {
    char combined[4096], message[4096];
    int32_t result;
    const void *arguments;
    char detail[4096];
  } frame = {.result = result, .arguments = arguments};
  if (kind < 0 || kind > 2)
    return -1;
  if (kind == 2 && counter(0x6ca3bc) > 0) {
    if (!BB_U32(0x48ccfc)) {
      void *user = load_user();
      if (!user)
        return -1;
      BB_SET_POINTER(0x48ccfc, resolve(user, 0x482da8));
      if (!BB_U32(0x48ccfc))
        return -1;
    }
    second_chance(frame.message, file, line);
    debug_output(frame.message);
    counter(0x6ca3c0);
    bb_crt_errors_break_entry();
    return -1;
  }
  if (format &&
      bb_crt_vsnprintf_entry(frame.detail, 0xfed, format, frame.arguments) < 0)
    bb_crt_string_copy_entry(frame.detail, STR(0x482d48));
  if (kind == 2)
    bb_crt_string_copy_entry(frame.combined, STR(format ? 0x482d34 : 0x482d20));
  bb_crt_string_append_entry(frame.combined, frame.detail);
  if (kind == 2) {
    if (BB_U32(0x48ccd0 + (uint32_t)kind * 4) & 1)
      bb_crt_string_append_entry(frame.combined, STR(0x482d1c));
    bb_crt_string_append_entry(frame.combined, STR(0x482d18));
  }
  if (file) {
    if (format_message(frame.message, 4096, STR(0x482d0c),
                         (uintptr_t[]){(uintptr_t)file, (uint32_t)line,
                                       (uintptr_t)frame.combined}, 3, 5) < 0)
      bb_crt_string_copy_entry(frame.message, STR(0x482d48));
  } else
    bb_crt_string_copy_entry(frame.message, frame.combined);
  if (BB_U32(0x6c9bb4) && report_callback(kind, frame.message, &frame.result)) {
    if (kind == 2)
      counter(0x6ca3c0);
    return frame.result;
  }
  if ((BB_U32(0x48ccd0 + (uint32_t)kind * 4) & 1) &&
      BB_U32(0x48cce0 + (uint32_t)kind * 4) != UINT32_MAX) {
    uint32_t size = bb_crt_string_length_entry(frame.message);
    write_text(BB_U32(0x48cce0 + (uint32_t)kind * 4), frame.message, size);
  }
  if (BB_U32(0x48ccd0 + (uint32_t)kind * 4) & 2)
    debug_output(frame.message);
  if (BB_U32(0x48ccd0 + (uint32_t)kind * 4) & 4) {
    char number[20];
    const char *line_text =
        line ? bb_crt_errors_itoa_entry(line, number, 10) : NULL;
    frame.result = bb_crt_errors_window_entry(kind, file, line_text,
                                              (char *)module, frame.detail);
    if (kind == 2)
      counter(0x6ca3c0);
    return frame.result;
  }
  if (kind == 2)
    counter(0x6ca3c0);
  return 0;
}
int32_t bb_crt_errors_window_entry(int32_t kind, const char *file,
                                   const char *line, char *module,
                                   const char *user_message) {
  if (!user_message && REPORT_SIMPLE(2, STR(0x482ef8), 474, NULL,
                                                  STR(0x482f04)) == 1)
    bb_crt_errors_break_entry();
  char filename_buffer[260], message[4096];
  if (!filename(filename_buffer, 260))
    bb_crt_string_copy_entry(filename_buffer, STR(0x482ee0));
  char *program = filename_buffer;
  if (bb_crt_string_length_entry(program) > 64) {
    program += bb_crt_string_length_entry(program) - 64;
    bb_crt_string_copy_count_entry(program, STR(0x482edc), 3);
  }
  if (module && bb_crt_string_length_entry(module) > 64) {
    module += bb_crt_string_length_entry(module) - 64;
    bb_crt_string_copy_count_entry(module, STR(0x482edc), 3);
  }
  const char *empty = STR(0x48481c);
  if (format_message(message, 4096, STR(0x482de4), (uintptr_t[]){
          (uintptr_t)BB_POINTER(0x48ccf0 + (uint32_t)kind * 4), (uintptr_t)program,
          (uintptr_t)(module ? STR(0x482e38) : empty), (uintptr_t)(module ? module : empty),
          (uintptr_t)(file ? STR(0x482e44) : empty), (uintptr_t)(file ? file : empty),
          (uintptr_t)(line ? STR(0x482e4c) : empty), (uintptr_t)(line ? line : empty),
          (uintptr_t)(BYTE(user_message) ? STR(0x482e54) : empty),
          (uintptr_t)(BYTE(user_message) && kind == 2 ? STR(0x482e58) : empty),
          (uintptr_t)(BYTE(user_message) ? user_message : empty),
          (uintptr_t)(kind == 2 ? STR(0x482e68) : empty)}, 12, 0xfff) < 0)
    bb_crt_string_copy_entry(message, STR(0x482d48));
  int32_t choice =
      bb_crt_errors_message_box_entry(message, STR(0x482dc0), 0x12012);
  if (choice == 3) {
    bb_crt_signal_raise_entry(22);
    bb_crt_boot_quick_exit_entry(3);
  }
  return choice == 4;
}
void bb_crt_errors_message_entry(int32_t code) {
  uint32_t index = 0;
  while (index < 18 && BB_I32(0x48d638 + index * 8) != code)
    ++index;
  if (BB_I32(0x48d638 + index * 8) != code)
    return;
  if (code != 252 &&
      REPORT_SIMPLE(1, NULL, 0, NULL,
                                 BB_POINTER(0x48d63c + index * 8)) == 1) {
#ifdef BB_MODEL_RUNTIME
    bb_crt_errors_break_entry();
#else
    __asm__ volatile("int3");
#endif
  }
  if (BB_U32(0x48cc5c) == 1 || (!BB_U32(0x48cc5c) && BB_U32(0x48cc60) == 1)) {
    uint32_t handle;
    if (!BB_U32(0x6c8aa0) || BB_U32(BB_U32(0x6c8aa0) + 16) == UINT32_MAX)
      handle = stderr_handle();
    else
      handle = BB_U32(BB_U32(0x6c8aa0) + 16);
    uint32_t size =
        bb_crt_string_length_entry(BB_POINTER(0x48d63c + index * 8));
    write_text(handle, BB_POINTER(0x48d63c + index * 8), size);
  } else if (code != 252) {
    struct {
      char message[160], filename[260];
    } frame;
    if (!filename(frame.filename, 260))
      bb_crt_string_copy_entry(frame.filename, STR(0x482ee0));
    char *program = frame.filename;
    if (bb_crt_string_length_entry(program) + 1 > 60) {
      program += bb_crt_string_length_entry(frame.filename) - 59;
      bb_crt_string_copy_count_entry(program, STR(0x482edc), 3);
    }
    bb_crt_string_copy_entry(frame.message, STR(0x4832dc));
    bb_crt_string_append_entry(frame.message, program);
    bb_crt_string_append_entry(frame.message, STR(0x482e54));
    bb_crt_string_append_entry(frame.message, BB_POINTER(0x48d63c + index * 8));
    bb_crt_errors_message_box_entry(frame.message, STR(0x4832b4), 0x12010);
  }
}
void bb_crt_errors_banner_entry(void) {
  if (BB_U32(0x48cc5c) == 1 || (!BB_U32(0x48cc5c) && BB_U32(0x48cc60) == 1)) {
    bb_crt_errors_message_entry(252);
    if (BB_U32(0x48d6c8)) {
#ifdef BB_MODEL_RUNTIME
      BB_GAME_CALL(BB_U32(0x48d6c8), 0);
#else
      ((void (*)(void))BB_CODE(BB_POINTER(0x48d6c8)))();
#endif
    }
    bb_crt_errors_message_entry(255);
  }
}
/* Preserve the unspecified callback result slot without an uninitialized C
 * read. The original report's result lies 4108 bytes below its entry ESP. */
void __attribute__((used, noinline)) bb_crt_errors_report_c(uint32_t *r) {
  uintptr_t a = r[3] + 16392;
  r[7] = report(BB_I32(a), BB_POINTER(a + 4), BB_I32(a + 8), BB_POINTER(a + 12),
                BB_POINTER(a + 16), (void *)(a + 20), BB_I32(r[3] + 12280));
}

#ifdef BB_MODEL_RUNTIME
void bb_crt_errors_stack_probe_c(uint32_t *r) {
  /* C owns the actual allocation. Validate the translated caller's logical
   * capacity rather than changing a native stack pointer or its return slot. */
  struct bb_model_scratch scope;
  bb_model_scratch_begin(&scope, r[7] ? r[7] : 1u);
  bb_model_scratch_end(&scope);
  r[7] = BB_U32(r[3] + 132u);
}
#endif
#ifndef BB_MODEL_RUNTIME
void __attribute__((used, noinline)) bb_crt_errors_stack_probe_c(uint32_t *r) {
  uint32_t size = r[7];
  uintptr_t p = r[3] + 136;
  while (size >= 4096) {
    p -= 4096;
    size -= 4096;
    (void)BB_U32(p);
  }
  p -= size;
  (void)BB_U32(p);
  r[7] = BB_U32(r[3] + 132);
  r[3] = p;
}
#ifdef _WIN32
#define ASM_NAME(n) "_" #n
#else
#define ASM_NAME(n) #n
#endif
#define UNUSED __attribute__((unused))
int32_t __attribute__((naked, noinline))
bb_crt_errors_report_entry(int32_t a UNUSED, const char *b UNUSED,
                           int32_t c UNUSED, const char *d UNUSED,
                           const char *e UNUSED, ...) {
  __asm__ volatile(
      "lea -16384(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " ASM_NAME(
          bb_crt_errors_report_c) ";addl $4,%esp;popal;popfl;lea "
                                  "16384(%esp),%esp;ret");
}
void __attribute__((naked, noinline)) bb_crt_errors_stack_probe_entry(void) {
  __asm__ volatile(
      "lea -128(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " ASM_NAME(
          bb_crt_errors_stack_probe_c) ";addl $4,%esp;popal;popfl;movl "
                                       "-24(%esp),%esp;pushl %eax;ret");
}

#endif
