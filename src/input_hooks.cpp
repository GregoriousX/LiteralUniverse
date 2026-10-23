// DirectInput fixes (fast device scan) and input timing diagnostics.
#include "input_hooks.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <intrin.h>
#include <xinput.h>

#include "config.h"
#include "device_scan.h"
#include "diag_tools.h"
#include "iat.h"
#include "input_vtbl.h"
#include "log.h"

namespace lu {

namespace {

LARGE_INTEGER g_freq;
LARGE_INTEGER g_lastReport;

struct Stat {
  const char* name;
  unsigned calls = 0;
  unsigned slow = 0;  // calls over 1 ms
  double totalMs = 0;
  double maxMs = 0;
};

enum StatId {
  kEnumDevices, kCreateDevice, kGetDeviceStatus, kAcquire, kGetDeviceState, kGetDeviceData,
  kPoll, kXGetState, kXSetState, kXGetCaps, kStatCount
};
Stat g_stats[kStatCount] = {
    {"EnumDevices"}, {"CreateDevice"}, {"GetDeviceStatus"}, {"Acquire"}, {"GetDeviceState"},
    {"GetDeviceData"}, {"Poll"}, {"XInputGetState"}, {"XInputSetState"},
    {"XInputGetCapabilities"},
};
SRWLOCK g_lock = SRWLOCK_INIT;

void Report(LARGE_INTEGER now) {
  if (now.QuadPart - g_lastReport.QuadPart < g_freq.QuadPart * 5) return;
  g_lastReport = now;
  for (auto& s : g_stats) {
    if (!s.calls) continue;
    LU_LOG("  %-22s calls=%-6u slow=%-4u avg=%.3fms max=%.2fms", s.name, s.calls, s.slow,
           s.totalMs / s.calls, s.maxMs);
    s = Stat{s.name};
  }
  LU_LOG("---- input, last 5 s");
}

// Measures one call. Calls slower than 4 ms are logged individually with their caller.
class Timer {
 public:
  Timer(StatId id, void* caller) : id_(id), caller_(caller) { QueryPerformanceCounter(&start_); }
  ~Timer() {
    if (!g_config.diagnostics) return;
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    double ms = double(end.QuadPart - start_.QuadPart) * 1000.0 / double(g_freq.QuadPart);
    AcquireSRWLockExclusive(&g_lock);
    Stat& s = g_stats[id_];
    ++s.calls;
    s.totalMs += ms;
    if (ms > s.maxMs) s.maxMs = ms;
    if (ms > 1.0) ++s.slow;
    if (ms > 4.0) {
      HMODULE mod = nullptr;
      char name[MAX_PATH] = "?";
      if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             static_cast<const char*>(caller_), &mod))
        GetModuleFileNameA(mod, name, MAX_PATH);
      LU_LOG("SLOW %s %.2f ms (from %p in %s)", s.name, ms, caller_, name);
    }
    Report(end);
    ReleaseSRWLockExclusive(&g_lock);
  }

 private:
  StatId id_;
  void* caller_;
  LARGE_INTEGER start_;
};

// --- IDirectInput8A ---

using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8A*, REFGUID,
                                                   IDirectInputDevice8A**, IUnknown*);
using GetDeviceStatus_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8A*, REFGUID);
EnumDevices_t o_EnumDevices;
CreateDevice_t o_CreateDevice;
GetDeviceStatus_t o_GetDeviceStatus;

// --- IDirectInputDevice8A ---

using DevNoArgs_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*);
using GetDeviceState_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*, DWORD, void*);
using GetDeviceData_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*, DWORD,
                                                    DIDEVICEOBJECTDATA*, DWORD*, DWORD);
DevNoArgs_t o_Acquire;
DevNoArgs_t o_Poll;
GetDeviceState_t o_GetDeviceState;
GetDeviceData_t o_GetDeviceData;

HRESULT STDMETHODCALLTYPE Hook_Acquire(IDirectInputDevice8A* dev) {
  Timer t(kAcquire, _ReturnAddress());
  return o_Acquire(dev);
}

HRESULT STDMETHODCALLTYPE Hook_Poll(IDirectInputDevice8A* dev) {
  Timer t(kPoll, _ReturnAddress());
  return o_Poll(dev);
}

