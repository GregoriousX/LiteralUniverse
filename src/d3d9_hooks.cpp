#include "d3d9_hooks.h"

#include <d3d9.h>
#include <intrin.h>

#include <set>
#include <tuple>

#include "audio_hooks.h"
#include "config.h"
#include "d3d9_vtbl.h"
#include "diag_tools.h"
#include "display.h"
#include "frame_stats.h"
#include "iat.h"
#include "log.h"
#include "game_patches.h"

namespace lu {

namespace {

using Direct3DCreate9_t = IDirect3D9*(WINAPI*)(UINT);
using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                   D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using Reset_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND,
                                              const RGNDATA*);
using SetSamplerState_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD,
                                                      D3DSAMPLERSTATETYPE, DWORD);
using CreateTexture_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD,
                                                    D3DFORMAT, D3DPOOL, IDirect3DTexture9**,
                                                    HANDLE*);
using SetViewport_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const D3DVIEWPORT9*);

Direct3DCreate9_t o_Direct3DCreate9;
CreateDevice_t o_CreateDevice;
Reset_t o_Reset;
Present_t o_Present;
SetSamplerState_t o_SetSamplerState;
CreateTexture_t o_CreateTexture;
SetViewport_t o_SetViewport;

DWORD g_maxAnisotropy = 0;

void LogParams(const char* what, const D3DPRESENT_PARAMETERS& pp) {
  LU_LOG("%s: %ux%u fmt=%d msaa=%d swap=%d windowed=%d refresh=%u interval=0x%x", what,
         pp.BackBufferWidth, pp.BackBufferHeight, pp.BackBufferFormat, pp.MultiSampleType,
         pp.SwapEffect, pp.Windowed, pp.FullScreen_RefreshRateInHz, pp.PresentationInterval);
}

void BeforeDevice(HWND window, const D3DPRESENT_PARAMETERS& requested,
                  D3DPRESENT_PARAMETERS& actual) {
  LogParams("Game requested", requested);
  PrepareDevice(window, requested, actual);
  if (actual.Windowed != requested.Windowed) LogParams("Using", actual);
  if (g_config.nativeResolution)
    SetSceneRenderSize(actual.BackBufferWidth, actual.BackBufferHeight);
  if (g_config.fixAspectRatio && actual.BackBufferHeight)
    SetOutputAspect(float(actual.BackBufferWidth) / float(actual.BackBufferHeight));
}

// --- device hooks ---

HRESULT STDMETHODCALLTYPE Hook_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
  D3DPRESENT_PARAMETERS actual;
  BeforeDevice(pp->hDeviceWindow, *pp, actual);
  HRESULT hr = o_Reset(dev, &actual);
  LU_LOG("Reset -> 0x%08lx", hr);
  if (SUCCEEDED(hr)) DeviceReady();
  return hr;
}

HRESULT STDMETHODCALLTYPE Hook_Present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst,
                                       HWND window, const RGNDATA* dirty) {
  RECT fit;
  if (!src && !dst && PresentDestination(fit)) dst = &fit;
  if (!g_config.diagnostics) return o_Present(dev, src, dst, window, dirty);
  ScriptFrame(dev);

  LARGE_INTEGER start, end;
  QueryPerformanceCounter(&start);
  HRESULT hr = o_Present(dev, src, dst, window, dirty);
  QueryPerformanceCounter(&end);
  RecordFrame(GameWindow(), start, end);
  AudioDiagnosticsTick();
  return hr;
}

HRESULT STDMETHODCALLTYPE Hook_SetSamplerState(IDirect3DDevice9* dev, DWORD sampler,
                                               D3DSAMPLERSTATETYPE type, DWORD value) {
  // Upgrade trilinear/bilinear minification to anisotropic filtering.
  if (type == D3DSAMP_MINFILTER && value == D3DTEXF_LINEAR) {
    o_SetSamplerState(dev, sampler, D3DSAMP_MAXANISOTROPY, g_maxAnisotropy);
    value = D3DTEXF_ANISOTROPIC;
  } else if (type == D3DSAMP_MAXANISOTROPY) {
    value = g_maxAnisotropy;
  }
  return o_SetSamplerState(dev, sampler, type, value);
}

