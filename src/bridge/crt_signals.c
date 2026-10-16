/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "crt_signals.h"
#include "crt_bootstrap.h"
#include "crt_errors.h"
#include "game_calls.h"
#include <stddef.h>
#define WIN BB_STDCALL
static void handle_one(uint32_t id, int32_t signal) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(id, 0, signal);
#else
  ((void (*)(int32_t))BB_CODE(id))(signal);
#endif
}
static void handle_two(uint32_t id, int32_t signal, uint32_t code) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(id, 0, signal, code);
#else
  ((void (*)(int32_t, uint32_t))BB_CODE(id))(signal, code);
#endif
}
static int32_t filter_call(uint32_t id, const void *pointers) {
#ifdef BB_MODEL_RUNTIME
  return BB_GAME_CALL(id, 0, bb_address_encode(pointers));
#else
  return ((int32_t(WIN *)(const void *))BB_CODE(id))(pointers);
#endif
}
static void *lookup(uint32_t value, unsigned field) {
  uintptr_t row = 0x48d380;
  do {
    if (BB_U32(row + field) == value)
      break;
    row += 12;
  } while (row < 0x48d380 + BB_U32(0x48d400) * 12);
  return BB_U32(row + field) == value ? (void *)row : NULL;
}
void *bb_crt_signal_exception_lookup_entry(uint32_t code) {
  return lookup(code, 0);
}
void *bb_crt_signal_lookup_entry(int32_t signal) { return lookup(signal, 4); }
static void clear_floating(void) {
  uint32_t index = BB_U32(0x48d3f8);
  while ((int32_t)index < (int32_t)(BB_U32(0x48d3f8) + BB_U32(0x48d3fc))) {
    BB_U32(0x48d388 + index * 12) = 0;
    ++index;
  }
}
int32_t bb_crt_signal_raise_entry(int32_t signal) {
  uintptr_t slot;
  switch (signal) {
  case 2:
    slot = 0x48e040;
    break;
  case 15:
    slot = 0x48e04c;
    break;
  case 21:
    slot = 0x48e044;
    break;
  case 22:
    slot = 0x48e048;
    break;
  case 4:
  case 8:
  case 11:
    slot = (uintptr_t)bb_crt_signal_lookup_entry(signal) + 8;
    break;
  default:
    return -1;
  }
  uintptr_t handler = BB_U32(slot);
  if (handler == 1)
    return 0;
  if (!handler)
    bb_crt_boot_quick_exit_entry(3);
  unsigned temporary = signal == 4 || signal == 8 || signal == 11;
  uint32_t context = 0, floating = 0;
  if (temporary) {
    context = BB_U32(0x48d408);
    BB_U32(0x48d408) = 0;
    if (signal == 8) {
      floating = BB_U32(0x48d404);
      BB_U32(0x48d404) = 140;
    }
  }
  if (signal == 8)
    clear_floating();
  else
    BB_U32(slot) = 0;
  if (signal == 8)
    handle_two(handler, 8, BB_U32(0x48d404));
  else
    handle_one(handler, signal);
  if (temporary) {
    BB_U32(0x48d408) = context;
    if (signal == 8)
      BB_U32(0x48d404) = floating;
  }
  return 0;
}
int32_t bb_crt_signal_exception_entry(uint32_t code, const void *pointers) {
  uintptr_t row = (uintptr_t)bb_crt_signal_exception_lookup_entry(code);
  if (!row || !BB_U32(row + 8))
    return filter_call(BB_U32(0x6ca494), pointers);
  if (BB_U32(row + 8) == 5) {
    BB_U32(row + 8) = 0;
    return 1;
  }
  if (BB_U32(row + 8) == 1)
    return -1;
  uintptr_t handler = BB_U32(row + 8);
  uint32_t context = BB_U32(0x48d408);
  BB_U32(0x48d408) = bb_address_encode(pointers);
  if (BB_U32(row + 4) == 8) {
    clear_floating();
    uint32_t floating = BB_U32(0x48d404);
    switch (BB_U32(row)) {
    case 0xc000008e:
      BB_U32(0x48d404) = 131;
      break;
    case 0xc0000090:
      BB_U32(0x48d404) = 129;
      break;
    case 0xc0000091:
      BB_U32(0x48d404) = 132;
      break;
    case 0xc0000093:
      BB_U32(0x48d404) = 133;
      break;
    case 0xc000008d:
      BB_U32(0x48d404) = 130;
      break;
    case 0xc000008f:
      BB_U32(0x48d404) = 134;
      break;
    case 0xc0000092:
      BB_U32(0x48d404) = 138;
      break;
    }
    handle_two(handler, 8, BB_U32(0x48d404));
    BB_U32(0x48d404) = floating;
  } else {
    BB_U32(row + 8) = 0;
    handle_one(handler, BB_U32(row + 4));
  }
  BB_U32(0x48d408) = context;
  return -1;
}
void bb_crt_signal_float_trap_entry(void) { bb_crt_boot_fatal_entry(2); }
int32_t bb_crt_signal_math_error_entry(void *exception
                                       __attribute__((unused))) {
  return 0;
}
void bb_crt_signal_initialize_entry(void) { bb_crt_signal_install_entry(); }
void bb_crt_signal_install_entry(void) {
#ifdef BB_MODEL_RUNTIME
  bb_crt_boot_atexit_entry((void *)(uintptr_t)0x47e8e0);
  BB_U32(0x6c8a8c) = BB_GAME_CALL(BB_U32(0x6ca3e8), 0, 0x47e840);
#else
  bb_crt_boot_atexit_entry((void *)(uintptr_t)bb_crt_signal_restore_entry);
  BB_SET_POINTER(0x6c8a8c, ((void *(WIN *)(void *))BB_CODE(BB_POINTER(0x6ca3e8)))((void *)(uintptr_t)bb_crt_signal_unhandled_entry));
#endif
}
int32_t bb_crt_signal_validate_read_entry(const void *p, uint32_t count) {
#ifdef BB_MODEL_RUNTIME
  uint32_t reference;
  if (!bb_address_try_encode(p, &reference))
    return 1; /* A private native C buffer is valid host storage. */
  return !BB_GAME_CALL(BB_U32(0x6ca458), 0, reference, count);
#else
  return !((int32_t(WIN *)(const void *, uint32_t))BB_CODE(BB_POINTER(0x6ca458)))(p, count);
#endif
}
int32_t bb_crt_signal_validate_write_entry(void *p, uint32_t count) {
#ifdef BB_MODEL_RUNTIME
  uint32_t reference;
  if (!bb_address_try_encode(p, &reference))
    return 1;
  return !BB_GAME_CALL(BB_U32(0x6ca450), 0, reference, count);
#else
  return !((int32_t(WIN *)(void *, uint32_t))BB_CODE(BB_POINTER(0x6ca450)))(p, count);
#endif
}
int32_t bb_crt_signal_validate_code_entry(const void *p) {
#ifdef BB_MODEL_RUNTIME
  uint32_t reference;
  return bb_address_try_encode(p, &reference) &&
         !BB_GAME_CALL(BB_U32(0x6ca3e4), 0, reference);
#else
  return !((int32_t(WIN *)(const void *))BB_CODE(BB_POINTER(0x6ca3e4)))(p);
#endif
}
int32_t WIN bb_crt_signal_unhandled_entry(const void *pointers) {
  uintptr_t record = BB_U32(pointers);
  if (BB_U32(record) == 0xe06d7363 && BB_U32(record + 16) == 3 &&
      BB_U32(record + 20) == 0x19930520) {
    bb_crt_signal_terminate_entry();
    return 1;
  }
  if (BB_U32(0x6c8a8c) &&
      bb_crt_signal_validate_code_entry(BB_POINTER(0x6c8a8c)))
    return filter_call(BB_U32(0x6c8a8c), pointers);
  return 0;
}
void bb_crt_signal_restore_entry(void) {
#ifdef BB_MODEL_RUNTIME
  BB_GAME_CALL(BB_U32(0x6ca3e8), 0, BB_U32(0x6c8a8c));
#else
  ((void *(WIN *)(void *))BB_CODE(BB_POINTER(0x6ca3e8)))(BB_POINTER(0x6c8a8c));
#endif
}
void bb_crt_signal_abort_entry(void) {
  bb_crt_errors_message_entry(10);
  bb_crt_signal_raise_entry(22);
  bb_crt_boot_quick_exit_entry(3);
}