HRESULT STDMETHODCALLTYPE Hook_GetDeviceState(IDirectInputDevice8A* dev, DWORD size, void* data) {
  Timer t(kGetDeviceState, _ReturnAddress());
  HRESULT hr = o_GetDeviceState(dev, size, data);
  if (ScriptActive() && size == 256 && data) {
    // Scripted test runs work while the game is in the background, where reads fail.
    if (FAILED(hr)) memset(data, 0, size);
    ApplyScriptedKeys(static_cast<BYTE*>(data));
    hr = DI_OK;
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE Hook_GetDeviceData(IDirectInputDevice8A* dev, DWORD size,
                                             DIDEVICEOBJECTDATA* data, DWORD* count, DWORD flags) {
  Timer t(kGetDeviceData, _ReturnAddress());
  return o_GetDeviceData(dev, size, data, count, flags);
}

HRESULT STDMETHODCALLTYPE Hook_EnumDevices(IDirectInput8A* di, DWORD type,
                                           LPDIENUMDEVICESCALLBACKA cb, void* ctx, DWORD flags) {
  Timer t(kEnumDevices, _ReturnAddress());
  if (g_config.fastDeviceScan) return FastEnumDevices(o_EnumDevices, di, type, cb, ctx, flags);
  return o_EnumDevices(di, type, cb, ctx, flags);
}

HRESULT STDMETHODCALLTYPE Hook_GetDeviceStatus(IDirectInput8A* di, REFGUID guid) {
  Timer t(kGetDeviceStatus, _ReturnAddress());
  return o_GetDeviceStatus(di, guid);
}

HRESULT STDMETHODCALLTYPE Hook_CreateDevice(IDirectInput8A* di, REFGUID guid,
                                            IDirectInputDevice8A** out, IUnknown* outer) {
  HRESULT hr;
  {
    Timer t(kCreateDevice, _ReturnAddress());
    hr = o_CreateDevice(di, guid, out, outer);
  }
  if (SUCCEEDED(hr) && out && *out) {
    HookVtable(*out, kDIDev_Acquire, &Hook_Acquire, o_Acquire);
    HookVtable(*out, kDIDev_Poll, &Hook_Poll, o_Poll);
    HookVtable(*out, kDIDev_GetDeviceState, &Hook_GetDeviceState, o_GetDeviceState);
    HookVtable(*out, kDIDev_GetDeviceData, &Hook_GetDeviceData, o_GetDeviceData);
  }
  return hr;
}

// --- XInput 1.3 (imported by ordinal) ---

using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using XInputSetState_t = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using XInputGetCapabilities_t = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
XInputGetState_t o_XInputGetState;
XInputSetState_t o_XInputSetState;
XInputGetCapabilities_t o_XInputGetCapabilities;

DWORD WINAPI Hook_XInputGetState(DWORD user, XINPUT_STATE* state) {
  Timer t(kXGetState, _ReturnAddress());
  return o_XInputGetState(user, state);
}

DWORD WINAPI Hook_XInputSetState(DWORD user, XINPUT_VIBRATION* vib) {
  Timer t(kXSetState, _ReturnAddress());
  return o_XInputSetState(user, vib);
}

DWORD WINAPI Hook_XInputGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps) {
  Timer t(kXGetCaps, _ReturnAddress());
  return o_XInputGetCapabilities(user, flags, caps);
}

template <typename T>
void HookOrdinal(HMODULE game, WORD ordinal, T replacement, T& original) {
  if (void* p = PatchImportOrdinal(game, "XINPUT1_3.dll", ordinal,
                                   reinterpret_cast<void*>(replacement)))
    original = reinterpret_cast<T>(p);
}

}  // namespace

void InstallInputHooks(HMODULE game) {
  QueryPerformanceFrequency(&g_freq);
  QueryPerformanceCounter(&g_lastReport);
  if (!g_config.diagnostics) return;
  HookOrdinal(game, 2, &Hook_XInputGetState, o_XInputGetState);
  HookOrdinal(game, 3, &Hook_XInputSetState, o_XInputSetState);
  HookOrdinal(game, 4, &Hook_XInputGetCapabilities, o_XInputGetCapabilities);
}

void OnDirectInputCreated(void* directInput) {
  if (!directInput) return;
  if (g_config.fastDeviceScan || g_config.diagnostics)
    HookVtable(directInput, kDI_EnumDevices, &Hook_EnumDevices, o_EnumDevices);
  if (!g_config.diagnostics) return;
  HookVtable(directInput, kDI_CreateDevice, &Hook_CreateDevice, o_CreateDevice);
  HookVtable(directInput, kDI_GetDeviceStatus, &Hook_GetDeviceStatus, o_GetDeviceStatus);
}

}  // namespace lu
