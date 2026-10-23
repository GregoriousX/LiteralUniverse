/* DirectInput 8 vtable slot numbers, taken from the C declarations in dinput.h. */
#define CINTERFACE
#define DIRECTINPUT_VERSION 0x0800
#include <stddef.h>
#include <dinput.h>

#include "input_vtbl.h"

#define SLOT(vtbl, member) (unsigned)(offsetof(vtbl, member) / sizeof(void*))

const unsigned kDI_CreateDevice = SLOT(IDirectInput8AVtbl, CreateDevice);
const unsigned kDI_EnumDevices = SLOT(IDirectInput8AVtbl, EnumDevices);
const unsigned kDI_GetDeviceStatus = SLOT(IDirectInput8AVtbl, GetDeviceStatus);

const unsigned kDIDev_Acquire = SLOT(IDirectInputDevice8AVtbl, Acquire);
const unsigned kDIDev_GetDeviceState = SLOT(IDirectInputDevice8AVtbl, GetDeviceState);
const unsigned kDIDev_GetDeviceData = SLOT(IDirectInputDevice8AVtbl, GetDeviceData);
const unsigned kDIDev_Poll = SLOT(IDirectInputDevice8AVtbl, Poll);