#ifndef BB_MODEL_RUNTIME
/* The original exception registration and its scope table are still consumed
 * by the retained Windows exception dispatcher. Normal dispatch is C; the
 * adapters keep its frame/landing convention for binary callbacks. */
void __attribute__((used, noinline))
bb_crt_signal_terminate_c(uintptr_t frame) {
  BB_I32(frame - 4) = 0;
  if (BB_U32(0x48e034)) {
    BB_I32(frame - 4) = 1;
    ((void (*)(void))BB_CODE(BB_POINTER(0x48e034)))();
    BB_I32(frame - 4) = 0;
  }
  BB_I32(frame - 4) = -1;
  bb_crt_signal_abort_entry();
}
void __attribute__((used, noinline))
bb_crt_signal_inconsistent_c(uintptr_t frame) {
  BB_I32(frame - 4) = 0;
  if (BB_U32(0x48e03c)) {
    BB_I32(frame - 4) = 1;
    ((void (*)(void))BB_CODE(BB_POINTER(0x48e03c)))();
    BB_I32(frame - 4) = 0;
  }
  BB_I32(frame - 4) = -1;
  bb_crt_signal_terminate_entry();
}
void __attribute__((used, noinline))
bb_crt_signal_terminate_catch_c(uintptr_t frame) {
  BB_I32(frame - 4) = -1;
  bb_crt_signal_abort_entry();
}
void __attribute__((used, noinline))
bb_crt_signal_inconsistent_catch_c(uintptr_t frame) {
  BB_I32(frame - 4) = -1;
  bb_crt_signal_terminate_entry();
}
#endif
int32_t bb_crt_signal_terminate_filter_entry(void) { return 1; }
int32_t bb_crt_signal_inconsistent_filter_entry(void) { return 1; }
void bb_crt_signal_abort_cleanup_entry(void) { bb_crt_signal_abort_entry(); }
void bb_crt_signal_terminate_cleanup_entry(void) {
  bb_crt_signal_terminate_entry();
}
#ifdef BB_MODEL_RUNTIME
static void terminal_callback(uint32_t slot) {
  if (BB_U32(slot)) {
    struct bb_call_state state = {0, 0, 0, 0x202};
    uint32_t error;
    /* A throwing terminate handler falls through to the terminal action. */
    (void)bb_model_try_invoke(&state, BB_U32(slot), NULL, 0, NULL, &error);
  }
}
void bb_crt_signal_terminate_entry(void) {
  terminal_callback(0x48e034);
  bb_crt_signal_abort_entry();
}
void bb_crt_signal_inconsistent_entry(void) {
  terminal_callback(0x48e03c);
  bb_crt_signal_terminate_entry();
}
void bb_crt_signal_terminate_catch_entry(void) { bb_crt_signal_abort_entry(); }
void bb_crt_signal_inconsistent_catch_entry(void) { bb_crt_signal_terminate_entry(); }
#else
#ifdef _WIN32
#define ASM_NAME(n) "_" #n
#else
#define ASM_NAME(n) #n
#endif
#define RESTORE_FRAME                                                          \
  "movl -16(%ebp),%eax;movl %eax,%fs:0;popl %edi;popl %esi;popl "              \
  "%ebx;leave;ret"
