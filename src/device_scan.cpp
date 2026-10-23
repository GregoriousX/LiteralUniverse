#include "device_scan.h"

#include <initguid.h>
#include <hidclass.h>
#include <setupapi.h>

#include <cwchar>

#include "log.h"

namespace lu {

namespace {

// Looks for a "HID_DEVICE_SYSTEM_GAME" ID, which Windows gives every HID joystick, gamepad and
// multi-axis controller (including the DirectInput side of XInput pads). Only the driver
// database is read; no device is opened.
bool HasIdPrefix(HDEVINFO set, SP_DEVINFO_DATA& dev, DWORD property) {
  wchar_t ids[2048];
  if (!SetupDiGetDeviceRegistryPropertyW(set, &dev, property, nullptr,
                                         reinterpret_cast<BYTE*>(ids), sizeof(ids) - 4, nullptr))
    return false;
  ids[2047] = ids[2046] = 0;
  for (const wchar_t* id = ids; *id; id += wcslen(id) + 1)
    if (_wcsnicmp(id, L"HID_DEVICE_SYSTEM_GAME", 22) == 0) return true;
  return false;
}

bool GameControllerPresent() {
  HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_HID, nullptr, nullptr,
                                      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (set == INVALID_HANDLE_VALUE) return true;  // can't tell: keep the normal scan
  bool found = false;
  SP_DEVINFO_DATA dev{sizeof(dev)};
  for (DWORD i = 0; !found && SetupDiEnumDeviceInfo(set, i, &dev); ++i)
    found = HasIdPrefix(set, dev, SPDRP_HARDWAREID) || HasIdPrefix(set, dev, SPDRP_COMPATIBLEIDS);
  SetupDiDestroyDeviceInfoList(set);
  return found;
}

bool IsControllerQuery(DWORD type) {
  BYTE kind = GET_DIDEVICE_TYPE(type);
  return kind == DI8DEVCLASS_GAMECTRL || (kind >= DI8DEVTYPE_JOYSTICK && kind <= DI8DEVTYPE_1STPERSON) ||
         kind == DI8DEVTYPE_SUPPLEMENTAL;
}

// Wraps the game's callback and ends the enumeration once every system device the query can
// return has been reported.
struct StopAfterSystemDevices {
  LPDIENUMDEVICESCALLBACKA callback;
  void* context;
  bool wantMouse, wantKeyboard;

  static BOOL CALLBACK Forward(const DIDEVICEINSTANCEA* device, void* self) {
    auto s = static_cast<StopAfterSystemDevices*>(self);
    if (s->callback(device, s->context) == DIENUM_STOP) return DIENUM_STOP;
    if (device->guidInstance == GUID_SysMouse) s->wantMouse = false;
    if (device->guidInstance == GUID_SysKeyboard) s->wantKeyboard = false;
    return (s->wantMouse || s->wantKeyboard) ? DIENUM_CONTINUE : DIENUM_STOP;
  }
};

}  // namespace

HRESULT FastEnumDevices(EnumDevices_t real, IDirectInput8A* di, DWORD type,
                        LPDIENUMDEVICESCALLBACKA callback, void* context, DWORD flags) {
  BYTE kind = GET_DIDEVICE_TYPE(type);
  bool mouse = kind == DI8DEVCLASS_ALL || kind == DI8DEVCLASS_POINTER || kind == DI8DEVTYPE_MOUSE;
  bool keyboard = kind == DI8DEVCLASS_ALL || kind == DI8DEVCLASS_KEYBOARD ||
                  kind == DI8DEVTYPE_KEYBOARD;
  bool controller = IsControllerQuery(type);

  if (!callback || (!mouse && !keyboard && !controller) || GameControllerPresent()) {
    LU_LOG("EnumDevices type=0x%lx flags=0x%lx: full scan", type, flags);
    return real(di, type, callback, context, flags);
  }
  if (controller) {
    LU_LOG("EnumDevices type=0x%lx flags=0x%lx: no game controller connected, skipped", type,
           flags);
    return DI_OK;
  }
  LU_LOG("EnumDevices type=0x%lx flags=0x%lx: no game controller connected, system devices only",
         type, flags);
  StopAfterSystemDevices stop{callback, context, mouse, keyboard};
  return real(di, type, &StopAfterSystemDevices::Forward, &stop, flags);
}

}  // namespace lu
