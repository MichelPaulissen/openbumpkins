/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef BB_MODEL_RUNTIME
#include "qmixer_lifetime.h"
#endif
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* Signed-16-bit multi-tap delay. Its vtable is C data; object fields retain
 * the packed data layout. Allocation and PCM helpers dispatch to C. */
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static uint32_t argument(const uint32_t *input, unsigned index) {
  return BB_U32(stack(input) + 4u + index * 4u);
}
static uint32_t invoke(uint32_t *input, uintptr_t target, uint32_t self,
                       uint32_t args[16]) {
  struct bb_call_state state = {input[7], self, input[5], input[8]};
  long double call_float __attribute__((unused)) = 0;
  BB_QMIX_DISPATCH(&state, target, args, stack(input) - 256u, input[0], input[1],
               input[2], input[4], 0);
  input[7] = state.eax;
  input[6] = state.ecx;
  input[5] = state.edx;
  input[8] = state.flags;
  return state.eax;
}
static uint32_t allocate(uint32_t *input, uint32_t bytes) {
  uint32_t args[16] = {bytes};
  return invoke(input, 0x10001020u, input[6], args);
}
static void release(uint32_t *input, uint32_t pointer) {
  uint32_t args[16] = {pointer};
  invoke(input, 0x10001070u, input[6], args);
}
#ifdef BB_MODEL_RUNTIME
/* This handwritten component has ordinary C locals rather than physical
 * registration records. Catch allocation errors at native C boundaries so
 * each owner releases its successfully constructed parts before rethrowing. */
