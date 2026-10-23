/* Vtable slot numbers taken from the C declarations in d3d9.h, so the hooks
   never depend on hand-counted indices. */
#define CINTERFACE
#define COBJMACROS
#include <stddef.h>
#include <d3d9.h>

#include "d3d9_vtbl.h"

#define SLOT(vtbl, member) (unsigned)(offsetof(vtbl, member) / sizeof(void*))

const unsigned kD3D9_CreateDevice = SLOT(IDirect3D9Vtbl, CreateDevice);
const unsigned kD3D9_GetAdapterModeCount = SLOT(IDirect3D9Vtbl, GetAdapterModeCount);
const unsigned kD3D9_EnumAdapterModes = SLOT(IDirect3D9Vtbl, EnumAdapterModes);
const unsigned kD3D9_GetAdapterDisplayMode = SLOT(IDirect3D9Vtbl, GetAdapterDisplayMode);

const unsigned kDev_Reset = SLOT(IDirect3DDevice9Vtbl, Reset);
const unsigned kDev_Present = SLOT(IDirect3DDevice9Vtbl, Present);
const unsigned kDev_CreateTexture = SLOT(IDirect3DDevice9Vtbl, CreateTexture);
const unsigned kDev_CreateRenderTarget = SLOT(IDirect3DDevice9Vtbl, CreateRenderTarget);
const unsigned kDev_CreateDepthStencilSurface = SLOT(IDirect3DDevice9Vtbl, CreateDepthStencilSurface);
const unsigned kDev_StretchRect = SLOT(IDirect3DDevice9Vtbl, StretchRect);
const unsigned kDev_SetRenderTarget = SLOT(IDirect3DDevice9Vtbl, SetRenderTarget);
const unsigned kDev_SetViewport = SLOT(IDirect3DDevice9Vtbl, SetViewport);
const unsigned kDev_SetSamplerState = SLOT(IDirect3DDevice9Vtbl, SetSamplerState);
const unsigned kDev_SetScissorRect = SLOT(IDirect3DDevice9Vtbl, SetScissorRect);
const unsigned kDev_TestCooperativeLevel = SLOT(IDirect3DDevice9Vtbl, TestCooperativeLevel);
