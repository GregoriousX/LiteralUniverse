#pragma once

#include <windows.h>

#include <d3d9.h>

namespace lu {

void InstallDisplayHooks(HMODULE game);

// Called with the game's presentation parameters right before CreateDevice or Reset.
// Fills `out` with the parameters that are actually used for the device.
void PrepareDevice(HWND window, const D3DPRESENT_PARAMETERS& requested, D3DPRESENT_PARAMETERS& out);

// Called after CreateDevice or Reset succeeded.
void DeviceReady();

// Destination rectangle that shows the back buffer at its own aspect ratio inside the window.
// Returns false when the back buffer already fills the client area exactly.
bool PresentDestination(RECT& dest);

// The game's main window, once the device has been created.
HWND GameWindow();

}  // namespace lu
