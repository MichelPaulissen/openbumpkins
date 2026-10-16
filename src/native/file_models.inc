/* SPDX-License-Identifier: GPL-3.0-only */
/* ABI translation only; file, mapping, calendar and path behavior stays shared.
 */
#include "file_models.h"
static pthread_mutex_t handles_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct KernelHandle *model_handles;

uint32_t bb_kernel_handle_export(Handle object) {
  if ((uintptr_t)object <= 3 || object == (void *)UINT32_MAX)
    return (uint32_t)(uintptr_t)object;
  struct KernelHandle *handle = object;
  pthread_mutex_lock(&handles_mutex);
  uint32_t reference = handle->reference;
  if (!reference) {
    reference = bb_heap_zeroed(1, 16);
    if (reference) {
      BB_U32(reference) = 0x4b480000u | handle->type;
      BB_U32(reference + 4u) = handle->type;
      BB_U64(reference + 8u) = (uintptr_t)handle;
      handle->reference = reference;
      handle->model_next = model_handles;
      model_handles = handle;
    }
  }
  pthread_mutex_unlock(&handles_mutex);
  return reference;
}
Handle bb_kernel_handle_acquire(uint32_t reference) {
  if (reference <= 3 || reference == UINT32_MAX)
    return (void *)(uintptr_t)reference;
  pthread_mutex_lock(&handles_mutex);
  struct KernelHandle *handle = model_handles;
  while (handle && handle->reference != reference)
    handle = handle->model_next;
  if (handle)
    atomic_fetch_add(&handle->references, 1);
  pthread_mutex_unlock(&handles_mutex);
  if (!handle)
    bb_kernel_last_error = 6;
  return handle;
}
void bb_kernel_handle_unborrow(Handle object) {
  if ((uintptr_t)object > 3 && object != (void *)UINT32_MAX)
    bb_kernel_handle_release(object);
}
int bb_kernel_handle_close(uint32_t reference) {
  if (!reference || reference == UINT32_MAX)
    return bb_kernel_error(6);
  if (reference <= 3)
    return 1;
  pthread_mutex_lock(&handles_mutex);
  struct KernelHandle **link = &model_handles;
  while (*link && (*link)->reference != reference)
    link = &(*link)->model_next;
  struct KernelHandle *handle = *link;
  if (handle) {
    *link = handle->model_next;
    handle->reference = 0;
  }
  pthread_mutex_unlock(&handles_mutex);
  if (!handle)
    return bb_kernel_error(6);
  if (!bb_heap_release(reference))
    bb_die("native kernel handle lost its owner");
  bb_kernel_handle_release(handle);
  return 1;
}
static uint32_t owned_handle(Handle handle, uint32_t failure) {
  if (!handle || handle == (void *)UINT32_MAX)
    return (uint32_t)(uintptr_t)handle;
  uint32_t result = bb_kernel_handle_export(handle);
  if (!result) {
    CloseHandle(handle);
    bb_kernel_last_error = 8;
    return failure;
  }
  return result;
}
static uint32_t file_arg(uint32_t *r, unsigned n) {
  return BB_U32(r[3] + 8u + n * 4u);
}
static void *file_data(uint32_t *r, unsigned n, size_t size) {
  uint32_t a = file_arg(r, n);
  return a ? BB_SPAN(a, size) : NULL;
}
#define A(n) file_arg(r, n)
#define P(n, bytes) file_data(r, n, bytes)
#define BORROW(n) Handle handle = bb_kernel_handle_acquire(A(n))
#define DONE() bb_kernel_handle_unborrow(handle)
static void model_create(uint32_t *r) {
  r[7] = owned_handle(CreateFileA(P(0, 1), A(1), A(2), (void *)(uintptr_t)A(3),
                                  A(4), A(5), (void *)(uintptr_t)A(6)),
                      UINT32_MAX);
}
static void model_close(uint32_t *r) { r[7] = bb_kernel_handle_close(A(0)); }
static void model_read(uint32_t *r) {
  BORROW(0);
  r[7] = ReadFile(handle, P(1, A(2)), A(2), P(3, 4), (void *)(uintptr_t)A(4));
  DONE();
}
static void model_write(uint32_t *r) {
  BORROW(0);
  r[7] = WriteFile(handle, P(1, A(2)), A(2), P(3, 4), (void *)(uintptr_t)A(4));
  DONE();
}
static void model_seek(uint32_t *r) {
  BORROW(0);
  r[7] = SetFilePointer(handle, (I32)A(1), P(2, 4), A(3));
  DONE();
}
static void model_end(uint32_t *r) {
  BORROW(0);
  r[7] = SetEndOfFile(handle);
  DONE();
}
static void model_flush(uint32_t *r) {
  BORROW(0);
  r[7] = FlushFileBuffers(handle);
  DONE();
}
static void model_lopen(uint32_t *r) { r[7] = _lopen(P(0, 1), (int)A(1)); }
static void model_lread(uint32_t *r) {
  r[7] = _lread((int)A(0), P(1, A(2)), A(2));
}
static void model_lclose(uint32_t *r) { r[7] = _lclose((int)A(0)); }
static void model_type(uint32_t *r) {
  BORROW(0);
  r[7] = GetFileType(handle);
  DONE();
}
/* Standard-handle slots borrow opaque words. Preserve even closed/stale values
 * without dereferencing them, as the native API does for caller-owned handles.
 */
