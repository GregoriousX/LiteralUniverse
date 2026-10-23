#include <windows.h>

#include <string>

#include "audio_hooks.h"
#include "config.h"
#include "d3d9_hooks.h"
#include "diag_tools.h"
#include "display.h"
#include "input_hooks.h"
#include "log.h"
#include "game_patches.h"

namespace {

std::wstring ModuleDirectory(HMODULE module) {
  wchar_t path[MAX_PATH];
  DWORD n = GetModuleFileNameW(module, path, MAX_PATH);
  std::wstring s(path, n);
  return s.substr(0, s.find_last_of(L"\\/") + 1);
}

void Initialize(HMODULE self) {
  std::wstring dir = ModuleDirectory(self);
  lu::LoadConfig(dir + L"LiteralUniverse.ini");
  lu::log::Open((dir + L"LiteralUniverse.log").c_str());
  lu::LoadScript(dir.c_str());
  LU_LOG("LiteralUniverse " LU_VERSION " mode=%s monitor=%d diagnostics=%d",
         lu::ToString(lu::g_config.mode), lu::g_config.monitor, lu::g_config.diagnostics);

  HMODULE game = GetModuleHandleW(nullptr);
  if (lu::DetectGameBuild()) {
    if (lu::g_config.fixAspectRatio) lu::InstallAspectFix();
    lu::InstallFrameLimit(lu::g_config.frameLimit);
  }
  lu::InstallDisplayHooks(game);
  lu::InstallD3D9Hooks(game);
  lu::InstallInputHooks(game);
  lu::InstallAudioHooks(game);
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(inst);
    Initialize(inst);
  }
  return TRUE;
}
