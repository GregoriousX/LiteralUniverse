#pragma once

namespace lu::log {

void Open(const wchar_t* path);
void Write(const char* fmt, ...);

}  // namespace lu::log

#define LU_LOG(...) ::lu::log::Write(__VA_ARGS__)
