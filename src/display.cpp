// Window management: borderless and windowed presentation of the game's fullscreen device,
// Alt+Enter, DPI awareness and focus handling.
//
// The game only knows exclusive fullscreen and a small fixed-size window. In borderless and
// windowed mode the game keeps believing it runs fullscreen, while the device is created as a
// windowed device and the window geometry belongs to LiteralUniverse.
#include "display.h"

#include <shellscalingapi.h>

#include <cwchar>

#include "call_trace.h"
#include "config.h"
#include "diag_tools.h"
#include "game_patches.h"
#include "iat.h"
#include "log.h"

namespace lu {

namespace {

using SetWindowLongA_t = LONG(WINAPI*)(HWND, int, LONG);
using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using MoveWindow_t = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
using SetWindowPlacement_t = BOOL(WINAPI*)(HWND, const WINDOWPLACEMENT*);
using ShowWindow_t = BOOL(WINAPI*)(HWND, int);
using CreateWindowExA_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND,
                                        HMENU, HINSTANCE, LPVOID);
using ClipCursor_t = BOOL(WINAPI*)(const RECT*);

SetWindowLongA_t o_SetWindowLongA = SetWindowLongA;
SetWindowPos_t o_SetWindowPos = SetWindowPos;
MoveWindow_t o_MoveWindow = MoveWindow;
SetWindowPlacement_t o_SetWindowPlacement = SetWindowPlacement;
ShowWindow_t o_ShowWindow = ShowWindow;
CreateWindowExA_t o_CreateWindowExA = CreateWindowExA;
ClipCursor_t o_ClipCursor = ClipCursor;

HWND g_window = nullptr;
WNDPROC g_gameProc = nullptr;
DisplayMode g_mode = DisplayMode::Borderless;
UINT g_backBufferWidth = 0;
UINT g_backBufferHeight = 0;
bool g_placing = false;  // true while LiteralUniverse itself moves the window
bool g_copySwap = false;  // device uses D3DSWAPEFFECT_COPY, so Present can letterbox

bool Managed(HWND hwnd) {
  return hwnd && hwnd == g_window && g_mode != DisplayMode::Fullscreen;
}

// Monitor chosen by [Display] Monitor: 0 = primary, N = \\.\DISPLAYN.
HMONITOR TargetMonitor() {
  if (g_config.monitor > 0) {
    struct Search {
      int number;
      HMONITOR found;
    } search{g_config.monitor, nullptr};
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM data) -> BOOL {
          auto s = reinterpret_cast<Search*>(data);
          MONITORINFOEXW info{};
          info.cbSize = sizeof(info);
          GetMonitorInfoW(mon, &info);
          const wchar_t* digits = info.szDevice + wcscspn(info.szDevice, L"0123456789");
          if (_wtoi(digits) == s->number) {
            s->found = mon;
            return FALSE;
          }
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    if (search.found) return search.found;
    LU_LOG("Monitor=%d not found, using the primary display", g_config.monitor);
  }
  return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

MONITORINFO TargetMonitorInfo() {
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  GetMonitorInfoW(TargetMonitor(), &info);
  return info;
}

// Client size for windowed mode: the configured size, or the back buffer scaled to fit 85% of
// the work area with the same aspect ratio.
SIZE WindowedClientSize(const RECT& work) {
  if (g_config.windowWidth > 0 && g_config.windowHeight > 0)
    return SIZE{g_config.windowWidth, g_config.windowHeight};

  double bw = g_backBufferWidth ? g_backBufferWidth : 1280;
  double bh = g_backBufferHeight ? g_backBufferHeight : 720;
  double maxW = (work.right - work.left) * 0.85;
  double maxH = (work.bottom - work.top) * 0.85;
  double scale = maxW / bw < maxH / bh ? maxW / bw : maxH / bh;
  if (scale > 1.0) scale = 1.0;
  return SIZE{LONG(bw * scale + 0.5), LONG(bh * scale + 0.5)};
}

constexpr LONG kBorderlessStyle = WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS;
constexpr LONG kWindowedStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPSIBLINGS;

void PaintBlack(HWND hwnd, HDC dc) {
  RECT r;
  GetClientRect(hwnd, &r);
  FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
}

void ApplyPlacement() {
  if (!Managed(g_window)) return;
  MONITORINFO mon = TargetMonitorInfo();
  g_placing = true;

  LONG ex = GetWindowLongW(g_window, GWL_EXSTYLE);
  ex &= ~(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE |
          WS_EX_STATICEDGE);
  RECT r;
  LONG style;
  if (g_mode == DisplayMode::Borderless) {
    style = kBorderlessStyle;
    r = mon.rcMonitor;
    if (g_config.diagnostics && g_config.testWidth > 0)  // test resolution: a screen of that size
      r = RECT{0, 0, g_config.testWidth, g_config.testHeight};
  } else {
    style = kWindowedStyle;
    SIZE client = WindowedClientSize(mon.rcWork);
    r = RECT{0, 0, client.cx, client.cy};
    AdjustWindowRectExForDpi(&r, DWORD(style), FALSE, DWORD(ex), GetDpiForWindow(g_window));
    LONG w = r.right - r.left, h = r.bottom - r.top;
    LONG x = mon.rcWork.left + ((mon.rcWork.right - mon.rcWork.left) - w) / 2;
    LONG y = mon.rcWork.top + ((mon.rcWork.bottom - mon.rcWork.top) - h) / 2;
    if (y < mon.rcWork.top) y = mon.rcWork.top;
    r = RECT{x, y, x + w, y + h};
  }

  HWND after = HWND_NOTOPMOST;
  UINT flags = SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER;
  if (ScriptActive()) {
    // Scripted test runs happen off screen and never take focus.
    OffsetRect(&r, -32000 - r.left, -32000 - r.top);
    after = HWND_BOTTOM;
    flags |= SWP_NOACTIVATE;
  }

  o_SetWindowLongA(g_window, GWL_STYLE, style);
  o_SetWindowLongA(g_window, GWL_EXSTYLE, ex);
  if (IsZoomed(g_window) || IsIconic(g_window)) o_ShowWindow(g_window, SW_RESTORE);
  o_SetWindowPos(g_window, after, r.left, r.top, r.right - r.left, r.bottom - r.top, flags);
  InvalidateRect(g_window, nullptr, TRUE);
  g_placing = false;
  LU_LOG("Window placed: %s (%ld,%ld)-(%ld,%ld), back buffer %ux%u", ToString(g_mode), r.left,
         r.top, r.right, r.bottom, g_backBufferWidth, g_backBufferHeight);
}

void ToggleMode() {
  g_mode = g_mode == DisplayMode::Borderless ? DisplayMode::Windowed : DisplayMode::Borderless;
  SaveDisplayMode(g_mode);
  ApplyPlacement();
}

// Keeps the client area at the back buffer's aspect ratio while the window is resized.
void ConstrainSizing(WPARAM edge, RECT* r) {
  if (!g_backBufferWidth || !g_backBufferHeight) return;
  RECT frame{0, 0, 0, 0};
  AdjustWindowRectExForDpi(&frame, DWORD(GetWindowLongW(g_window, GWL_STYLE)), FALSE,
                           DWORD(GetWindowLongW(g_window, GWL_EXSTYLE)), GetDpiForWindow(g_window));
  LONG fw = (frame.right - frame.left), fh = (frame.bottom - frame.top);
  double aspect = double(g_backBufferWidth) / g_backBufferHeight;
  LONG cw = (r->right - r->left) - fw, ch = (r->bottom - r->top) - fh;

  if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) {
    cw = LONG(ch * aspect + 0.5);
    r->right = r->left + cw + fw;
  } else {
    ch = LONG(cw / aspect + 0.5);
    if (edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
      r->top = r->bottom - ch - fh;
    else
      r->bottom = r->top + ch + fh;
  }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_KEYDOWN:
      // F9: 30 FPS or the configured frame limit, for parts of the game that misbehave.
      if (wp == VK_F9 && !(lp & (1 << 30))) ToggleFrameLimit();
      if (wp == VK_F12 && g_config.diagnostics && !(lp & (1 << 30))) ToggleCallTrace();
      break;

    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
      // Alt+Enter: switch between borderless and windowed instead of the game's own toggle.
      if (wp == VK_RETURN && (lp & (1 << 29)) && g_config.altEnter &&
          g_mode != DisplayMode::Fullscreen) {
        if (msg == WM_SYSKEYDOWN && !(lp & (1 << 30))) ToggleMode();
        return 0;
      }
      break;

    case WM_SIZING:
      if (Managed(hwnd) && g_mode == DisplayMode::Windowed) {
        ConstrainSizing(wp, reinterpret_cast<RECT*>(lp));
        return TRUE;
      }
      break;

    case WM_ERASEBKGND:
      // Black bars around a back buffer with a different aspect ratio than the window.
      if (Managed(hwnd)) {
        PaintBlack(hwnd, reinterpret_cast<HDC>(wp));
        return 1;
      }
      break;

    case WM_SIZE:
      if (Managed(hwnd)) InvalidateRect(hwnd, nullptr, TRUE);
      break;

    case WM_DPICHANGED:
      // Keep the window's pixel size when it moves to a screen with a different scale.
      if (Managed(hwnd)) return 0;
      break;

    case WM_SETCURSOR:
      if (g_config.hideCursor && LOWORD(lp) == HTCLIENT) {
        SetCursor(nullptr);
        return TRUE;
      }
      break;

    case WM_ACTIVATEAPP:
      if (!wp) o_ClipCursor(nullptr);
      break;
  }
  return CallWindowProcA(g_gameProc, hwnd, msg, wp, lp);
}

void AttachWindow(HWND hwnd) {
  if (g_window == hwnd) return;
  g_window = hwnd;
  g_gameProc = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
  LU_LOG("Attached to game window %p", hwnd);
}

// --- user32 hooks (imports of DisneyUPC.exe) ---

LONG WINAPI Hook_SetWindowLongA(HWND hwnd, int index, LONG value) {
  if (!g_placing && Managed(hwnd) && (index == GWL_STYLE || index == GWL_EXSTYLE))
    return GetWindowLongA(hwnd, index);
  return o_SetWindowLongA(hwnd, index, value);
}

BOOL WINAPI Hook_SetWindowPos(HWND hwnd, HWND after, int x, int y, int w, int h, UINT flags) {
  if (!g_placing && Managed(hwnd)) {
    // The game only re-applies its own frame here; keep ours.
    if (after == HWND_TOPMOST) after = HWND_NOTOPMOST;
    flags |= SWP_NOMOVE | SWP_NOSIZE;
    flags &= ~SWP_FRAMECHANGED;
  }
  return o_SetWindowPos(hwnd, after, x, y, w, h, flags);
}

BOOL WINAPI Hook_MoveWindow(HWND hwnd, int x, int y, int w, int h, BOOL repaint) {
  if (!g_placing && Managed(hwnd)) return TRUE;
  return o_MoveWindow(hwnd, x, y, w, h, repaint);
}

BOOL WINAPI Hook_SetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT* wp) {
  if (!g_placing && Managed(hwnd)) return TRUE;
  return o_SetWindowPlacement(hwnd, wp);
}

