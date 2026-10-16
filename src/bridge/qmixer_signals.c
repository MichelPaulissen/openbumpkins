/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif

/* CRT signal routing in C. Legacy addresses still name the temporary DLL
 * data/dispatch ABI; registered dependencies execute their C bodies. */
static uint32_t stack(const uint32_t *input) { return input[3] + 2052u; }
static uint32_t argument(const uint32_t *input) {
  return BB_U32(stack(input) + 4u);
}
static uint32_t call(uint32_t *input, uintptr_t target, uint32_t args[16]) {
  struct bb_call_state state = {input[7], input[6], input[5], input[8]};
  bb_qmix_call(&state, target, args, stack(input) - 256u, input[0], input[1],
               input[2], input[4], 0);
  input[7] = state.eax; input[6] = state.ecx;
  input[5] = state.edx; input[8] = state.flags;
  return input[7];
}
static uint32_t lookup(uint32_t signal) {
  uint32_t row = 0x10020678u, end = row + BB_U32(0x100206f8u) * 12u;
  /* The original inspects the first row even at count zero and the end row
   * after an unsuccessful scan. Preserve this boundary behavior. */
  while (BB_U32(row + 4u) != signal) {
    row += 12u;
    if (row >= end) break;
  }
  return BB_U32(row + 4u) == signal ? row : 0;
}
/* 0x10018080..0x100180b1: first matching exception-to-signal record. */
void bb_qmix_signal_lookup_c(uint32_t *input) {
  input[7] = lookup(argument(input));
}
/* 0x10017ef0..0x10018044; the following 53 bytes are dispatch tables. */
void bb_qmix_signal_raise_c(uint32_t *input) {
  uint32_t signal = argument(input), slot;
  switch (signal) {
  case 2: slot = 0x10020660u; break;
  case 15: slot = 0x1002066cu; break;
  case 21: slot = 0x10020664u; break;
  case 22: slot = 0x10020668u; break;
  case 4: case 8: case 11: slot = lookup(signal) + 8u; break;
  default: input[7] = UINT32_MAX; return;
  }
  uint32_t handler = BB_U32(slot);
  if (handler == 1) { input[7] = 0; return; }
  if (!handler) {
    uint32_t args[16] = {3}; call(input, 0x100148a0u, args);
  }
  uint32_t context = 0, exception = 0;
  unsigned temporary = signal == 4 || signal == 8 || signal == 11;
  if (temporary) {
    context = BB_U32(0x10020700u); BB_U32(0x10020700u) = 0;
    if (signal == 8) {
      exception = BB_U32(0x100206fcu); BB_U32(0x100206fcu) = 0x8cu;
    }
  }
  if (signal == 8) {
    int32_t first = (int32_t)BB_U32(0x100206f0u);
    int32_t count = (int32_t)BB_U32(0x100206f4u);
    /* Signed comparison of the original wrapping endpoint precedes its
     * unsigned iteration count. Valid metadata names the seven FP rows. */
    if (first < (int32_t)((uint32_t)first + (uint32_t)count))
      for (uint32_t n = 0; n < (uint32_t)count; ++n)
        BB_U32(0x10020680u + ((uint32_t)first + n) * 12u) = 0;
  } else BB_U32(slot) = 0;
  uint32_t args[16] = {signal, signal == 8 ? BB_U32(0x100206fcu) : 0};
  call(input, handler, args);
  if (temporary) {
    BB_U32(0x10020700u) = context;
    if (signal == 8) BB_U32(0x100206fcu) = exception;
  }
  input[7] = 0;
}
/* 0x10015da0..0x10015dbf: report abort, dispatch SIGABRT, then exit 3 even
 * if the handler ignores the signal or returns. */
void bb_qmix_signal_abort_c(uint32_t *input) {
  uint32_t args[16] = {10}; call(input, 0x100153d0u, args);
  args[0] = 22; call(input, 0x10017ef0u, args);
  args[0] = 3; call(input, 0x100148a0u, args);
}

#define ARG(n) uint32_t a##n __attribute__((unused))
#define ENTRY(name, signature)                                                 \
  uint32_t __attribute__((naked)) name##_entry signature {                     \
    __asm__ volatile(                                                          \
        "leal -2048(%esp),%esp;pushfl;pushal;pushl %esp;call " BB_ASM(         \
            name##_c) ";leal 4(%esp),%esp;popal;popfl;leal 2048(%esp),%esp;ret"); \
  }
ENTRY(bb_qmix_signal_lookup, (ARG(0)))
ENTRY(bb_qmix_signal_raise, (ARG(0)))
ENTRY(bb_qmix_signal_abort, (void))
#undef ENTRY
#undef ARG
