#pragma once

#include <windows.h>

namespace lu {

void InstallInputHooks(HMODULE game);

// Called by the dinput8 proxy with each IDirectInput8 object the game creates.
void OnDirectInputCreated(void* directInput);

}  // namespace lu