BOOL WINAPI Hook_ShowWindow(HWND hwnd, int cmd) {
  if (ScriptActive() && (cmd == SW_SHOW || cmd == SW_SHOWNORMAL || cmd == SW_RESTORE))
    cmd = SW_SHOWNOACTIVATE;
  if (Managed(hwnd) && g_config.keepVisibleOnFocusLoss &&
      (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_SHOWMINNOACTIVE ||
       cmd == SW_FORCEMINIMIZE))
    return IsWindowVisible(hwnd);
  return o_ShowWindow(hwnd, cmd);
}

using GetSystemMetrics_t = int(WINAPI*)(int);
GetSystemMetrics_t o_GetSystemMetrics = GetSystemMetrics;

// Diagnostics with a test resolution: the screen size the game sees.
int WINAPI Hook_GetSystemMetrics(int index) {
  if (index == SM_CXSCREEN) return g_config.testWidth;
  if (index == SM_CYSCREEN) return g_config.testHeight;
  return o_GetSystemMetrics(index);
}

HWND WINAPI Hook_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y,
                                 int w, int h, HWND parent, HMENU menu, HINSTANCE inst,
                                 LPVOID param) {
  if (ScriptActive() && !parent) {
    x = -32000;
    y = -32000;
    ex |= WS_EX_NOACTIVATE;
  }
  HWND hwnd = o_CreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
  LU_LOG("CreateWindowExA '%s' style=0x%lx ex=0x%lx %d,%d %dx%d -> %p (error %lu)",
         title && HIWORD(title) ? title : "", style, ex, x, y, w, h, hwnd, hwnd ? 0 : GetLastError());
  return hwnd;
}

