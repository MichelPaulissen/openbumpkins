/* SPDX-License-Identifier: GPL-3.0-only */
#include "qmixer_lifetime.h"
#include "address.h"
#include "heap.h"
#include "qmixer_cpp_handlers.h"
#include "qmixer_api_models.h"
#include <stdio.h>

static _Thread_local uint32_t exception_head = UINT32_MAX;
struct boundary {
  struct boundary *previous;
  uint32_t head;
  struct bb_qmix_model_error error;
};
static _Thread_local struct boundary *boundary;
static _Thread_local struct bb_qmix_model_error caught_error;
static _Thread_local unsigned catching;
static _Thread_local struct boundary *unwinding_boundary;
static _Thread_local unsigned unwinding;
struct construction_allocation {
  struct construction_allocation *next;
  uint32_t owner, address;
};
static _Thread_local struct construction_allocation *construction_allocations;

/* These operations have incomplete raw-buffer cleanup state maps.
 * Raw buffers allocated before a later allocation fails still belong to the
 * unfinished operation. Keep that ownership in native C storage until it
 * commits, rather than reproducing partial-failure leaks. */
static int construction_owner(uint32_t handler) {
  switch (handler) {
  case 0x10001731u: /* wave loading: descriptor, callback data and format */
  case 0x1000a487u: /* latency probe: headers and temporary PCM */
  case 0x1000e509u: /* float biquad */
  case 0x1000e8abu: /* float convolution */
  case 0x1000f2fbu: /* integer recursive */
  case 0x1000f71cu: /* integer biquad */
  case 0x1000fa7cu: /* integer convolution */
  case 0x10010311u: /* short recursive */
  case 0x1001098du: /* short biquad */
  case 0x10010ef5u: /* short convolution */
  case 0x1000ec3fu: /* float delay */
  case 0x1000fdb0u: /* integer delay */
    return 1;
  default:
    return 0;
  }
}
static void forget_allocation(uint32_t address) {
  struct construction_allocation **slot = &construction_allocations;
  while (*slot) {
    struct construction_allocation *node = *slot;
    if (node->address == address) {
      *slot = node->next;
      free(node);
      return;
    }
    slot = &node->next;
  }
}
static void finish_construction(uint32_t owner, int failed) {
  struct construction_allocation **slot = &construction_allocations;
  while (*slot) {
    struct construction_allocation *node = *slot;
    if (node->owner != owner) {
      slot = &node->next;
      continue;
    }
    *slot = node->next;
    if (failed && !bb_heap_release(node->address))
      abort();
    free(node);
  }
}

uint32_t bb_qmix_model_exception_head(void) { return exception_head; }
void bb_qmix_model_exception_link(uint32_t head) {
  if (exception_head != UINT32_MAX && exception_head != head &&
      BB_U32(exception_head) == head)
    finish_construction(exception_head, 0);
  exception_head = head;
}
static _Noreturn void bad_unwind(const char *message) {
  fprintf(stderr, "bumpkins: mixer C cleanup: %s\n", message);
  abort();
}
static uint32_t info_for(uint32_t handler) {
  switch (handler) {
#define INFO(address, name, info)                                              \
  case address:                                                                \
    return info;
    BB_QMIX_CPP_HANDLERS(INFO)
#undef INFO
  default:
    bad_unwind("unregistered owner");
  }
}

