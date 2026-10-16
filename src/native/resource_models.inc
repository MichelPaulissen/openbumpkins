/* SPDX-License-Identifier: GPL-3.0-only */
#include "resource_models.h"
static void resource_discard(struct Resource *resource) {
  pthread_mutex_lock(&resource_mutex);
  struct Resource **link = &resource_owners;
  while (*link && *link != resource)
    link = &(*link)->next;
  if (!*link)
    bb_die("resource descriptor lost its registry owner");
  *link = resource->next;
  pthread_mutex_unlock(&resource_mutex);
  free(resource);
}
static U32 resource_data_reference(struct Resource *resource) {
  uint32_t reference;
  size_t size = resource->size ? resource->size : 1;
  pthread_mutex_lock(&resource_mutex);
  if (bb_address_try_encode(resource->data, &reference)) {
    void *mapped;
    if (!bb_address_try_pointer(reference, size, &mapped) ||
        mapped != resource->data)
      bb_die("resource view exceeds its registered data");
  } else {
    struct ResourceView *view = malloc(sizeof(*view));
    if (!view || !bb_address_bind_auto(0x20000000u, UINT64_C(0xf0000000),
                                       resource->data, size, 16, &reference)) {
      free(view);
      pthread_mutex_unlock(&resource_mutex);
      return 0;
    }
    *view = (struct ResourceView){resource_views, reference};
    resource_views = view;
  }
  pthread_mutex_unlock(&resource_mutex);
  return reference;
}
static U32 resource_publish(struct Resource *resource) {
  if (!resource)
    return 0;
  U32 proxy = bb_heap_zeroed(1, 16);
  if (proxy) {
    resource->data_reference = resource_data_reference(resource);
    if (!resource->data_reference) {
      bb_heap_release(proxy);
      proxy = 0;
    }
  }
  if (!proxy) {
    resource_discard(resource);
    bb_kernel_last_error = 8;
    return 0;
  }
  resource->reference = proxy;
  BB_U32(proxy) = 0x52535243u;
  BB_U32(proxy + 4) = resource->size;
  BB_U64(proxy + 8) = (uintptr_t)resource;
  return proxy;
}
static struct Resource *resource_host(U32 reference) {
  if (!reference)
    return NULL;
  if (BB_U32(reference) != 0x52535243u || !BB_U64(reference + 8))
    bb_die("invalid resource descriptor %08x", reference);
  return (struct Resource *)(uintptr_t)BB_U64(reference + 8);
}
static U32 resource_arg(U32 *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
#define A(n) resource_arg(r, n)
#define H(n) ((Handle)(uintptr_t)A(n))
static void model_find(U32 *r) {
  PeModule *module = bb_pe_module(H(0));
  const char *name = (const char *)(uintptr_t)A(1);
  if (module && module->builtin == BB_MODULE_GAME && A(1) > 0xffff &&
      A(2) <= 0xffff)
    name = BB_DATA(A(1));
  r[7] = resource_publish(
      FindResourceA(H(0), name, (const char *)(uintptr_t)A(2)));
}
static void model_load(U32 *r) {
  struct Resource *resource = resource_host(A(1));
  r[7] = LoadResource(H(0), resource) ? resource->data_reference : 0;
}
static void model_lock(U32 *r) { r[7] = (U32)(uintptr_t)LockResource(H(0)); }
static void model_size(U32 *r) {
  r[7] = SizeofResource(H(0), resource_host(A(1)));
}
static void model_free(U32 *r) { r[7] = FreeResource(H(0)); }
#undef H
#undef A
static const struct {
  const char *name;
  struct bb_model_target target;
} resource_targets[] = {
    {"FindResourceA", {BB_RESOURCE_MODEL_API, model_find, 3, 0, 0}},
    {"LoadResource", {BB_RESOURCE_MODEL_API + 4, model_load, 2, 0, 0}},
    {"LockResource", {BB_RESOURCE_MODEL_API + 8, model_lock, 1, 0, 0}},
    {"SizeofResource", {BB_RESOURCE_MODEL_API + 12, model_size, 2, 0, 0}},
    {"FreeResource", {BB_RESOURCE_MODEL_API + 16, model_free, 1, 0, 0}}};
uint32_t bb_resource_model_export(const char *name) {
  for (unsigned n = 0; n < sizeof(resource_targets) / sizeof(*resource_targets);
       n++)
    if (!strcmp(name, resource_targets[n].name))
      return resource_targets[n].target.identity;
  return 0;
}
const struct bb_model_target *bb_resource_model_find(void *unused,
                                                     uint32_t id) {
  (void)unused;
  for (unsigned n = 0; n < sizeof(resource_targets) / sizeof(*resource_targets);
       n++)
    if (id == resource_targets[n].target.identity)
      return &resource_targets[n].target;
  return NULL;
}