BOOL WINAPI Hook_ClipCursor(const RECT* rect) {
  // Never confine the cursor while another program has focus.
  if (rect && g_window && GetForegroundWindow() != g_window) return TRUE;
  return o_ClipCursor(rect);
}

template <typename T>
void Hook(HMODULE game, const char* name, T replacement, T& original) {
  if (void* prev = PatchImport(game, "USER32.dll", name, reinterpret_cast<void*>(replacement)))
    original = reinterpret_cast<T>(prev);
  else
    LU_LOG("user32 import %s not found", name);
}

}  // namespace

void InstallDisplayHooks(HMODULE game) {
  // Per-monitor DPI awareness: window sizes are real pixels on 4K screens with Windows scaling,
  // without the HIGHDPIAWARE compatibility flag.
  if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

  g_mode = g_config.mode;
  Hook(game, "SetWindowLongA", &Hook_SetWindowLongA, o_SetWindowLongA);
  Hook(game, "SetWindowPos", &Hook_SetWindowPos, o_SetWindowPos);
  Hook(game, "MoveWindow", &Hook_MoveWindow, o_MoveWindow);
  Hook(game, "SetWindowPlacement", &Hook_SetWindowPlacement, o_SetWindowPlacement);
  Hook(game, "ShowWindow", &Hook_ShowWindow, o_ShowWindow);
  if (ScriptActive()) Hook(game, "CreateWindowExA", &Hook_CreateWindowExA, o_CreateWindowExA);
  if (g_config.diagnostics && g_config.testWidth > 0)
    Hook(game, "GetSystemMetrics", &Hook_GetSystemMetrics, o_GetSystemMetrics);
  Hook(game, "ClipCursor", &Hook_ClipCursor, o_ClipCursor);
}

