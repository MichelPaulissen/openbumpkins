/* SPDX-License-Identifier: GPL-3.0-only */
#include "process_models.h"
static uint32_t process_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
#define A(n) process_arg(r, n)
#define P(n) ((void *)(uintptr_t)A(n))
static void model_exit(uint32_t *r) { ExitProcess(A(0)); }
static void model_terminate(uint32_t *r) {
  r[7] = TerminateProcess(P(0), A(1));
}
static void model_process(uint32_t *r) {
  r[7] = (uintptr_t)GetCurrentProcess();
}
static void model_thread(uint32_t *r) { r[7] = (uintptr_t)GetCurrentThread(); }
static void model_process_id(uint32_t *r) { r[7] = GetCurrentProcessId(); }
static void model_thread_id(uint32_t *r) { r[7] = GetCurrentThreadId(); }
static void model_priority(uint32_t *r) {
  r[7] = SetThreadPriority(P(0), (int32_t)A(1));
}
static void model_raise(uint32_t *r) { RaiseException(A(0), A(1), A(2), P(3)); }
static void model_unwind(uint32_t *r) { RtlUnwind(P(0), P(1), P(2), P(3)); }
static void model_debug(uint32_t *r) {
  (void)r;
  DebugBreak();
}
static void model_create(uint32_t *r) {
  uint32_t id = 0, output = A(5);
  r[7] = (uintptr_t)CreateThread(P(0), A(1), (void *)P(2), P(3), A(4),
                                 output ? &id : NULL);
  if (r[7] && output)
    BB_U32(output) = id;
}
#undef P
#undef A
static const struct {
  const char *name;
  struct bb_model_target target;
} process_targets[] = {
#define T(i, name, body, words)                                                \
  {#name, {BB_PROCESS_MODEL_API + i * 4u, body, words, 0, 0}}
    T(0, ExitProcess, model_exit, 1),
    T(1, TerminateProcess, model_terminate, 2),
    T(2, GetCurrentProcess, model_process, 0),
    T(3, GetCurrentThread, model_thread, 0),
    T(4, GetCurrentProcessId, model_process_id, 0),
    T(5, GetCurrentThreadId, model_thread_id, 0),
    T(6, SetThreadPriority, model_priority, 2),
    T(7, RaiseException, model_raise, 4),
    T(8, RtlUnwind, model_unwind, 4),
    T(9, DebugBreak, model_debug, 0),
    T(10, CreateThread, model_create, 6)
#undef T
};
uint32_t bb_process_model_export(const char *name) {
  for (unsigned n = 0; n < sizeof process_targets / sizeof *process_targets;
       n++)
    if (!strcmp(name, process_targets[n].name))
      return process_targets[n].target.identity;
  return 0;
}
const struct bb_model_target *bb_process_model_find(void *context,
                                                    uint32_t id) {
  (void)context;
  for (unsigned n = 0; n < sizeof process_targets / sizeof *process_targets;
       n++)
    if (process_targets[n].target.identity == id)
      return &process_targets[n].target;
  return NULL;
}
