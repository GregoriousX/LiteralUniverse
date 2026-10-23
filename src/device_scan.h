#pragma once

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

namespace lu {

using EnumDevices_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8A*, DWORD,
                                                  LPDIENUMDEVICESCALLBACKA, void*, DWORD);

// IDirectInput8::EnumDevices without the full HID scan when no game controller is connected.
//
// DirectInput opens every HID device on each EnumDevices call, whatever device class is asked
// for. A single device that doesn't answer (for example a USB audio device that ignores string
// requests) holds every call for several seconds. When Windows lists no game controller, the
// result can't contain one anyway, so controller queries return at once and keyboard/mouse
// queries stop as soon as the system keyboard and mouse have been reported.
HRESULT FastEnumDevices(EnumDevices_t real, IDirectInput8A* di, DWORD type,
                        LPDIENUMDEVICESCALLBACKA callback, void* context, DWORD flags);

}  // namespace lu
