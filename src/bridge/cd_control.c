/* SPDX-License-Identifier: GPL-3.0-only */
#include "abi.h"
#include "interop.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "game_calls.h"
#include "session_models.h"
#ifdef BB_MODEL_RUNTIME
/* These immutable command strings are part of the C-owned game data image. */
void bb_cd_play_c(uint32_t object, int32_t track, uint32_t scratch) {
  uint32_t response = scratch + 4u, command = scratch + 1028u;
  uint32_t send = BB_U32(0x6ca594u);
  BB_U8(response) = 0;
  BB_GAME_CALL(send, 0, 0x48c694u, response, 1024, 0);
  BB_GAME_CALL(send, 0, 0x48c6a4u, response, 1024, 0);
  BB_U8(response) = 0;
  snprintf(BB_SPAN(command, 1024), 1024, "play cdaudio from %" PRId32, track);
  BB_GAME_CALL(send, 0, command, response, 1024, 0);
  BB_GAME_CALL(send, 0, 0x48c6dcu, response, 1024, 0);
  BB_I32(object + 0xac34u) = track;
}
uint32_t bb_cd_stop_c(uint32_t object, uint32_t scratch) {
  if (!BB_U32(object + 0xac34u))
    return object;
  uint32_t response = scratch + 4u, command = scratch + 1028u;
  uint32_t send = BB_U32(0x6ca594u);
  BB_U8(response) = 0;
  BB_GAME_CALL(send, 0, 0x48c6ecu, response, 1024, 0);
  memcpy(BB_SPAN(command, 13), BB_SPAN(0x48c6fcu, 13), 13);
  BB_GAME_CALL(send, 0, command, response, 1024, 0);
  return BB_GAME_CALL(send, 0, 0x48c70cu, response, 1024, 0);
}
void bb_cd_play(void *self, int32_t track) {
  BB_GAME_CALL(0x469fdfu, bb_address_encode(self), track);
}
uint32_t bb_cd_stop(void *self) {
  return BB_GAME_CALL(0x46a098u, bb_address_encode(self));
}
uint32_t bb_cd_resume(void *self) {
  return BB_GAME_CALL(0x46a12fu, bb_address_encode(self));
}
#else
typedef uint32_t(BB_STDCALL * mci_send_fn)(const char *, char *,
                                                         uint32_t, void *);
static mci_send_fn mci_send(void) {
  return (mci_send_fn)BB_CODE(BB_POINTER(0x006ca594));
}
/* Reconstructed from 0x00469fdf..0x0046a097. The original ignores MCI errors,
 * remembers the requested track, and resets only the response's first byte. */
void bb_cd_play(void *self, int32_t track) {
  char response[1024], command[1024];
  mci_send_fn send = mci_send();
  response[0] = '\0';
  send("open cdaudio", response, sizeof(response), NULL);
  send("set cdaudio time format tmsf", response, sizeof(response), NULL);
  response[0] = '\0';
  snprintf(command, sizeof(command), "play cdaudio from %" PRId32, track);
  send(command, response, sizeof(response), NULL);
  send("close cdaudio", response, sizeof(response), NULL);
  *(volatile int32_t *)((unsigned char *)self + 0xac34) = track;
}
/* Reconstructed from 0x0046a098..0x0046a12e. Stopping retains the track so
 * resume can restart it. Preserve the last MCI return value at the boundary. */
uint32_t bb_cd_stop(void *self) {
  if (!*(volatile int32_t *)((unsigned char *)self + 0xac34))
    return (uint32_t)(uintptr_t)self;
  char response[1024];
  mci_send_fn send = mci_send();
  response[0] = '\0';
  send("open cdaudio", response, sizeof(response), NULL);
  send("stop cdaudio", response, sizeof(response), NULL);
  return send("close cdaudio", response, sizeof(response), NULL);
}
/* Reconstructed from 0x0046a12f..0x0046a166: restart the remembered track. */
uint32_t bb_cd_resume(void *self) {
  int32_t track = *(volatile int32_t *)((unsigned char *)self + 0xac34);
  if (track)
    bb_cd_play(self, track);
  return track ? (uint32_t)track : (uint32_t)(uintptr_t)self;
}

#ifdef _WIN32
#define BB_ASM(name) "_" #name
#else
#define BB_ASM(name) #name
#endif
/* The original play boundary returns the track in EAX and self in ECX.
 * Callers also rely on the empty stop/resume path preserving ECX and EDX. */
void __attribute__((naked)) bb_cd_play_entry(void) {
  __asm__ volatile("pushl 4(%esp)\n\t"
                   "pushl %ecx\n\t"
                   "call " BB_ASM(bb_cd_play) "\n\t"
                                              "movl (%esp), %ecx\n\t"
                                              "addl $8, %esp\n\t"
                                              "movl 4(%esp), %eax\n\t"
                                              "ret $4");
}
void __attribute__((naked)) bb_cd_stop_entry(void) {
  __asm__ volatile("movl %ecx, %eax\n\t"
                   "cmpl $0, 0xac34(%ecx)\n\t"
                   "jne 1f\n\t"
                   "ret\n"
                   "1: pushl %ecx\n\t"
                   "call " BB_ASM(bb_cd_stop) "\n\t"
                                              "addl $4, %esp\n\t"
                                              "ret");
}
void __attribute__((naked)) bb_cd_resume_entry(void) {
  __asm__ volatile("movl %ecx, %eax\n\t"
                   "cmpl $0, 0xac34(%ecx)\n\t"
                   "jne 1f\n\t"
                   "ret\n"
                   "1: pushl %ecx\n\t"
                   "call " BB_ASM(bb_cd_resume) "\n\t"
                                                "movl (%esp), %ecx\n\t"
                                                "addl $4, %esp\n\t"
                                                "ret");
}

#endif
