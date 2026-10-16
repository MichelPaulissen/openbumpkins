/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_internal.h"
#include <signal.h>
#ifdef BB_MODEL_RUNTIME
#include "qmixer_lifetime.h"
#endif
#ifdef _WIN32
#define BB_ASM_INNER(name) "_" #name
#else
#define BB_ASM_INNER(name) #name
#endif
#define BB_ASM(name) BB_ASM_INNER(name)
#define BB_NUMBER_INNER(value) #value
#define BB_NUMBER(value) BB_NUMBER_INNER(value)

/* Recovery actions use an explicit logical owner in portable C. The working
 * i386 bridge retains its final stack/FS boundary. Calls use scratch below the
 * suspended frame, keeping cleanup state available during nested exceptions. */
static uint32_t device_call(uint32_t *r, uintptr_t target, uint32_t self,
                            uint32_t first, uint32_t second) {
  uint32_t args[16] = {first, second};
  struct bb_call_state state = {r[7], self, r[5], r[8]};
  long double call_float __attribute__((unused)) = 0;
  BB_QMIX_DISPATCH(&state, target, args, r[3] + 2052u - 256u, r[0], r[1], r[2],
               r[4], 0);
  r[7] = state.eax;
  r[6] = state.ecx;
  r[5] = state.edx;
  return state.eax;
}
static void restore_chain(uint32_t *r, unsigned result_is_chain) {
  uint32_t previous = BB_U32(r[2] - 12u);
  if (result_is_chain)
    r[7] = previous;
  else
    r[6] = previous;
#ifdef BB_MODEL_RUNTIME
  bb_qmix_model_exception_link(previous);
#else
  __asm__ volatile("movl %0,%%fs:0" ::"r"(previous) : "memory");
#endif
}
#ifndef BB_MODEL_RUNTIME
#define CALL_WORKER(name)                                                      \
  "lea -2048(%esp),%esp;pushfl;pushal;cld;pushl %esp;call " BB_ASM(             \
      name##_c) ";addl $4,%esp;popal;popfl;lea 2048(%esp),%esp;"
#endif
#define CATCH_BODY(address, name, error, continuation)                         \
  void name##_c(uint32_t *r) {                                                 \
    if (error) {                                                              \
      device_call(r, 0x10005040u, r[6], error, 0);                             \
      device_call(r, 0x100116c0u, r[6], 0, 0);                                \
    } else {                                                                  \
      device_call(r, BB_U32(0x100212e8u), r[6], 0x1001e2f0u, 0);              \
      BB_U32(r[2] - 28u) = 75;                                               \
    }                                                                         \
    r[7] = continuation;                                                      \
  }
BB_QMIX_DEVICE_CATCHES(CATCH_BODY)
#undef CATCH_BODY
#ifndef BB_MODEL_RUNTIME
#define CATCH_ENTRY(address, name, error, continuation)                        \
  uint32_t __attribute__((naked)) name##_entry(void) {                          \
    __asm__ volatile(CALL_WORKER(name) "ret");                                \
  }
BB_QMIX_DEVICE_CATCHES(CATCH_ENTRY)
#undef CATCH_ENTRY
#endif

void bb_qmix_waveout_latency_resume_c(uint32_t *r) {
  uint32_t frame = r[2], wave = frame - 56u;
  BB_U32(frame - 4u) = 0;
  device_call(r, 0x10004310u, wave, 0, 0);
  uint32_t headers = BB_U32(frame - 20u);
  for (unsigned n = 0; n < 250; ++n)
    device_call(r, 0x100042d0u, wave, headers + 32u * n, 0);
  device_call(r, 0x10001070u, r[6], BB_U32(frame - 20u), 0);
  device_call(r, 0x10001070u, r[6], BB_U32(frame - 36u), 0);
  BB_U32(frame - 4u) = UINT32_MAX;
  int32_t product = BB_U32(frame - 32u) * BB_U32(frame - 28u);
  uint32_t block_align = BB_U16(BB_U32(frame - 40u));
  if (!block_align) {
    raise(SIGFPE);
    __builtin_unreachable();
  }
  BB_U32(frame - 44u) = product / (int32_t)block_align;
  device_call(r, 0x100041f0u, wave, 0, 0);
  r[7] = BB_U32(frame - 44u);
  restore_chain(r, 0);
}
void bb_qmix_waveout_open_resume_c(uint32_t *r) {
  uint32_t frame = r[2], self = BB_U32(frame - 20u);
  BB_U32(frame - 4u) = UINT32_MAX;
  device_call(r, BB_U32(BB_U32(self) + 8u), self, 0, 0);
  restore_chain(r, 1);
}
void bb_qmix_waveout_close_resume_c(uint32_t *r) {
  BB_U32(r[2] - 4u) = UINT32_MAX;
  restore_chain(r, 1);
}
void bb_qmix_waveout_pump_resume_c(uint32_t *r) {
  uint32_t frame = r[2];
  BB_U32(frame - 4u) = UINT32_MAX;
  if (BB_U32(BB_U32(frame - 24u))) {
    device_call(r, BB_U32(0x100212c4u), r[6], frame - 168u, 0);
    BB_U32(BB_U32(frame - 20u) + 232u) +=
        BB_U32(frame - 168u) - BB_U32(frame - 160u);
  }
  restore_chain(r, 1);
}
#ifndef BB_MODEL_RUNTIME
#define RETURN(address, name, cleanup)                                         \
  uint32_t __attribute__((naked)) name##_entry(void) {                         \
    __asm__ volatile(CALL_WORKER(name)                                         \
                     "popl %edi;popl %esi;popl %ebx;leave;ret $"               \
                     BB_NUMBER(cleanup));                                      \
  }
BB_QMIX_DEVICE_RETURNS(RETURN)
#undef RETURN
#undef CALL_WORKER

#endif
