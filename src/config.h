#pragma once

#include <string>

namespace lu {

enum class DisplayMode { Borderless, Windowed, Fullscreen };

struct Config {
  // [Display]
  DisplayMode mode = DisplayMode::Borderless;
  int monitor = 0;  // 0 = primary, 1..N = display number as Windows lists them
  int windowWidth = 0;
  int windowHeight = 0;
  bool altEnter = true;
  bool rememberMode = true;
  bool keepVisibleOnFocusLoss = true;
  bool hideCursor = true;
  bool fixAspectRatio = true;

  // [Rendering]
  bool nativeResolution = true;
  int anisotropy = 16;
  int frameLimit = 60;  // 0 = off

  // [Input]
  bool fastDeviceScan = true;

  // [Audio]
  int audioChannels = 2;  // 0 = game default

  // [Troubleshooting]
  bool diagnostics = false;
  int testWidth = 0;  // TestResolution: desktop mode reported to the game (diagnostics)
  int testHeight = 0;

  std::wstring path;
};

extern Config g_config;

void LoadConfig(const std::wstring& path);
void SaveDisplayMode(DisplayMode mode);
const char* ToString(DisplayMode mode);

}  // namespace lu
