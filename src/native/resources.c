/* SPDX-License-Identifier: GPL-3.0-only */
#include "kernel_handle.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#ifdef BB_MODEL_RUNTIME
#include "../bridge/address.h"
#include "../bridge/heap.h"
#endif
struct Resource {
  void *data;
  U32 size;
  struct Resource *next;
#ifdef BB_MODEL_RUNTIME
  U32 reference, data_reference;
#endif
};
static struct Resource *resource_owners;
static pthread_mutex_t resource_mutex = PTHREAD_MUTEX_INITIALIZER;
#ifdef BB_MODEL_RUNTIME
struct ResourceView {
  struct ResourceView *next;
  U32 reference;
};
static struct ResourceView *resource_views;
#endif
static Handle W FindResourceA(Handle module, const char *name,
                              const char *type) {
  struct Resource value = {0};
  value.data = bb_pe_resource(module, name, type, &value.size);
  if (!value.data)
    return NULL;
  struct Resource *result = malloc(sizeof(*result));
  if (!result) {
    bb_kernel_last_error = 8;
    return NULL;
  }
  *result = value;
  pthread_mutex_lock(&resource_mutex);
  result->next = resource_owners;
  resource_owners = result;
  pthread_mutex_unlock(&resource_mutex);
  return result;
}
static Handle W LoadResource(Handle module, Handle resource) {
  return resource ? ((struct Resource *)resource)->data : NULL;
}
static void *W LockResource(Handle resource) { return resource; }
static U32 W SizeofResource(Handle module, Handle resource) {
  return resource ? ((struct Resource *)resource)->size : 0;
}
static int W FreeResource(Handle resource) { return 0; }
/* Resource descriptors and their borrowed views live until their modules close.
 * Call only after callbacks and resource consumers have stopped. */
void bb_resources_reset(void) {
  pthread_mutex_lock(&resource_mutex);
  while (resource_owners) {
    struct Resource *r = resource_owners;
    resource_owners = r->next;
#ifdef BB_MODEL_RUNTIME
    if (r->reference && !bb_heap_release(r->reference))
      bb_die("resource descriptor lost its owner");
#endif
    free(r);
  }
#ifdef BB_MODEL_RUNTIME
  while (resource_views) {
    struct ResourceView *v = resource_views;
    resource_views = v->next;
    if (!bb_address_unbind(v->reference))
      bb_die("resource view lost its mapping");
    free(v);
  }
#endif
  pthread_mutex_unlock(&resource_mutex);
}
void *bb_resource_api(const char *name) {
  static const struct ApiEntry table[] = {
      API_ENTRY(FindResourceA), API_ENTRY(LoadResource),
      API_ENTRY(LockResource), API_ENTRY(SizeofResource),
      API_ENTRY(FreeResource)};
  API_RETURN(table);
}
#ifdef BB_MODEL_RUNTIME
#include "resource_models.inc"
#endif
