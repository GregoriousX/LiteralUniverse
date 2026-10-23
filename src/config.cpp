#include "config.h"

#include <windows.h>

#include <cwchar>

namespace lu {

Config g_config;

namespace {

const char kDefaultIni[] =
    "; LiteralUniverse for Disney Universe (PC). Placed next to DisneyUPC.exe together with dinput8.dll.\r\n"
    "; Restart the game after changing anything here. 1 = on, 0 = off unless stated otherwise.\r\n"
    "\r\n"
    "[Display]\r\n"
    "; borderless, windowed or fullscreen. fullscreen keeps the game's own exclusive mode.\r\n"
    "Mode=borderless\r\n"
    "; 0 = primary display, 1, 2, ... = display number as listed in Windows display settings.\r\n"
    "Monitor=0\r\n"
    "; Client size of the window in windowed mode. 0 = automatic (fits the screen, keeps the aspect ratio).\r\n"
    "WindowWidth=0\r\n"
    "WindowHeight=0\r\n"
    "; Alt+Enter switches between borderless and windowed.\r\n"
    "AltEnter=1\r\n"
    "; Save Alt+Enter changes into this file.\r\n"
    "RememberMode=1\r\n"
    "; Keep the picture visible when Alt+Tab or another program takes focus.\r\n"
    "KeepVisibleOnFocusLoss=1\r\n"
    "; Hide the mouse cursor over the game window. The game is played with keyboard or controller.\r\n"
    "HideCursor=1\r\n"
    "; Fill screens that aren't 16:9: wider screens see more to the sides, narrower screens see more\r\n"
    "; above and below. The HUD stays in a centred 16:9 area. 0 = the game's 16:9 picture with bars.\r\n"
    "FixAspectRatio=1\r\n"
    "\r\n"
    "[Rendering]\r\n"
    "; Render the 3D scene at the full output resolution instead of the game's reduced internal size.\r\n"
    "NativeResolution=1\r\n"
    "; Frame rate limit: 30 = the game's own limit, 60 = recommended, up to 1000, 0 = off.\r\n"
    "; The game should run stably at 60 but can occasionally misbehave; above 60 more of it does.\r\n"
    "; F9 switches between 30 FPS and this value while playing.\r\n"
    "FrameLimit=60\r\n"
    "; Anisotropic texture filtering: 0 = game default, 2, 4, 8 or 16.\r\n"
    "AnisotropicFiltering=16\r\n"
    "\r\n"
    "[Input]\r\n"
    "; Skip DirectInput's scan of all USB input devices when no game controller is connected.\r\n"
    "; One slow device (often a USB audio device) otherwise delays game start by many seconds.\r\n"
    "FastDeviceScan=1\r\n"
    "\r\n"
    "[Audio]\r\n"
    "; Speaker channels for the game's sound mix. 2 = stereo, 6 = 5.1, 0 = let the game choose.\r\n"
    "; The game asks for the device's speaker setup, and with a 7.1 setup it overdrives one channel,\r\n"
    "; which comes out as heavy distortion after Windows mixes it down.\r\n"
    "Channels=2\r\n"
    "\r\n"
    "[Troubleshooting]\r\n"
    "; Write a detailed LiteralUniverse.log (window and Direct3D activity).\r\n"
    "Diagnostics=0\r\n";

int ReadInt(const wchar_t* section, const wchar_t* key, int fallback) {
  return int(GetPrivateProfileIntW(section, key, fallback, g_config.path.c_str()));
}

std::wstring ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
  wchar_t buf[128];
  GetPrivateProfileStringW(section, key, fallback, buf, 128, g_config.path.c_str());
  return buf;
}

DisplayMode ParseMode(const std::wstring& s) {
  if (_wcsicmp(s.c_str(), L"windowed") == 0) return DisplayMode::Windowed;
  if (_wcsicmp(s.c_str(), L"fullscreen") == 0) return DisplayMode::Fullscreen;
  return DisplayMode::Borderless;
}

}  // namespace

void LoadConfig(const std::wstring& path) {
  g_config.path = path;
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      DWORD written;
      WriteFile(f, kDefaultIni, sizeof(kDefaultIni) - 1, &written, nullptr);
      CloseHandle(f);
    }
  }

  Config& c = g_config;
  c.mode = ParseMode(ReadString(L"Display", L"Mode", L"borderless"));
  c.monitor = ReadInt(L"Display", L"Monitor", 0);
  c.windowWidth = ReadInt(L"Display", L"WindowWidth", 0);
  c.windowHeight = ReadInt(L"Display", L"WindowHeight", 0);
  c.altEnter = ReadInt(L"Display", L"AltEnter", 1) != 0;
  c.rememberMode = ReadInt(L"Display", L"RememberMode", 1) != 0;
  c.keepVisibleOnFocusLoss = ReadInt(L"Display", L"KeepVisibleOnFocusLoss", 1) != 0;
  c.hideCursor = ReadInt(L"Display", L"HideCursor", 1) != 0;
  c.fixAspectRatio = ReadInt(L"Display", L"FixAspectRatio", 1) != 0;

  c.nativeResolution = ReadInt(L"Rendering", L"NativeResolution", 1) != 0;
  c.anisotropy = ReadInt(L"Rendering", L"AnisotropicFiltering", 16);
  if (c.anisotropy < 0) c.anisotropy = 0;
  if (c.anisotropy > 16) c.anisotropy = 16;

  c.frameLimit = ReadInt(L"Rendering", L"FrameLimit", 60);
  if (c.frameLimit < 0) c.frameLimit = 30;
  if (c.frameLimit > 0 && c.frameLimit < 30) c.frameLimit = 30;
  if (c.frameLimit > 1000) c.frameLimit = 1000;

  c.fastDeviceScan = ReadInt(L"Input", L"FastDeviceScan", 1) != 0;

  c.audioChannels = ReadInt(L"Audio", L"Channels", 2);
  if (c.audioChannels < 0 || c.audioChannels > 8) c.audioChannels = 2;

  c.diagnostics = ReadInt(L"Troubleshooting", L"Diagnostics", 0) != 0;
  std::wstring test = ReadString(L"Troubleshooting", L"TestResolution", L"");
  if (swscanf(test.c_str(), L"%dx%d", &c.testWidth, &c.testHeight) != 2) c.testWidth = c.testHeight = 0;
}

void SaveDisplayMode(DisplayMode mode) {
  if (!g_config.rememberMode) return;
  wchar_t value[16];
  MultiByteToWideChar(CP_ACP, 0, ToString(mode), -1, value, 16);
  WritePrivateProfileStringW(L"Display", L"Mode", value, g_config.path.c_str());
}

const char* ToString(DisplayMode mode) {
  switch (mode) {
    case DisplayMode::Windowed: return "windowed";
    case DisplayMode::Fullscreen: return "fullscreen";
    default: return "borderless";
  }
}

}  // namespace lu
