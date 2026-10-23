#pragma once

#include <windows.h>

namespace lu {

// Diagnostics: records the time between presented frames and the time spent inside Present,
// and writes a summary to the log every 5 seconds.
void RecordFrame(HWND window, LARGE_INTEGER presentStart, LARGE_INTEGER presentEnd);

}  // namespace lu
