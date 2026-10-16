/* SPDX-License-Identifier: GPL-3.0-only */
/* Host file/time APIs shared by the working i386 runtime and portable C ABI. */
#define _FILE_OFFSET_BITS 64
#define _TIME_BITS 64
#include "kernel_handle.h"
#ifdef BB_MODEL_RUNTIME
#include "../bridge/address.h"
#include "../bridge/heap.h"
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
static _Atomic(uintptr_t) standard_handles[3] = {1, 2, 3};
static Handle W CreateFileA(const char *name, U32 access, U32 sharing,
                            Handle security, U32 creation, U32 flags,
                            Handle template) {
  char *path = bb_path(name);
  int options = (access & 0x40000000)
                    ? ((access & 0x80000000) ? O_RDWR : O_WRONLY)
                    : O_RDONLY;
#ifdef O_LARGEFILE
  options |= O_LARGEFILE;
#endif
  if (creation == 1)
    options |= O_CREAT | O_EXCL;
  if (creation == 2)
    options |= O_CREAT | O_TRUNC;
  if (creation == 4)
    options |= O_CREAT;
  if (creation == 5)
    options |= O_TRUNC;
  int fd = open(path, options, 0666);
  int saved_errno = errno;
  if (bb_trace)
    fprintf(stderr, "CreateFile %s -> %d\n", path, fd);
  free(path);
  if (fd < 0) {
    errno = saved_errno;
    bb_kernel_last_error = bb_kernel_errno();
    return (void *)UINT32_MAX;
  }
  return bb_kernel_handle_new(1, fd);
}
static int W CloseHandle(Handle handle) {
  if (!handle || handle == (void *)UINT32_MAX)
    return bb_kernel_error(6);
  if ((uintptr_t)handle <= 3)
    return 1;
  struct KernelHandle *h = handle;
  bb_kernel_handle_release(h);
  return 1;
}
static int W ReadFile(Handle handle, void *data, U32 size, U32 *read_out,
                      Handle overlapped) {
  int fd = bb_file_descriptor(handle);
  ssize_t amount = read(fd, data, size);
  if (read_out)
    *read_out = amount < 0 ? 0 : (U32)amount;
  if (amount < 0) {
    int saved_errno = errno;
    int mode = saved_errno == EBADF ? fcntl(fd, F_GETFL) : -1;
    errno = saved_errno;
    bb_kernel_last_error =
        mode >= 0 && (mode & O_ACCMODE) == O_WRONLY ? 5 : bb_kernel_errno();
  }
  if (!amount && size) {
    struct stat status;
    if (!fstat(fd, &status) &&
        (S_ISFIFO(status.st_mode) || S_ISSOCK(status.st_mode))) {
      bb_kernel_last_error = 109;
      return 0;
    }
  }
  return amount >= 0;
}
static int W WriteFile(Handle handle, const void *data, U32 size, U32 *written,
                       Handle overlapped) {
  int fd = bb_file_descriptor(handle);
  ssize_t amount = write(fd, data, size);
  if (written)
    *written = amount < 0 ? 0 : (U32)amount;
  if (amount < 0) {
    int saved_errno = errno;
    int mode = saved_errno == EBADF ? fcntl(fd, F_GETFL) : -1;
    errno = saved_errno;
    bb_kernel_last_error =
        mode >= 0 && (mode & O_ACCMODE) == O_RDONLY ? 5 : bb_kernel_errno();
  }
  return amount >= 0;
}
static U32 W SetFilePointer(Handle handle, I32 low, I32 *high, U32 method) {
  int64_t offset = (int64_t)low;
  if (high)
    offset = (int64_t)(((uint64_t)(U32)*high << 32) | (U32)low);
  int fd = bb_file_descriptor(handle);
  if (method > SEEK_END) {
    bb_kernel_last_error = 87;
    return UINT32_MAX;
  }
  off_t initial = lseek(fd, 0, SEEK_CUR);
  off_t position = lseek(fd, offset, (int)method);
  if (position < 0) {
    bb_kernel_last_error =
        errno == EINVAL && offset < 0 ? 131 : bb_kernel_errno();
    return UINT32_MAX;
  }
  if (!high && (uint64_t)position > UINT32_MAX) {
    if (initial >= 0)
      lseek(fd, initial, SEEK_SET);
    bb_kernel_last_error = 87;
    return UINT32_MAX;
  }
  if (high)
    *high = (I32)((uint64_t)position >> 32);
  if ((U32)position == UINT32_MAX)
    bb_kernel_last_error = 0;
  return (U32)position;
}
static int W SetEndOfFile(Handle handle) {
  int fd = bb_file_descriptor(handle);
  off_t position = lseek(fd, 0, SEEK_CUR);
  if (position < 0 || ftruncate(fd, position)) {
    bb_kernel_last_error = bb_kernel_errno();
    return 0;
  }
  return 1;
}
static int W FlushFileBuffers(Handle handle) {
  if (fsync(bb_file_descriptor(handle))) {
    bb_kernel_last_error = bb_kernel_errno();
    return 0;
  }
  return 1;
}
static int W _lopen(const char *name, int flags) {
  char *path = bb_path(name);
  int fd = open(path, flags & 3);
  free(path);
  return fd;
}
static U32 W _lread(int file, void *data, U32 amount) {
  return (U32)read(file, data, amount);
}
static int W _lclose(int file) { return close(file); }
static U32 W GetFileType(Handle handle) {
  struct stat s;
  if (fstat(bb_file_descriptor(handle), &s)) {
    bb_kernel_last_error = bb_kernel_errno();
    return 0;
  }
  if (S_ISFIFO(s.st_mode) || S_ISSOCK(s.st_mode))
    return 3;
  return S_ISREG(s.st_mode) || S_ISBLK(s.st_mode) ? 1 : 2;
}
static Handle W GetStdHandle(I32 number) {
  if (number < -12 || number > -10) {
    bb_kernel_last_error = 87;
    return (void *)UINT32_MAX;
  }
  return (Handle)atomic_load_explicit(&standard_handles[-10 - number],
                                      memory_order_relaxed);
}
static int W SetStdHandle(U32 number, Handle handle) {
  if (number < 0xfffffff4u || number > 0xfffffff6u) {
    bb_kernel_last_error = 87;
    return 0;
  }
  atomic_store_explicit(&standard_handles[0xfffffff6u - number],
                        (uintptr_t)handle, memory_order_relaxed);
  return 1;
}
static U32 W SetHandleCount(U32 count) { return count; }
static U32 W GetLastError(void) { return bb_kernel_last_error; }
static void W SetLastError(U32 error) { bb_kernel_last_error = error; }
static void fill_time(U16 *output, const struct tm *t, int milliseconds) {
  output[0] = (U16)(t->tm_year + 1900);
  output[1] = (U16)(t->tm_mon + 1);
  output[2] = (U16)t->tm_wday;
  output[3] = (U16)t->tm_mday;
  output[4] = (U16)t->tm_hour;
  output[5] = (U16)t->tm_min;
  output[6] = (U16)t->tm_sec;
  output[7] = (U16)milliseconds;
}
static void W GetSystemTime(U16 *output) {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  struct tm broken;
  gmtime_r(&t.tv_sec, &broken);
  fill_time(output, &broken, (int)(t.tv_nsec / 1000000));
}
static void W GetLocalTime(U16 *output) {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  struct tm broken;
  localtime_r(&t.tv_sec, &broken);
  fill_time(output, &broken, (int)(t.tv_nsec / 1000000));
}
static U32 W GetTimeZoneInformation(void *output) {
  U32 result = bb_time_zone_information((int64_t)time(NULL), output);
  if (result == UINT32_MAX)
    bb_kernel_last_error = 87;
  return result;
}
/* FILETIME belongs to byte-aligned game storage, not a native uint64_t slot. */
static int W SystemTimeToFileTime(const U16 *input, void *output) {
  if (!input || !output)
    return bb_kernel_error(87);
  U16 fields[8];
  memcpy(fields, input, sizeof(fields));
  unsigned year = fields[0], month = fields[1], day = fields[3];
  static const unsigned days[] = {31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
  /* Preserve the reference API's signed-short year limit and its acceptance
   * of February 29 in a non-leap year (normalized to March 1 by timegm).
   * Other out-of-range fields fail instead of being silently normalized. */
  if (year < 1601 || year > 32767 || month < 1 || month > 12 || !day ||
      day > (month == 2 ? 29u : days[month - 1]) || fields[4] > 23 ||
      fields[5] > 59 || fields[6] > 59 || fields[7] > 999)
    return bb_kernel_error(87);
  struct tm t = {0};
  t.tm_year = (int)year - 1900;
  t.tm_mon = (int)month - 1;
  t.tm_mday = (int)day;
  t.tm_hour = fields[4];
  t.tm_min = fields[5];
  t.tm_sec = fields[6];
  /* wDayOfWeek is deliberately ignored. Valid dates fit native 64-bit time_t.
   */
  uint64_t value = ((uint64_t)timegm(&t) + 11644473600ULL) * 10000000 +
                   (uint64_t)fields[7] * 10000;
  memcpy(output, &value, sizeof(value));
  return 1;
}
static int W FileTimeToSystemTime(const void *input, U16 *output) {
  if (!input || !output)
    return bb_kernel_error(87);
  uint64_t value;
  memcpy(&value, input, sizeof(value));
  if (value >= (UINT64_C(1) << 63))
    return bb_kernel_error(87);
  time_t seconds = (time_t)(value / 10000000) - INT64_C(11644473600);
  struct tm t;
  if (!gmtime_r(&seconds, &t))
    return bb_kernel_error(87);
  fill_time(output, &t, (int)((value % 10000000) / 10000));
  return 1;
}
static uint64_t file_time(int64_t seconds, long nanoseconds) {
  /* FILETIME counts 100 ns intervals since 1601, independently of host time_t
   * width and the archive format's coarser SYSTEMTIME millisecond precision. */
  return (uint64_t)seconds * UINT64_C(10000000) + UINT64_C(116444736000000000) +
         (uint64_t)nanoseconds / 100;
}
static int W GetFileTime(Handle handle, void *created, void *accessed,
                         void *modified) {
  int fd = bb_file_descriptor(handle);
  struct stat s;
  if (fstat(fd, &s)) {
    bb_kernel_last_error = bb_kernel_errno();
    return 0;
  }
  if (created) {
#ifdef __APPLE__
    uint64_t value =
        file_time(s.st_birthtimespec.tv_sec, s.st_birthtimespec.tv_nsec);
#else
    uint64_t value = file_time(s.st_ctim.tv_sec, s.st_ctim.tv_nsec);
#endif
#ifdef __linux__
    struct statx extended;
    if (!statx(fd, "", AT_EMPTY_PATH, STATX_BTIME, &extended) &&
        (extended.stx_mask & STATX_BTIME))
      value = file_time(extended.stx_btime.tv_sec, extended.stx_btime.tv_nsec);
#endif
    memcpy(created, &value, sizeof(value));
  }
  if (accessed) {
#ifdef __APPLE__
    uint64_t value = file_time(s.st_atimespec.tv_sec, s.st_atimespec.tv_nsec);
#else
    uint64_t value = file_time(s.st_atim.tv_sec, s.st_atim.tv_nsec);
#endif
    memcpy(accessed, &value, 8);
  }
  if (modified) {
#ifdef __APPLE__
    uint64_t value = file_time(s.st_mtimespec.tv_sec, s.st_mtimespec.tv_nsec);
#else
    uint64_t value = file_time(s.st_mtim.tv_sec, s.st_mtim.tv_nsec);
#endif
    memcpy(modified, &value, 8);
  }
  return 1;
}
static U32 W GetDriveTypeA(const char *path) { return 3; }
static U32 W GetLogicalDriveStringsA(U32 capacity, char *output) {
  const char data[] = "Z:\\\0";
  if (capacity >= sizeof(data))
    memcpy(output, data, sizeof(data));
  return sizeof(data) - 1;
}
static int W GetDiskFreeSpaceA(const char *root, U32 *sectors, U32 *bytes,
                               U32 *free_clusters, U32 *total) {
  struct statvfs v;
  if (statvfs(".", &v))
    return 0;
  *bytes = 512;
  *sectors = (U32)v.f_frsize / 512;
  *free_clusters = (U32)v.f_bavail;
  *total = (U32)v.f_blocks;
  return 1;
}
static int W GetVolumeInformationA(const char *root, char *name, U32 capacity,
                                   U32 *serial, U32 *max_name, U32 *flags,
                                   char *filesystem, U32 fs_capacity) {
  if (name && capacity)
    snprintf(name, capacity, "BEASTS");
  if (serial)
    *serial = 0xbb1997;
  if (max_name)
    *max_name = 255;
  if (flags)
    *flags = 2;
  if (filesystem && fs_capacity)
    snprintf(filesystem, fs_capacity, "Linux");
  return 1;
}
struct MappingView {
  void *address;
  size_t length;
  struct MappingView *next;
#ifdef BB_MODEL_RUNTIME
  uint32_t reference;
#endif
};
static struct MappingView *mapping_views;
static pthread_mutex_t mapping_mutex = PTHREAD_MUTEX_INITIALIZER;
static Handle W CreateFileMappingA(Handle file, Handle security, U32 protection,
                                   U32 high, U32 low, const char *name) {
  struct KernelHandle *h =
      bb_kernel_handle_new(4, dup(bb_file_descriptor(file)));
  struct stat s;
  if (fstat(h->fd, &s)) {
    bb_kernel_last_error = bb_kernel_errno();
    if (h->fd >= 0)
      close(h->fd);
    free(h);
    return NULL;
  }
  h->size = low ? low : (size_t)s.st_size;
  return h;
}
static void *W MapViewOfFile(Handle mapping, U32 access, U32 high, U32 low,
                             U32 size) {
  struct KernelHandle *h = mapping;
  if ((uintptr_t)mapping <= 3 || mapping == (void *)UINT32_MAX ||
      h->type != 4) {
    bb_kernel_last_error = 6;
    return NULL;
  }
  size_t length = size ? size : h->size;
  void *result = mmap(NULL, length, PROT_READ | ((access & 2) ? PROT_WRITE : 0),
                      MAP_PRIVATE, h->fd, (off_t)low);
  if (result == MAP_FAILED) {
    bb_kernel_last_error = bb_kernel_errno();
    return NULL;
  }
  struct MappingView *view = malloc(sizeof(*view));
  if (!view) {
    munmap(result, length);
    bb_kernel_last_error = 8;
    return NULL;
  }
  *view =
      (struct MappingView){.address = result, .length = length, .next = NULL};
  pthread_mutex_lock(&mapping_mutex);
  view->next = mapping_views;
  mapping_views = view;
  pthread_mutex_unlock(&mapping_mutex);
  return result;
}
/* Caller holds mapping_mutex. A logical unmap validates/removes the reference
 * under this same lock, so a stale request cannot unmap a newly reused view. */
static int unmap_view_locked(struct MappingView **link) {
  struct MappingView *view = *link;
  if (!view)
    return bb_kernel_error(87);
  if (munmap(view->address, view->length)) {
    bb_kernel_last_error = bb_kernel_errno();
    return 0;
  }
#ifdef BB_PORTABLE_RUNTIME
  if (view->reference && !bb_address_unbind(view->reference))
    bb_die("native file mapping lost its logical reference");
#endif
  *link = view->next;
  free(view);
  return 1;
}
static int W UnmapViewOfFile(void *address) {
  pthread_mutex_lock(&mapping_mutex);
  struct MappingView **link = &mapping_views;
  while (*link && (*link)->address != address)
    link = &(*link)->next;
  int result = unmap_view_locked(link);
  pthread_mutex_unlock(&mapping_mutex);
  return result;
}
static char *search_file(const char *name) {
  char *resolved = bb_path(name);
  struct stat status;
  if (!stat(resolved, &status) && !S_ISDIR(status.st_mode))
    return resolved;
  free(resolved);
  return NULL;
}
static U32 W SearchPathA(const char *path, const char *file,
                         const char *extension, U32 size, char *output,
                         char **part) {
  if (!file || !*file || (size && !output))
    return (U32)bb_kernel_error(87);
  const char *base = file;
  for (const char *p = file; *p; ++p)
    if (*p == '/' || *p == '\\')
      base = p + 1;
  char *name;
  if (asprintf(&name, "%s%s", file,
               extension && !strchr(base, '.') ? extension : "") < 0)
    bb_die("path allocation failed");
  char *resolved = NULL;
  int qualified =
      file[0] == '/' || file[0] == '\\' || (file[0] && file[1] == ':');
  if (qualified || !path || !*path)
    resolved = search_file(name);
  else {
    char *directories = strdup(path), *remaining = directories, *directory;
    if (!directories)
      bb_die("path allocation failed");
    while (!resolved && (directory = strsep(&remaining, ";"))) {
      char *candidate;
      if (asprintf(&candidate, "%s%s%s", directory, *directory ? "/" : "",
                   name) < 0)
        bb_die("path allocation failed");
      resolved = search_file(candidate);
      free(candidate);
    }
    free(directories);
  }
  free(name);
  char *ansi = NULL;
  if (!resolved && !path && !qualified) {
    /* DirectSound discovery uses SearchPath before LoadLibrary. These Windows
     * services and the C game/mixer modules exist without files on disk. */
    char *candidate;
    if (asprintf(&candidate, "%s%s", file,
                 extension && !strchr(base, '.') ? extension : "") < 0)
      bb_die("path allocation failed");
    const char *library = bb_library_name(candidate);
    free(candidate);
    if (library && asprintf(&ansi, "Z:\\%s", library) < 0)
      bb_die("path allocation failed");
  }
  if (resolved) {
    char *absolute = realpath(resolved, NULL);
    free(resolved);
    if (!absolute) {
      bb_kernel_last_error = bb_kernel_errno();
      return 0;
    }
    ansi = bb_ansi_path(absolute);
    free(absolute);
    if (!ansi)
      return (U32)bb_kernel_error(1113);
  }
  if (!ansi)
    return (U32)bb_kernel_error(2);
  size_t length = strlen(ansi);
  if (size <= length) {
    free(ansi);
    return (U32)length + 1;
  }
  memcpy(output, ansi, length + 1);
  free(ansi);
  if (part) {
    *part = output;
    for (char *p = output; *p; ++p)
      if (*p == '/' || *p == '\\')
        *part = p + 1;
  }
  return (U32)length;
}
static U32 W OpenFile(const char *name, void *information, U32 flags) {
  return (U32)_lopen(name, (int)(flags & 3));
}

#ifndef BB_MODEL_RUNTIME
void *bb_file_api(const char *name) {
  static const struct ApiEntry table[] = {API_ENTRY(CreateFileA),
                                          API_ENTRY(CloseHandle),
                                          API_ENTRY(ReadFile),
                                          API_ENTRY(WriteFile),
                                          API_ENTRY(SetFilePointer),
                                          API_ENTRY(SetEndOfFile),
                                          API_ENTRY(FlushFileBuffers),
                                          API_ENTRY(_lopen),
                                          API_ENTRY(_lread),
                                          API_ENTRY(_lclose),
                                          API_ENTRY(GetFileType),
                                          API_ENTRY(GetStdHandle),
                                          API_ENTRY(SetStdHandle),
                                          API_ENTRY(SetHandleCount),
                                          API_ENTRY(GetLastError),
                                          API_ENTRY(SetLastError),
                                          API_ENTRY(GetSystemTime),
                                          API_ENTRY(GetLocalTime),
                                          API_ENTRY(GetTimeZoneInformation),
                                          API_ENTRY(SystemTimeToFileTime),
                                          API_ENTRY(FileTimeToSystemTime),
                                          API_ENTRY(GetFileTime),
                                          API_ENTRY(GetDriveTypeA),
                                          API_ENTRY(GetLogicalDriveStringsA),
                                          API_ENTRY(GetDiskFreeSpaceA),
                                          API_ENTRY(GetVolumeInformationA),
                                          API_ENTRY(CreateFileMappingA),
                                          API_ENTRY(MapViewOfFile),
                                          API_ENTRY(UnmapViewOfFile),
                                          API_ENTRY(SearchPathA),
                                          API_ENTRY(OpenFile)};
  API_RETURN(table);
}
#else
#include "file_models.inc"
#endif