void PrepareDevice(HWND window, const D3DPRESENT_PARAMETERS& requested,
                   D3DPRESENT_PARAMETERS& out) {
  out = requested;
  if (window) AttachWindow(window);
  g_backBufferWidth = requested.BackBufferWidth;
  g_backBufferHeight = requested.BackBufferHeight;
  g_copySwap = false;
  if (g_mode == DisplayMode::Fullscreen) return;

  // The game's fullscreen device becomes a windowed device on a borderless or framed window.
  // COPY lets Present place the picture at its own aspect ratio inside the window.
  out.Windowed = TRUE;
  out.FullScreen_RefreshRateInHz = 0;
  g_copySwap = out.MultiSampleType == D3DMULTISAMPLE_NONE && out.BackBufferCount <= 1;
  if (g_copySwap) out.SwapEffect = D3DSWAPEFFECT_COPY;
  if (!out.BackBufferWidth || !out.BackBufferHeight) {
    MONITORINFO mon = TargetMonitorInfo();
    out.BackBufferWidth = UINT(mon.rcMonitor.right - mon.rcMonitor.left);
    out.BackBufferHeight = UINT(mon.rcMonitor.bottom - mon.rcMonitor.top);
    g_backBufferWidth = out.BackBufferWidth;
    g_backBufferHeight = out.BackBufferHeight;
  }
  ApplyPlacement();
}

void DeviceReady() {
  ApplyPlacement();
}

HWND GameWindow() { return g_window; }

bool PresentDestination(RECT& dest) {
  if (!g_copySwap || !Managed(g_window) || !g_backBufferWidth || !g_backBufferHeight) return false;
  RECT client;
  if (!GetClientRect(g_window, &client) || client.right <= 0 || client.bottom <= 0) return false;

  // Scale the back buffer to the largest rectangle with the same aspect ratio and centre it.
  LONG cw = client.right, ch = client.bottom;
  LONG w = cw, h = LONG(double(cw) * g_backBufferHeight / g_backBufferWidth + 0.5);
  if (h > ch) {
    h = ch;
    w = LONG(double(ch) * g_backBufferWidth / g_backBufferHeight + 0.5);
  }
  if (cw - w <= 1 && ch - h <= 1) return false;
  dest.left = (cw - w) / 2;
  dest.top = (ch - h) / 2;
  dest.right = dest.left + w;
  dest.bottom = dest.top + h;
  return true;
}

}  // namespace lu
