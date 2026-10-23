#pragma once

#include <windows.h>

#include <d3d9.h>

namespace lu {

// Diagnostics: a timed script (LiteralUniverse.script next to the DLL) that presses keys through
// DirectInput and saves screenshots of the back buffer, so the game can be tested while it runs in
// the background. Each line is "<seconds since start> <command>":
//   12.5 key SPACE 0.2      press a key (SPACE, RETURN, ESCAPE, LEFT, RIGHT, UP, DOWN, C, X, Z)
//   30   shot worldselect   save LiteralUniverse_worldselect.bmp
//   45   quit               end the game
void LoadScript(const wchar_t* directory);
bool ScriptActive();

// Called with the 256-byte keyboard state the game reads; adds the scripted keys.
void ApplyScriptedKeys(BYTE* keys);

// Called once per presented frame, before Present.
void ScriptFrame(IDirect3DDevice9* device);

}  // namespace lu