HRESULT STDMETHODCALLTYPE Hook_CreateTexture(IDirect3DDevice9* dev, UINT w, UINT h, UINT levels,
                                             DWORD usage, D3DFORMAT fmt, D3DPOOL pool,
                                             IDirect3DTexture9** out, HANDLE* shared) {
  HRESULT hr = o_CreateTexture(dev, w, h, levels, usage, fmt, pool, out, shared);
  if (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
    LU_LOG("Render target %ux%u usage=0x%lx fmt=%d -> 0x%08lx (from %p)", w, h, usage, fmt, hr,
           _ReturnAddress());
  return hr;
}

HRESULT STDMETHODCALLTYPE Hook_SetViewport(IDirect3DDevice9* dev, const D3DVIEWPORT9* vp) {
  static std::set<std::tuple<DWORD, DWORD, DWORD, DWORD>> seen;
  if (vp && seen.size() < 256 && seen.insert({vp->X, vp->Y, vp->Width, vp->Height}).second)
    LU_LOG("Viewport %lu,%lu %lux%lu (from %p)", vp->X, vp->Y, vp->Width, vp->Height,
           _ReturnAddress());
  return o_SetViewport(dev, vp);
}

void HookDevice(IDirect3DDevice9* dev) {
  HookVtable(dev, kDev_Reset, &Hook_Reset, o_Reset);
  HookVtable(dev, kDev_Present, &Hook_Present, o_Present);

  if (g_config.anisotropy > 1) {
    D3DCAPS9 caps{};
    dev->GetDeviceCaps(&caps);
    g_maxAnisotropy = DWORD(g_config.anisotropy);
    if (g_maxAnisotropy > caps.MaxAnisotropy) g_maxAnisotropy = caps.MaxAnisotropy;
    if (g_maxAnisotropy > 1 && (caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC)) {
      HookVtable(dev, kDev_SetSamplerState, &Hook_SetSamplerState, o_SetSamplerState);
      LU_LOG("Anisotropic filtering %lux", g_maxAnisotropy);
    }
  }

  if (g_config.diagnostics) {
    HookVtable(dev, kDev_CreateTexture, &Hook_CreateTexture, o_CreateTexture);
    HookVtable(dev, kDev_SetViewport, &Hook_SetViewport, o_SetViewport);
  }
}

HRESULT STDMETHODCALLTYPE Hook_CreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type,
                                            HWND focus, DWORD flags, D3DPRESENT_PARAMETERS* pp,
                                            IDirect3DDevice9** out) {
  D3DPRESENT_PARAMETERS actual;
  BeforeDevice(pp->hDeviceWindow ? pp->hDeviceWindow : focus, *pp, actual);
  HRESULT hr = o_CreateDevice(d3d, adapter, type, focus, flags, &actual, out);
  LU_LOG("CreateDevice adapter=%u flags=0x%lx -> 0x%08lx", adapter, flags, hr);
  if (SUCCEEDED(hr) && out && *out) {
    HookDevice(*out);
    DeviceReady();
  }
  return hr;
}

// --- test resolution (diagnostics): the desktop mode the game sees, added to the mode list ---

using GetAdapterModeCount_t = UINT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DFORMAT);
using EnumAdapterModes_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DFORMAT, UINT,
                                                       D3DDISPLAYMODE*);
using GetAdapterDisplayMode_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDISPLAYMODE*);
GetAdapterModeCount_t o_GetAdapterModeCount;
EnumAdapterModes_t o_EnumAdapterModes;
GetAdapterDisplayMode_t o_GetAdapterDisplayMode;

UINT STDMETHODCALLTYPE Hook_GetAdapterModeCount(IDirect3D9* d3d, UINT adapter, D3DFORMAT fmt) {
  return o_GetAdapterModeCount(d3d, adapter, fmt) + 1;
}

HRESULT STDMETHODCALLTYPE Hook_EnumAdapterModes(IDirect3D9* d3d, UINT adapter, D3DFORMAT fmt,
                                                UINT index, D3DDISPLAYMODE* mode) {
  UINT count = o_GetAdapterModeCount(d3d, adapter, fmt);
  if (index < count) return o_EnumAdapterModes(d3d, adapter, fmt, index, mode);
  HRESULT hr = o_EnumAdapterModes(d3d, adapter, fmt, count - 1, mode);
  if (SUCCEEDED(hr)) {
    mode->Width = UINT(g_config.testWidth);
    mode->Height = UINT(g_config.testHeight);
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE Hook_GetAdapterDisplayMode(IDirect3D9* d3d, UINT adapter,
                                                     D3DDISPLAYMODE* mode) {
  HRESULT hr = o_GetAdapterDisplayMode(d3d, adapter, mode);
  if (SUCCEEDED(hr) && mode) {
    LU_LOG("GetAdapterDisplayMode %ux%u -> test %dx%d", mode->Width, mode->Height,
           g_config.testWidth, g_config.testHeight);
    mode->Width = UINT(g_config.testWidth);
    mode->Height = UINT(g_config.testHeight);
  }
  return hr;
}

IDirect3D9* WINAPI Hook_Direct3DCreate9(UINT sdk) {
  IDirect3D9* d3d = o_Direct3DCreate9(sdk);
  if (d3d)
    HookVtable(d3d, kD3D9_CreateDevice, &Hook_CreateDevice, o_CreateDevice);
  if (d3d && g_config.diagnostics && g_config.testWidth > 0 && g_config.testHeight > 0) {
    HookVtable(d3d, kD3D9_GetAdapterModeCount, &Hook_GetAdapterModeCount, o_GetAdapterModeCount);
    HookVtable(d3d, kD3D9_EnumAdapterModes, &Hook_EnumAdapterModes, o_EnumAdapterModes);
    HookVtable(d3d, kD3D9_GetAdapterDisplayMode, &Hook_GetAdapterDisplayMode,
               o_GetAdapterDisplayMode);
    LU_LOG("Test resolution %dx%d", g_config.testWidth, g_config.testHeight);
  }
  return d3d;
}

}  // namespace

void InstallD3D9Hooks(HMODULE game) {
  o_Direct3DCreate9 = Direct3DCreate9_t(
      PatchImport(game, "d3d9.dll", "Direct3DCreate9", &Hook_Direct3DCreate9));
  if (!o_Direct3DCreate9) LU_LOG("d3d9 import not found: display fixes disabled");
}

}  // namespace lu