static int converted_catches(uint32_t handler) {
  switch (handler) {
  case 0x1000a487u:
  case 0x1000a608u:
  case 0x1000aad0u:
    return 1;
#define API_OWNER(address, name, frame, high, error, memory) case address:
    BB_QMIX_API_SCOPES(API_OWNER)
#undef API_OWNER
    return 1;
  default:
    return 0;
  }
}
static void cleanup_stages(uint32_t record, uint32_t info, int32_t stop) {
  uint32_t states = BB_U32(info + 4), rows = BB_U32(info + 8);
  int32_t stage = BB_I32(record + 8);
  unsigned steps = 0;
  while (stage != stop) {
    if (stage < 0 || (uint32_t)stage >= states || ++steps > states)
      bad_unwind("invalid cleanup stage");
    uint32_t row = rows + (uint32_t)stage * 8u;
    int32_t parent = BB_I32(row);
    uint32_t cleanup = BB_U32(row + 4);
    BB_I32(record + 8) = parent;
    if (cleanup) {
      struct bb_call_state call = {0, 0, 0, 0x202};
      struct bb_model_saved owner = {0, 0, record + 12, 0};
      bb_model_invoke_frame(&call, cleanup, NULL, 0, &owner, NULL);
      if (exception_head != record)
        bad_unwind("cleanup did not restore its registration");
    }
    stage = parent;
  }
}

void bb_qmix_model_cleanup_to(uint32_t owner, int32_t stage) {
  if (owner != exception_head || !owner || owner == UINT32_MAX)
    bad_unwind("cleanup outside its owner");
  uint32_t info = info_for(BB_U32(owner + 4));
  if (BB_U32(info) != 0x19930520u ||
      (BB_U32(info + 12) && !converted_catches(BB_U32(owner + 4))))
    bad_unwind("owner needs a catch-clause adapter");
  struct boundary *previous = unwinding_boundary;
  unwinding_boundary = boundary;
  ++unwinding;
  cleanup_stages(owner, info, stage);
  --unwinding;
  unwinding_boundary = previous;
}

/* Translated owners retain C-owned FuncInfo identities and stage words. Walk
 * their cleanup state maps with an explicit logical owner EBP. This preserves
 * partial construction/destruction without a Win32 FS register or CONTEXT.
 * Native wrappers select catches for explicitly converted owners. Their maps
 * also remain usable if recovery itself fails and an outer scope cleans up.
 */
static void unwind_to(uint32_t stop) {
  unsigned frames = 0;
  while (exception_head != stop) {
    uint32_t record = exception_head;
    if (!record || record == UINT32_MAX || ++frames > 65536)
      bad_unwind("invalid registration chain");
    uint32_t previous = BB_U32(record), info = info_for(BB_U32(record + 4));
    if (BB_U32(info) != 0x19930520u ||
        (BB_U32(info + 12) && !converted_catches(BB_U32(record + 4))))
      bad_unwind("owner needs a catch-clause adapter");
    int32_t stage = BB_I32(record + 8);
    uint32_t handler = BB_U32(record + 4);
    if (stage >= 0 &&
        (handler == 0x10001ef4u || handler == 0x1000ae4au)) {
      /* These old maps only destroy the base. The portable constructors
       * initialize absent resources so their real destructors can release
       * a partially opened codec or partially acquired device ramps too. */
      unsigned codec = handler == 0x10001ef4u;
      struct bb_call_state call = {0, BB_U32(record - (codec ? 8u : 4u)),
                                   0, 0x202};
      bb_model_invoke(&call, codec ? 0x10001f30u : 0x1000aec0u, NULL, 0);
      if (exception_head != record)
        bad_unwind("constructor cleanup did not restore its registration");
    }
    cleanup_stages(record, info, -1);
    finish_construction(record, 1);
    exception_head = previous;
  }
}
int bb_qmix_model_try_invoke_detailed(struct bb_call_state *state,
                                     uint32_t identity,
                                     const uint32_t *arguments, size_t words,
                                     struct bb_model_result *result,
                                     struct bb_qmix_model_error *error) {
  struct boundary scope = {boundary, exception_head, {0, 0}};
  boundary = &scope;
  uint32_t information = 0;
  int success = bb_model_try_invoke(state, identity, arguments, words, result,
                                   &information);
  boundary = scope.previous;
  if (exception_head != scope.head)
    bad_unwind("call left an unbalanced registration");
  scope.error.information = information;
  if (error)
    *error = scope.error;
  return success;
}
int bb_qmix_model_try_invoke(struct bb_call_state *state, uint32_t identity,
                             const uint32_t *arguments, size_t words,
                             struct bb_model_result *result, uint32_t *error) {
  struct bb_qmix_model_error detail;
  int success = bb_qmix_model_try_invoke_detailed(state, identity, arguments,
                                                 words, result, &detail);
  if (error)
    *error = detail.information;
  return success;
}

