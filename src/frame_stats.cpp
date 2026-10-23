#include "frame_stats.h"

#include <algorithm>
#include <vector>

#include "log.h"

namespace lu {

namespace {

LARGE_INTEGER g_freq{};
LARGE_INTEGER g_lastEnd{};
LARGE_INTEGER g_reportStart{};
std::vector<double> g_intervals;  // ms between the ends of consecutive Present calls
double g_presentMs = 0;           // total time spent inside Present
unsigned g_unfocused = 0;         // frames presented while another window had focus

double Ms(LONGLONG ticks) { return double(ticks) * 1000.0 / double(g_freq.QuadPart); }

void Report() {
  if (g_intervals.empty()) return;
  std::vector<double> sorted = g_intervals;
  std::sort(sorted.begin(), sorted.end());
  size_t n = sorted.size();
  double sum = 0;
  unsigned over40 = 0, over50 = 0;
  for (double v : sorted) {
    sum += v;
    if (v > 40) ++over40;
    if (v > 50) ++over50;
  }
  LU_LOG("Frames: %zu  avg=%.2fms  min=%.2f  p50=%.2f  p90=%.2f  p99=%.2f  max=%.2f  "
         ">40ms=%u  >50ms=%u  inPresent=%.2fms/frame  unfocused=%u",
         n, sum / n, sorted.front(), sorted[n / 2], sorted[n * 9 / 10], sorted[n * 99 / 100],
         sorted.back(), over40, over50, g_presentMs / n, g_unfocused);

  // Sequence of the first frame intervals, rounded, to show pacing patterns such as 33/38/29.
  char line[512];
  int len = 0;
  for (size_t i = 0; i < g_intervals.size() && i < 60 && len < 480; ++i)
    len += snprintf(line + len, sizeof(line) - len, "%.0f ", g_intervals[i]);
  LU_LOG("  first intervals: %s", line);

  g_intervals.clear();
  g_presentMs = 0;
  g_unfocused = 0;
}

}  // namespace

void RecordFrame(HWND window, LARGE_INTEGER presentStart, LARGE_INTEGER presentEnd) {
  if (!g_freq.QuadPart) {
    QueryPerformanceFrequency(&g_freq);
    g_reportStart = presentEnd;
  } else {
    g_intervals.push_back(Ms(presentEnd.QuadPart - g_lastEnd.QuadPart));
  }
  g_lastEnd = presentEnd;
  g_presentMs += Ms(presentEnd.QuadPart - presentStart.QuadPart);
  if (window && GetForegroundWindow() != window) ++g_unfocused;

  if (presentEnd.QuadPart - g_reportStart.QuadPart > g_freq.QuadPart * 5) {
    g_reportStart = presentEnd;
    Report();
  }
}

}  // namespace lu
