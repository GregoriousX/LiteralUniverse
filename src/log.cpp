#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace lu::log {

namespace {
HANDLE g_file = INVALID_HANDLE_VALUE;
SRWLOCK g_lock = SRWLOCK_INIT;
LARGE_INTEGER g_start{}, g_freq{};
}  // namespace

void Open(const wchar_t* path) {
  // Keep the previous session's log next to the new one (LiteralUniverse.prev.log).
  std::wstring previous = path;
  size_t dot = previous.find_last_of(L'.');
  previous.insert(dot == std::wstring::npos ? previous.size() : dot, L".prev");
  MoveFileExW(path, previous.c_str(), MOVEFILE_REPLACE_EXISTING);

  g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
  QueryPerformanceFrequency(&g_freq);
  QueryPerformanceCounter(&g_start);
}

void Write(const char* fmt, ...) {
  if (g_file == INVALID_HANDLE_VALUE) return;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  double t = double(now.QuadPart - g_start.QuadPart) / double(g_freq.QuadPart);

  char buf[1024];
  int n = snprintf(buf, sizeof(buf), "[%9.3f] ", t);
  va_list ap;
  va_start(ap, fmt);
  int m = vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
  va_end(ap);
  if (m < 0) return;
  n += (m < int(sizeof(buf)) - n - 2) ? m : int(sizeof(buf)) - n - 3;
  buf[n++] = '\r';
  buf[n++] = '\n';

  AcquireSRWLockExclusive(&g_lock);
  DWORD written;
  WriteFile(g_file, buf, DWORD(n), &written, nullptr);
  ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace lu::log
