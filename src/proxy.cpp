// Forwards the DirectInput 8 exports to the system dinput8.dll.
#include <windows.h>
#include <unknwn.h>

#include "input_hooks.h"
#include "log.h"

namespace {

HMODULE g_real = nullptr;

FARPROC Real(const char* name) {
  if (!g_real) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);  // SysWOW64 for this 32-bit process
    lstrcpynW(path + n, L"\\dinput8.dll", MAX_PATH - n);
    g_real = LoadLibraryW(path);
    if (!g_real) {
      LU_LOG("Could not load the system dinput8.dll (error %lu)", GetLastError());
      return nullptr;
    }
  }
  return GetProcAddress(g_real, name);
}

template <typename Fn>
Fn RealAs(const char* name) {
  static FARPROC p = Real(name);
  return reinterpret_cast<Fn>(p);
}

}  // namespace

extern "C" {

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID iid, void** out,
                                        IUnknown* outer) {
  auto fn = RealAs<HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*)>(
      "DirectInput8Create");
  if (!fn) return E_FAIL;
  HRESULT hr = fn(inst, version, iid, out, outer);
  if (SUCCEEDED(hr) && out) lu::OnDirectInputCreated(*out);
  return hr;
}

HRESULT WINAPI Proxy_DllCanUnloadNow() {
  auto fn = RealAs<HRESULT(WINAPI*)()>("DllCanUnloadNow");
  return fn ? fn() : S_FALSE;
}

HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
  auto fn = RealAs<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>("DllGetClassObject");
  return fn ? fn(clsid, iid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI Proxy_DllRegisterServer() {
  auto fn = RealAs<HRESULT(WINAPI*)()>("DllRegisterServer");
  return fn ? fn() : E_FAIL;
}

HRESULT WINAPI Proxy_DllUnregisterServer() {
  auto fn = RealAs<HRESULT(WINAPI*)()>("DllUnregisterServer");
  return fn ? fn() : E_FAIL;
}

const void* WINAPI Proxy_GetdfDIJoystick() {
  auto fn = RealAs<const void*(WINAPI*)()>("GetdfDIJoystick");
  return fn ? fn() : nullptr;
}

}  // extern "C"