#define GUARDED(name, table)                                                   \
  void __attribute__((naked, noinline)) name##_entry(void) {                   \
    __asm__ volatile(                                                          \
        "pushl %ebp;movl %esp,%ebp;pushl $-1;pushl $" #table                   \
        ";pushl $0x476478;movl %fs:0,%eax;pushl %eax;movl %esp,%fs:0;subl "    \
        "$8,%esp;pushl %ebx;pushl %esi;pushl %edi;movl %esp,-24(%ebp);leal "   \
        "-128(%esp),%esp;pushl %ebp;call " ASM_NAME(                           \
            name##_c) ";movl -24(%ebp),%esp;" RESTORE_FRAME);                  \
  }
#define LANDING(name)                                                          \
  void __attribute__((naked, noinline)) name##_entry(void) {                   \
    __asm__ volatile("movl -24(%ebp),%esp;movl $0,-4(%ebp);leal "              \
                     "-128(%esp),%esp;pushl %ebp;call " ASM_NAME(              \
                         name##_c) ";movl -24(%ebp),%esp;" RESTORE_FRAME);     \
  }
GUARDED(bb_crt_signal_terminate, 0x483408)
GUARDED(bb_crt_signal_inconsistent, 0x483420)
LANDING(bb_crt_signal_terminate_catch)
LANDING(bb_crt_signal_inconsistent_catch)

#endif