int bb_qmix_model_try_body(void (*body)(uint32_t *), uint32_t *input,
                           uint32_t owner, struct bb_qmix_model_error *error) {
  uint32_t previous = exception_head;
  struct boundary scope = {boundary, owner, {0, 0}};
  boundary = &scope;
  uint32_t information = 0;
  int success = bb_model_try_body(body, input, &information);
  boundary = scope.previous;
  if (exception_head != (success ? previous : owner) ||
      (!success && BB_U32(owner) != previous))
    bad_unwind("worker left an unbalanced registration");
  scope.error.information = information;
  if (error)
    *error = scope.error;
  return success;
}

void bb_qmix_model_catch_body(void (*body)(uint32_t *), uint32_t *input,
                             struct bb_qmix_model_error error) {
  struct bb_qmix_model_error previous = caught_error;
  struct boundary scope = {boundary, exception_head, {0, 0}};
  caught_error = error;
  ++catching;
  boundary = &scope;
  uint32_t information = 0;
  int success = bb_model_try_body(body, input, &information);
  boundary = scope.previous;
  --catching;
  caught_error = previous;
  if (exception_head != scope.head)
    bad_unwind("catch left an unbalanced registration");
  if (!success) {
    scope.error.information = information;
    bb_qmix_model_raise_error(scope.error);
  }
}

_Noreturn void bb_qmix_model_rethrow(void) {
  bb_qmix_model_raise_error(bb_qmix_model_caught_error());
}

struct bb_qmix_model_error bb_qmix_model_caught_error(void) {
  if (!catching)
    bad_unwind("error value outside a catch");
  return caught_error;
}

void bb_qmix_model_malloc_c(uint32_t *input) {
  uint32_t size = BB_U32(input[3] + 2056u);
  input[7] = bb_heap_allocate(size);
  if (input[7] && exception_head != UINT32_MAX &&
      construction_owner(BB_U32(exception_head + 4u))) {
    struct construction_allocation *node = malloc(sizeof *node);
    if (!node) {
      if (!bb_heap_release(input[7]))
        abort();
      input[7] = 0;
    } else {
      *node = (struct construction_allocation){
          construction_allocations, exception_head, input[7]};
      construction_allocations = node;
    }
  }
}
void bb_qmix_model_free_c(uint32_t *input) {
  uint32_t address = BB_U32(input[3] + 2056u);
  if (!bb_heap_release(address))
    bad_unwind("invalid allocation release");
  forget_allocation(address);
}
void bb_qmix_model_raise_c(uint32_t *input) {
  uint32_t information = BB_U32(input[3] + 2060u);
  uint32_t object = BB_U32(input[3] + 2056u);
  if (!information && !object)
    bb_qmix_model_rethrow();
  struct bb_qmix_model_error error = {
      information, information == 0x1001ae90u && object ? BB_U32(object) : 0};
  bb_qmix_model_raise_error(error);
}

_Noreturn void bb_qmix_model_raise(uint32_t information) {
  bb_qmix_model_raise_error((struct bb_qmix_model_error){information, 0});
}
_Noreturn void bb_qmix_model_raise_error(struct bb_qmix_model_error error) {
  /* An exception escaping a cleanup is terminal. A new protected boundary
   * inside that cleanup can still handle its own allocation failure. */
  if (unwinding && unwinding_boundary == boundary)
    bad_unwind("exception escaped a cleanup");
  struct boundary *previous = unwinding_boundary;
  unwinding_boundary = boundary;
  ++unwinding;
  unwind_to(boundary ? boundary->head : UINT32_MAX);
  --unwinding;
  unwinding_boundary = previous;
  if (boundary)
    boundary->error = error;
  bb_model_raise(error.information);
}