static int protected_invoke(uint32_t *input, uint32_t target, uint32_t self,
                             uint32_t args[16], uint32_t *value,
                             uint32_t *error) {
  struct bb_call_state state = {input[7], self, input[5], input[8]};
  int success =
      bb_qmix_model_try_invoke(&state, target, args, 16, NULL, error);
  input[7] = state.eax;
  input[6] = state.ecx;
  input[5] = state.edx;
  input[8] = state.flags;
  *value = state.eax;
  return success;
}
static uint32_t allocate_part(uint32_t *input, uint32_t self, uint32_t bytes,
                              int release_taps) {
  uint32_t args[16] = {bytes}, value, error;
  if (protected_invoke(input, 0x10001020u, input[6], args, &value, &error))
    return value;
  if (release_taps) {
    release(input, BB_U32(self + 12u));
    BB_U32(self + 12u) = 0;
  }
  bb_qmix_model_raise(error);
}
#endif
static void clear(uint32_t self) {
  memset(BB_SPAN(BB_U32(self + 16u), BB_U32(self + 8u) * 2u), 0,
         BB_U32(self + 8u) * 2u);
  BB_U32(self + 20u) = 0;
  BB_U32(self + 24u) = 1;
}
static uint32_t construct(uint32_t *input, uint32_t self,
                          uint32_t description) {
  BB_U32(self) = BB_QMIX_VTABLE(bb_qmix_vtable_tap_delay);
  BB_U32(self + 4u) = BB_U32(description + 4u);
#ifdef BB_MODEL_RUNTIME
  BB_U32(self + 12u) = BB_U32(self + 16u) = 0;
  BB_U32(self + 12u) =
      allocate_part(input, self, BB_U32(self + 4u) * 8u, 0);
#else
  BB_U32(self + 12u) = allocate(input, BB_U32(self + 4u) * 8u);
#endif
  BB_U32(self + 8u) = 0;
  if (!BB_U32(self + 12u) && BB_U32(self + 4u))
    abort();
  for (int32_t index = 0; index < BB_I32(self + 4u); ++index) {
    uint32_t offset = (uint32_t)index * 8u;
    float gain = BB_FLOAT(BB_U32(description) + offset + 4u);
    long double scaled = (long double)gain * 32767.0L + 0.5L;
    int64_t integer =
        scaled >= -0x1p63L && scaled < 0x1p63L ? (int64_t)scaled : INT64_MIN;
    BB_U16(BB_U32(self + 12u) + offset + 4u) = (uint16_t)integer;
    uint32_t delay = BB_U32(BB_U32(description) + offset);
    BB_U32(BB_U32(self + 12u) + offset) = delay;
    if (BB_I32(self + 8u) < (int32_t)delay)
      BB_U32(self + 8u) = delay;
  }
#ifdef BB_MODEL_RUNTIME
  BB_U32(self + 16u) =
      allocate_part(input, self, BB_U32(self + 8u) * 2u, 1);
#else
  BB_U32(self + 16u) = allocate(input, BB_U32(self + 8u) * 2u);
#endif
  if (!BB_U32(self + 16u) && BB_U32(self + 8u)) {
    release(input, BB_U32(self + 12u));
    abort();
  }
  clear(self);
  return self;
}
void bb_qmix_tap_delay_factory_c(uint32_t *input) {
  uint32_t description = argument(input, 0);
  uint32_t self = allocate(input, 28);
#ifdef BB_MODEL_RUNTIME
  uint32_t args[16] = {description}, value, error;
  if (!self) {
    input[7] = 0;
    return;
  }
  if (!protected_invoke(input, 0x10011220u, self, args, &value, &error)) {
    release(input, self);
    bb_qmix_model_raise(error);
  }
  input[7] = value;
#else
  input[7] = self ? construct(input, self, description) : 0;
#endif
}
void bb_qmix_tap_delay_constructor_c(uint32_t *input) {
  uint32_t self = input[6], description = argument(input, 0);
  input[7] = construct(input, self, description);
}
static void destroy(uint32_t *input, uint32_t self) {
  BB_U32(self) = BB_QMIX_VTABLE(bb_qmix_vtable_tap_delay);
  release(input, BB_U32(self + 16u));
  release(input, BB_U32(self + 12u));
  uint32_t args[16] = {0};
  invoke(input, 0x1000ec80u, self, args);
}
void bb_qmix_tap_delay_destructor_c(uint32_t *input) {
  destroy(input, input[6]);
}
void bb_qmix_tap_delay_delete_c(uint32_t *input) {
  uint32_t self = input[6], flags = argument(input, 0);
  destroy(input, self);
  if (flags & 1u)
    release(input, self);
  input[7] = self;
}
void bb_qmix_tap_delay_clear_c(uint32_t *input) {
  uint32_t self = input[6];
  clear(self);
  input[7] = 0;
  input[5] = self;
}
void bb_qmix_tap_delay_silent_c(uint32_t *input) {
  uint32_t self = input[6];
  if (BB_U32(self + 24u)) {
    input[7] = 1;
    return;
  }
  uint32_t args[16] = {BB_U32(self + 16u), 10, BB_U32(self + 8u)};
  uint32_t found = invoke(input, 0x1000dd70u, self, args);
  input[7] = BB_U32(self + 24u) = found == UINT32_MAX;
}
static void mix(uint32_t *input, uintptr_t function, uint32_t destination,
                uint32_t source, uint16_t gain, uint32_t count) {
  uint32_t args[16] = {destination, source, gain, 0, count};
  invoke(input, function, input[6], args);
}
void bb_qmix_tap_delay_process_c(uint32_t *input) {
  uint32_t self = input[6], destination = argument(input, 0);
  uint32_t source = argument(input, 1), count = argument(input, 2);
  uint32_t args[16] = {count};
  if ((int32_t)count > BB_I32(source + 12u)) {
    if (!BB_U32(source + 12u)) {
      uint32_t none[16] = {0};
      if (invoke(input, BB_U32(BB_U32(self) + 4u), self, none))
        return;
    }
    invoke(input, 0x1000d690u, source, args);
  }
  uint32_t from = BB_U32(source + 4u), to = BB_U32(destination + 4u);
  uintptr_t mixer;
  if (count != BB_U32(destination + 12u) && !BB_U32(destination + 12u)) {
    memset(BB_SPAN(to + count * 2u, BB_U32(destination + 8u) * 2u), 0,
           BB_U32(destination + 8u) * 2u);
    BB_U32(destination + 12u) = count;
    mixer = 0x1000da80u;
  } else {
    if (count != BB_U32(destination + 12u))
      invoke(input, 0x1000d690u, destination, args);
    mixer = 0x1000db50u;
  }
  for (int32_t index = 0; index < BB_I32(self + 4u); ++index) {
    uint32_t tap = BB_U32(self + 12u) + (uint32_t)index * 8u;
    uint32_t delay = BB_U32(tap);
    uint16_t gain = BB_U16(tap + 4u);
    uint32_t historical = (int32_t)count < (int32_t)delay ? count : delay;
    int32_t position = (int32_t)(BB_U32(self + 20u) - delay);
    if (position < 0)
      position += BB_U32(self + 8u);
    uint32_t available = BB_U32(self + 8u) - (uint32_t)position;
    uint32_t history = BB_U32(self + 16u);
    if ((int32_t)available < (int32_t)historical) {
      mix(input, mixer, to, history + (uint32_t)position * 2u, gain, available);
      mix(input, mixer, to + available * 2u, history, gain,
          historical - available);
    } else
      mix(input, mixer, to, history + (uint32_t)position * 2u, gain,
          historical);
    if ((int32_t)count > (int32_t)historical)
      mix(input, mixer, to + historical * 2u, from, gain, count - historical);
    mixer = 0x1000db50u;
  }
  uint32_t capacity = BB_U32(self + 8u), history = BB_U32(self + 16u);
  uint32_t position = BB_U32(self + 20u);
  if ((int32_t)count >= (int32_t)capacity) {
    memcpy(BB_SPAN(history, capacity * 2u),
           BB_SPAN(from + (count - capacity) * 2u, capacity * 2u), capacity * 2u);
    BB_U32(self + 20u) = 0;
  } else {
    uint32_t available = capacity - position;
    uint32_t first = count <= available ? count : available;
    memcpy(BB_SPAN(history + position * 2u, first * 2u),
           BB_SPAN(from, first * 2u), first * 2u);
    if (count > available)
      memcpy(BB_SPAN(history, (count - available) * 2u),
             BB_SPAN(from + available * 2u, (count - available) * 2u),
             (count - available) * 2u);
    BB_U32(self + 20u) = (position + count) % capacity;
  }
  BB_U32(self + 24u) = 0;
}
void bb_qmix_tap_delay_factory_cleanup_c(uint32_t *input) {
  release(input, BB_U32(input[2] - 16u));
}
void bb_qmix_tap_delay_filter_cleanup_c(uint32_t *input) {
  uint32_t args[16] = {0};
  invoke(input, 0x1000e160u, BB_U32(input[2] - 16u), args);
}
void bb_qmix_tap_delay_base_cleanup_c(uint32_t *input) {
  uint32_t args[16] = {0};
  invoke(input, 0x1000ec80u, BB_U32(input[2] - 16u), args);
}
void bb_qmix_tap_delay_destructor_cleanup_c(uint32_t *input) {
  bb_qmix_tap_delay_base_cleanup_c(input);
}

#ifndef BB_MODEL_RUNTIME
#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature, cleanup)                                        \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal "                   \
                      "2048(%esp),%esp;" cleanup);                             \
  }
ENTRY(bb_qmix_tap_delay_factory, (ARG(0)), "ret $4")
ENTRY(bb_qmix_tap_delay_constructor, (ARG(0)), "ret $4")
ENTRY(bb_qmix_tap_delay_delete, (ARG(0)), "ret $4")
ENTRY(bb_qmix_tap_delay_destructor, (void), "ret")
ENTRY(bb_qmix_tap_delay_process, (ARG(0), ARG(1), ARG(2)), "ret $12")
ENTRY(bb_qmix_tap_delay_silent, (void), "ret")
ENTRY(bb_qmix_tap_delay_clear, (void), "ret")
ENTRY(bb_qmix_tap_delay_factory_cleanup, (void), "ret")
ENTRY(bb_qmix_tap_delay_filter_cleanup, (void), "ret")
ENTRY(bb_qmix_tap_delay_base_cleanup, (void), "ret")
ENTRY(bb_qmix_tap_delay_destructor_cleanup, (void), "ret")
#undef ENTRY
#undef ARG
#endif
