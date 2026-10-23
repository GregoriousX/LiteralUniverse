#include "diag_tools.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "display.h"
#include "log.h"

namespace lu {

namespace {

struct Command {
  double time;
  enum { Key, Shot, Quit } type;
  BYTE key;
  BYTE vk;
  double hold;
  bool pressed;
  std::string name;
  bool done;
};

std::vector<Command> g_script;
std::wstring g_directory;
LARGE_INTEGER g_start{}, g_freq{};

double Now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return double(t.QuadPart - g_start.QuadPart) / double(g_freq.QuadPart);
}

// DirectInput scan code and virtual key of a key name.
bool KeyCode(const char* name, BYTE& scan, BYTE& vk) {
  static const struct {
    const char* name;
    BYTE scan, vk;
  } keys[] = {{"SPACE", 0x39, VK_SPACE}, {"RETURN", 0x1C, VK_RETURN}, {"ESCAPE", 0x01, VK_ESCAPE},
              {"LEFT", 0xCB, VK_LEFT},   {"RIGHT", 0xCD, VK_RIGHT},   {"UP", 0xC8, VK_UP},
              {"DOWN", 0xD0, VK_DOWN},   {"C", 0x2E, 'C'},            {"X", 0x2D, 'X'},
              {"Z", 0x2C, 'Z'}};
  for (const auto& k : keys) {
    if (_stricmp(k.name, name) == 0) {
      scan = k.scan;
      vk = k.vk;
      return true;
    }
  }
  return false;
}

// Window messages for a key, for game code that reads WM_KEYDOWN/WM_KEYUP.
void PostKey(const Command& c, bool down) {
  HWND window = GameWindow();
  if (!window) return;
  bool extended = c.key >= 0x80;
  LPARAM lp = 1 | (LPARAM(c.key & 0x7F) << 16) | (extended ? (1 << 24) : 0);
  if (!down) lp |= (1 << 30) | (1u << 31);
  PostMessageA(window, down ? WM_KEYDOWN : WM_KEYUP, c.vk, lp);
}

// Saves the back buffer as a 24-bit BMP, shrunk by a whole factor to at most 1600 pixels wide.
void SaveScreenshot(IDirect3DDevice9* device, const std::string& name) {
  IDirect3DSurface9* back = nullptr;
  if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) return;
  D3DSURFACE_DESC desc;
  back->GetDesc(&desc);
  IDirect3DSurface9* copy = nullptr;
  HRESULT hr = device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                   D3DPOOL_SYSTEMMEM, &copy, nullptr);
  if (SUCCEEDED(hr)) hr = device->GetRenderTargetData(back, copy);
  back->Release();
  D3DLOCKED_RECT lock;
  if (FAILED(hr) || FAILED(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) {
    LU_LOG("Script: screenshot %s failed (0x%08lx)", name.c_str(), hr);
    if (copy) copy->Release();
    return;
  }

  UINT factor = (desc.Width + 1599) / 1600;
  UINT w = desc.Width / factor, h = desc.Height / factor;
  UINT stride = (w * 3 + 3) & ~3u;
  std::vector<BYTE> pixels(size_t(stride) * h);
  for (UINT y = 0; y < h; ++y) {
    BYTE* out = &pixels[size_t(h - 1 - y) * stride];
    for (UINT x = 0; x < w; ++x) {
      unsigned sum[3] = {0, 0, 0};
      for (UINT sy = 0; sy < factor; ++sy) {
        auto row = static_cast<const BYTE*>(lock.pBits) + size_t(y * factor + sy) * lock.Pitch;
        for (UINT sx = 0; sx < factor; ++sx) {
          const BYTE* p = row + size_t(x * factor + sx) * 4;  // X8R8G8B8 / A8R8G8B8
          sum[0] += p[0];
          sum[1] += p[1];
          sum[2] += p[2];
        }
      }
      for (int c = 0; c < 3; ++c) out[x * 3 + c] = BYTE(sum[c] / (factor * factor));
    }
  }
  copy->UnlockRect();
  copy->Release();

  BITMAPFILEHEADER file{};
  BITMAPINFOHEADER info{};
  info.biSize = sizeof(info);
  info.biWidth = LONG(w);
  info.biHeight = LONG(h);
  info.biPlanes = 1;
  info.biBitCount = 24;
  file.bfType = 0x4D42;
  file.bfOffBits = sizeof(file) + sizeof(info);
  file.bfSize = file.bfOffBits + DWORD(pixels.size());

  std::wstring path = g_directory + L"LiteralUniverse_";
  path += std::wstring(name.begin(), name.end()) + L".bmp";
  FILE* f = _wfopen(path.c_str(), L"wb");
  if (!f) return;
  fwrite(&file, sizeof(file), 1, f);
  fwrite(&info, sizeof(info), 1, f);
  fwrite(pixels.data(), 1, pixels.size(), f);
  fclose(f);
  LU_LOG("Script: screenshot %s (%ux%u from %ux%u)", name.c_str(), w, h, desc.Width, desc.Height);
}

}  // namespace

void LoadScript(const wchar_t* directory) {
  QueryPerformanceFrequency(&g_freq);
  QueryPerformanceCounter(&g_start);
  if (!g_config.diagnostics) return;
  g_directory = directory;
  FILE* f = _wfopen((g_directory + L"LiteralUniverse.script").c_str(), L"r");
  if (!f) return;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    double time = 0, hold = 0.2;
    char command[32] = {}, arg[64] = {};
    if (line[0] == ';' || sscanf(line, "%lf %31s %63s %lf", &time, command, arg, &hold) < 2)
      continue;
    Command c{time, Command::Quit, 0, 0, hold, false, arg, false};
    if (_stricmp(command, "key") == 0) {
      c.type = Command::Key;
      if (!KeyCode(arg, c.key, c.vk)) continue;
    } else if (_stricmp(command, "shot") == 0) {
      c.type = Command::Shot;
    } else if (_stricmp(command, "quit") != 0) {
      continue;
    }
    g_script.push_back(c);
  }
  fclose(f);
  LU_LOG("Script: %zu commands loaded", g_script.size());
}

bool ScriptActive() {
  return !g_script.empty();
}

void ApplyScriptedKeys(BYTE* keys) {
  double now = Now();
  for (const Command& c : g_script)
    if (c.type == Command::Key && now >= c.time && now < c.time + c.hold) keys[c.key] = 0x80;
}

void ScriptFrame(IDirect3DDevice9* device) {
  if (g_script.empty()) return;
  double now = Now();
  for (Command& c : g_script) {
    if (c.done || now < c.time) continue;
    if (c.type == Command::Key && !c.pressed) {
      c.pressed = true;
      PostKey(c, true);
    }
    if (c.type == Command::Key && now < c.time + c.hold) continue;
    if (c.type == Command::Key) PostKey(c, false);
    c.done = true;
    if (c.type == Command::Key) LU_LOG("Script: key %02X released", c.key);
    if (c.type == Command::Shot) SaveScreenshot(device, c.name);
    if (c.type == Command::Quit) {
      LU_LOG("Script: quit");
      TerminateProcess(GetCurrentProcess(), 0);
    }
  }
}

}  // namespace lu
