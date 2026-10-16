/* SPDX-License-Identifier: GPL-3.0-only */
#include "native.h"
#include "codepage.h"
#include "unicode.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static U32 next_character(const char **text) {
  const unsigned char *p = (const unsigned char *)*text;
  unsigned first = *p, count;
  U32 character, minimum;
  ++*text;
  if (first < 128)
    return first;
  if (first >= 0xc2 && first <= 0xdf)
    character = first & 31, count = 1, minimum = 0x80;
  else if (first >= 0xe0 && first <= 0xef)
    character = first & 15, count = 2, minimum = 0x800;
  else if (first >= 0xf0 && first <= 0xf4)
    character = first & 7, count = 3, minimum = 0x10000;
  else
    return 0x110000 + first;
  for (unsigned i = 1; i <= count; ++i) {
    if ((p[i] & 0xc0) != 0x80)
      return 0x110000 + first;
    character = (character << 6) | (p[i] & 63);
  }
  if (character < minimum || character > 0x10ffff ||
      (character >= 0xd800 && character <= 0xdfff))
    return 0x110000 + first;
  *text += count;
  return character;
}
static int same_name(const char *a, const char *b) {
  while (*a && *b)
    if (bb_unicode_case(next_character(&a), 1) !=
        bb_unicode_case(next_character(&b), 1))
      return 0;
  return !*a && !*b;
}
static char *copy(const char *name) {
  char *result = strdup(name);
  if (!result)
    bb_die("path allocation failed");
  return result;
}
static char *join(const char *directory, const char *name) {
  char *result;
  size_t size = strlen(directory);
  if (asprintf(&result, "%s%s%s", directory,
               size && directory[size - 1] != '/' ? "/" : "", name) < 0)
    bb_die("path allocation failed");
  return result;
}
/* Resolve one component without changing cwd or caching directory contents.
 * Exact spelling wins; otherwise choose consistently on a Linux directory
 * containing multiple names that a Windows directory could not distinguish. */
static char *component(const char *directory, const char *name) {
  char *exact = join(directory, name);
  struct stat status;
  if (!lstat(exact, &status)) {
    free(exact);
    return copy(name);
  }
  free(exact);
  DIR *entries = opendir(*directory ? directory : ".");
  char *matched = NULL;
  if (entries) {
    struct dirent *entry;
    while ((entry = readdir(entries)))
      if (same_name(entry->d_name, name) &&
          (!matched || strcmp(entry->d_name, matched) < 0)) {
        free(matched);
        matched = copy(entry->d_name);
      }
    closedir(entries);
  }
  return matched ? matched : copy(name);
}
char *bb_host_path(const char *name) {
  int saved = errno;
  if (!name)
    name = "";
  struct stat status;
  if (!*name || !lstat(name, &status)) {
    char *result = copy(name);
    errno = saved;
    return result;
  }
  size_t length = strlen(name);
  int trailing = name[length - 1] == '/';
  char *parts = copy(name), *cursor = NULL;
  char *result = copy(*name == '/' ? "/" : "");
  for (char *part = strtok_r(parts, "/", &cursor); part;
       part = strtok_r(NULL, "/", &cursor)) {
    char *actual = !strcmp(part, ".") || !strcmp(part, "..")
                       ? copy(part)
                       : component(result, part);
    char *next = join(result, actual);
    free(actual);
    free(result);
    result = next;
  }
  free(parts);
  if (trailing && *result && result[strlen(result) - 1] != '/') {
    char *next = join(result, "");
    free(result);
    result = next;
  }
  errno = saved;
  return result;
}
char *bb_path(const char *windows) {
  if (!windows)
    return copy("");
  /* The game has one directory. Guest drive roots (including its reported
   * Z:\\BEASTS.EXE location) refer to that asset directory. Explicit POSIX
   * absolute paths retain the host root; no missing path loses its parent. */
  int guest = 0;
  if (((windows[0] >= 'A' && windows[0] <= 'Z') ||
       (windows[0] >= 'a' && windows[0] <= 'z')) && windows[1] == ':') {
    guest = 1;
    windows += 2;
    while (*windows == '/' || *windows == '\\')
      ++windows;
  } else if (windows[0] == '\\' && windows[1] != '\\') {
    guest = 1;
    ++windows;
  }
  if (guest && !*windows)
    return copy(".");
  size_t length = strlen(windows);
  if (length > (SIZE_MAX - 1) / 3)
    bb_die("path too long");
  char *utf8 = malloc(length * 3 + 1), *out = utf8;
  if (!utf8)
    bb_die("path allocation failed");
  for (; *windows; ++windows) {
    U32 c = bb_codepage_decode(1252, (unsigned char)*windows);
    if (c == '\\')
      c = '/';
    if (c < 0x80)
      *out++ = (char)c;
    else if (c < 0x800) {
      *out++ = (char)(0xc0 | (c >> 6));
      *out++ = (char)(0x80 | (c & 63));
    } else {
      *out++ = (char)(0xe0 | (c >> 12));
      *out++ = (char)(0x80 | ((c >> 6) & 63));
      *out++ = (char)(0x80 | (c & 63));
    }
  }
  *out = 0;
  char *result = bb_host_path(utf8);
  free(utf8);
  return result;
}
/* SearchPathA returns an ANSI path that must survive another CreateFileA call.
 * Native lookup paths are UTF-8; never return those bytes as if they were ACP. */
char *bb_ansi_path(const char *host) {
  char *result = copy(host), *out = result;
  while (*host) {
    int byte = bb_codepage_encode(1252, next_character(&host), 0);
    if (byte < 0) {
      free(result);
      errno = EILSEQ;
      return NULL;
    }
    *out++ = (char)byte;
  }
  *out = 0;
  return result;
}