static void model_std_get(uint32_t *r) {
  r[7] = bb_address_encode(GetStdHandle((I32)A(0)));
}
static void model_std_set(uint32_t *r) {
  r[7] = SetStdHandle(A(0), (void *)(uintptr_t)A(1));
}
static void model_handle_count(uint32_t *r) { r[7] = SetHandleCount(A(0)); }
static void model_error_get(uint32_t *r) { r[7] = GetLastError(); }
static void model_error_set(uint32_t *r) { SetLastError(A(0)); }
static void model_system_time(uint32_t *r) { GetSystemTime(P(0, 16)); }
static void model_local_time(uint32_t *r) { GetLocalTime(P(0, 16)); }
static void model_zone(uint32_t *r) {
  r[7] = GetTimeZoneInformation(P(0, 172));
}
static void model_to_file(uint32_t *r) {
  r[7] = SystemTimeToFileTime(P(0, 16), P(1, 8));
}
static void model_from_file(uint32_t *r) {
  r[7] = FileTimeToSystemTime(P(0, 8), P(1, 16));
}
static void model_file_time(uint32_t *r) {
  BORROW(0);
  r[7] = GetFileTime(handle, P(1, 8), P(2, 8), P(3, 8));
  DONE();
}
static void model_drive(uint32_t *r) { r[7] = GetDriveTypeA(NULL); }
static void model_drive_names(uint32_t *r) {
  r[7] = GetLogicalDriveStringsA(A(0), P(1, A(0)));
}
static void model_disk(uint32_t *r) {
  r[7] = GetDiskFreeSpaceA(NULL, P(1, 4), P(2, 4), P(3, 4), P(4, 4));
}
static void model_volume(uint32_t *r) {
  r[7] = GetVolumeInformationA(NULL, P(1, A(2)), A(2), P(3, 4), P(4, 4),
                               P(5, 4), P(6, A(7)), A(7));
}
static void model_mapping(uint32_t *r) {
  BORROW(0);
  r[7] = owned_handle(CreateFileMappingA(handle, (void *)(uintptr_t)A(1), A(2),
                                         A(3), A(4), NULL),
                      0);
  DONE();
}
static void model_map(uint32_t *r) {
  BORROW(0);
  void *address = MapViewOfFile(handle, A(1), A(2), A(3), A(4));
  DONE();
  if (!address) {
    r[7] = 0;
    return;
  }
  pthread_mutex_lock(&mapping_mutex);
  struct MappingView *view = mapping_views;
  while (view && view->address != address)
    view = view->next;
  if (!view)
    bb_die("native file mapping disappeared during creation");
  uint32_t reference;
#ifdef BB_PORTABLE_RUNTIME
  int ok = bb_address_bind_auto(0x20000000u, UINT64_C(0xf0000000), address,
                                view->length, 16, &reference);
#else
  int ok = 1;
  reference = bb_address_encode(address);
#endif
  if (ok)
    view->reference = reference;
  pthread_mutex_unlock(&mapping_mutex);
  if (!ok) {
    UnmapViewOfFile(address);
    bb_kernel_last_error = 8;
    reference = 0;
  }
  r[7] = reference;
}
static void model_unmap(uint32_t *r) {
  pthread_mutex_lock(&mapping_mutex);
  struct MappingView **link = &mapping_views;
  while (*link && (*link)->reference != A(0))
    link = &(*link)->next;
  r[7] = unmap_view_locked(link);
  pthread_mutex_unlock(&mapping_mutex);
}
static void model_search(uint32_t *r) {
  char *part = NULL;
  r[7] = SearchPathA(P(0, 1), P(1, 1), P(2, 1), A(3), P(4, A(3)),
                     A(5) ? &part : NULL);
  if (A(5) && r[7] && r[7] < A(3))
    BB_U32(A(5)) = bb_address_encode(part);
}
static void model_open(uint32_t *r) {
  r[7] = OpenFile(P(0, 1), (void *)(uintptr_t)A(1), A(2));
}
#undef BORROW
#undef DONE
#undef A
#undef P

static const struct {
  const char *name;
  struct bb_model_target target;
} file_targets[] = {
#define API(n, name, fn, argc)                                                 \
  {#name, {BB_FILE_MODEL_API + (n) * 4u, model_##fn, argc, 0, 0}}
    API(0, CreateFileA, create, 7),
    API(1, CloseHandle, close, 1),
    API(2, ReadFile, read, 5),
    API(3, WriteFile, write, 5),
    API(4, SetFilePointer, seek, 4),
    API(5, SetEndOfFile, end, 1),
    API(6, FlushFileBuffers, flush, 1),
    API(7, _lopen, lopen, 2),
    API(8, _lread, lread, 3),
    API(9, _lclose, lclose, 1),
    API(10, GetFileType, type, 1),
    API(11, GetStdHandle, std_get, 1),
    API(12, SetStdHandle, std_set, 2),
    API(13, SetHandleCount, handle_count, 1),
    API(14, GetLastError, error_get, 0),
    API(15, SetLastError, error_set, 1),
    API(16, GetSystemTime, system_time, 1),
    API(17, GetLocalTime, local_time, 1),
    API(18, GetTimeZoneInformation, zone, 1),
    API(19, SystemTimeToFileTime, to_file, 2),
    API(20, FileTimeToSystemTime, from_file, 2),
    API(21, GetFileTime, file_time, 4),
    API(22, GetDriveTypeA, drive, 1),
    API(23, GetLogicalDriveStringsA, drive_names, 2),
    API(24, GetDiskFreeSpaceA, disk, 5),
    API(25, GetVolumeInformationA, volume, 8),
    API(26, CreateFileMappingA, mapping, 6),
    API(27, MapViewOfFile, map, 5),
    API(28, UnmapViewOfFile, unmap, 1),
    API(29, SearchPathA, search, 6),
    API(30, OpenFile, open, 3)
#undef API
};
uint32_t bb_file_model_export(const char *name) {
  for (unsigned i = 0; i < sizeof file_targets / sizeof *file_targets; i++)
    if (!strcmp(name, file_targets[i].name))
      return file_targets[i].target.identity;
  return 0;
}
const struct bb_model_target *bb_file_model_find(void *ctx, uint32_t id) {
  (void)ctx;
  if (id < BB_FILE_MODEL_API || (id - BB_FILE_MODEL_API) % 4u)
    return NULL;
  uint32_t n = (id - BB_FILE_MODEL_API) / 4u;
  return n < sizeof file_targets / sizeof *file_targets
             ? &file_targets[n].target
             : NULL;
}
