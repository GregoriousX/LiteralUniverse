#include "audio_hooks.h"

#include <objbase.h>

#include <cmath>
#include <cstdio>

#include "config.h"
#include "diag_tools.h"
#include "iat.h"
#include "log.h"

namespace lu {

namespace {

// --- XAudio2 2.7 (DirectX SDK June 2010) declarations ---
// The Windows SDK's xaudio2.h describes XAudio2 2.9, whose vtables differ, so only the pieces
// used here are declared, with their 2.7 slot numbers.

constexpr CLSID kCLSID_XAudio2_7 = {0x5a508685, 0xa254, 0x4fba, {0x9b, 0x82, 0x9a, 0x24, 0xb0, 0x03, 0x06, 0xaf}};
constexpr CLSID kCLSID_XAudio2_7_Debug = {0xdb05ea35, 0x0329, 0x4d4b, {0xa5, 0x3a, 0x6d, 0xea, 0xd0, 0x3d, 0x38, 0x52}};
constexpr CLSID kCLSID_AudioVolumeMeter_2_7 = {0xcac1105f, 0x619b, 0x4d04, {0x83, 0x1a, 0x44, 0xe1, 0xcb, 0xf1, 0x2d, 0x57}};

constexpr unsigned kXA_CreateMasteringVoice = 10;
constexpr unsigned kXA_GetPerformanceData = 14;
constexpr unsigned kVoice_GetVoiceDetails = 0;
constexpr unsigned kVoice_SetEffectChain = 2;
constexpr unsigned kVoice_GetEffectParameters = 7;

struct VoiceDetails {
  UINT32 creationFlags;
  UINT32 inputChannels;
  UINT32 inputSampleRate;
};

struct EffectDescriptor {
  IUnknown* effect;
  BOOL initialState;
  UINT32 outputChannels;
};

struct EffectChain {
  UINT32 effectCount;
  const EffectDescriptor* effects;
};

struct VolumeMeterLevels {
  float* peakLevels;
  float* rmsLevels;
  UINT32 channelCount;
};

struct PerformanceData {
  UINT64 audioCyclesSinceLastQuery;
  UINT64 totalCyclesSinceLastQuery;
  UINT32 minimumCyclesPerQuantum;
  UINT32 maximumCyclesPerQuantum;
  UINT32 memoryUsageInBytes;
  UINT32 currentLatencyInSamples;
  UINT32 glitchesSinceEngineStarted;
  UINT32 activeSourceVoiceCount;
  UINT32 totalSourceVoiceCount;
  UINT32 activeSubmixVoiceCount;
  UINT32 activeResamplerCount;
  UINT32 activeMatrixMixCount;
  UINT32 activeXmaSourceVoices;
  UINT32 activeXmaStreams;
};

template <typename Fn>
Fn Method(void* object, unsigned slot) {
  return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object))[slot]);
}

// --- hooks ---

using CoCreateInstance_t = HRESULT(WINAPI*)(REFCLSID, IUnknown*, DWORD, REFIID, void**);
using CreateMasteringVoice_t = HRESULT(STDMETHODCALLTYPE*)(void*, void**, UINT32, UINT32, UINT32,
                                                           UINT32, const EffectChain*);
CoCreateInstance_t o_CoCreateInstance = CoCreateInstance;
CreateMasteringVoice_t o_CreateMasteringVoice;

void* g_engine = nullptr;  // IXAudio2
void* g_master = nullptr;  // IXAudio2MasteringVoice
UINT32 g_channels = 0;
bool g_meter = false;

LARGE_INTEGER g_freq{}, g_lastReport{};
UINT32 g_lastGlitches = 0;
float g_peak = 0;            // highest master peak since the last report
float g_channelPeak[8] = {};  // the same per output channel
unsigned g_clippedFrames = 0;  // frames whose master peak reached full scale
unsigned g_frames = 0;

// IXAudio2 2.7 GetDeviceCount (slot 3) and GetDeviceDetails (slot 4). XAUDIO2_DEVICE_DETAILS is
// DeviceID[256], DisplayName[256], Role, then a WAVEFORMATEXTENSIBLE (1068 bytes in total).
void LogDevices(void* engine) {
  UINT32 count = 0;
  Method<HRESULT(STDMETHODCALLTYPE*)(void*, UINT32*)>(engine, 3)(engine, &count);
  for (UINT32 i = 0; i < count && i < 16; ++i) {
    alignas(8) BYTE details[1068] = {};
    if (FAILED(Method<HRESULT(STDMETHODCALLTYPE*)(void*, UINT32, void*)>(engine, 4)(engine, i, details)))
      continue;
    const BYTE* fmt = details + 1028;
    LU_LOG("Audio device %u: %ls  role=0x%x  %u ch  %u Hz  %u bit  mask=0x%lx", i,
           reinterpret_cast<const wchar_t*>(details + 512),
           *reinterpret_cast<const UINT32*>(details + 1024), *reinterpret_cast<const WORD*>(fmt + 2),
           *reinterpret_cast<const DWORD*>(fmt + 4), *reinterpret_cast<const WORD*>(fmt + 14),
           *reinterpret_cast<const DWORD*>(fmt + 20));
  }
}

void AttachVolumeMeter() {
  if (!g_config.diagnostics || !g_master || g_channels == 0 || g_channels > 8) return;
  IUnknown* meter = nullptr;
  if (FAILED(o_CoCreateInstance(kCLSID_AudioVolumeMeter_2_7, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IUnknown, reinterpret_cast<void**>(&meter)))) {
    LU_LOG("Audio: volume meter not available");
    return;
  }
  EffectDescriptor desc{meter, TRUE, g_channels};
  EffectChain chain{1, &desc};
  HRESULT hr = Method<HRESULT(STDMETHODCALLTYPE*)(void*, const EffectChain*)>(
      g_master, kVoice_SetEffectChain)(g_master, &chain);
  meter->Release();  // the voice keeps its own reference
  g_meter = SUCCEEDED(hr);
  LU_LOG("Audio: master volume meter %s (0x%08lx)", g_meter ? "attached" : "failed", hr);
}

HRESULT STDMETHODCALLTYPE Hook_CreateMasteringVoice(void* engine, void** voice, UINT32 channels,
                                                    UINT32 rate, UINT32 flags, UINT32 device,
                                                    const EffectChain* chain) {
  if (g_config.diagnostics) LogDevices(engine);
  // The game asks for the device's speaker layout (0). With 7.1 it overdrives a channel it doesn't
  // know about, so the mix uses the configured channel count instead.
  if (channels == 0 && g_config.audioChannels > 0) channels = UINT32(g_config.audioChannels);
  HRESULT hr = o_CreateMasteringVoice(engine, voice, channels, rate, flags, device, chain);
  if (SUCCEEDED(hr) && voice && *voice) {
    g_engine = engine;
    g_master = *voice;
    VoiceDetails details{};
    Method<void(STDMETHODCALLTYPE*)(void*, VoiceDetails*)>(g_master, kVoice_GetVoiceDetails)(
        g_master, &details);
    g_channels = details.inputChannels;
    LU_LOG("Audio: mastering voice %u ch %u Hz (requested %u ch %u Hz, device %u, %u effects)",
           details.inputChannels, details.inputSampleRate, channels, rate, device,
           chain ? chain->effectCount : 0);
    if (!chain || chain->effectCount == 0) AttachVolumeMeter();
    if (ScriptActive()) {
      // Scripted test runs are silent: IXAudio2Voice::SetVolume (slot 12).
      Method<HRESULT(STDMETHODCALLTYPE*)(void*, float, UINT32)>(g_master, 12)(g_master, 0.0f, 0);
      LU_LOG("Audio: muted for the scripted test run");
    }
  } else {
    LU_LOG("Audio: CreateMasteringVoice failed 0x%08lx", hr);
  }
  return hr;
}

HRESULT WINAPI Hook_CoCreateInstance(REFCLSID clsid, IUnknown* outer, DWORD context, REFIID iid,
                                     void** out) {
  HRESULT hr = o_CoCreateInstance(clsid, outer, context, iid, out);
  bool xaudio = IsEqualCLSID(clsid, kCLSID_XAudio2_7) || IsEqualCLSID(clsid, kCLSID_XAudio2_7_Debug);
  if (g_config.diagnostics) {
    wchar_t name[64];
    StringFromGUID2(clsid, name, 64);
    LU_LOG("CoCreateInstance %ls -> 0x%08lx%s", name, hr, xaudio ? " (XAudio2 2.7)" : "");
  }
  if (xaudio && SUCCEEDED(hr) && out && *out)
    HookVtable(*out, kXA_CreateMasteringVoice, &Hook_CreateMasteringVoice, o_CreateMasteringVoice);
  return hr;
}

}  // namespace

void InstallAudioHooks(HMODULE game) {
  if (void* p = PatchImport(game, "ole32.dll", "CoCreateInstance",
                            reinterpret_cast<void*>(&Hook_CoCreateInstance)))
    o_CoCreateInstance = reinterpret_cast<CoCreateInstance_t>(p);
}

void AudioDiagnosticsTick() {
  if (!g_config.diagnostics || !g_engine) return;
  if (!g_freq.QuadPart) {
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_lastReport);
  }

  if (g_meter) {
    float peaks[8] = {}, rms[8] = {};
    VolumeMeterLevels levels{peaks, rms, g_channels};
    Method<HRESULT(STDMETHODCALLTYPE*)(void*, UINT32, void*, UINT32)>(
        g_master, kVoice_GetEffectParameters)(g_master, 0, &levels, sizeof(levels));
    float peak = 0;
    for (UINT32 c = 0; c < g_channels; ++c) {
      if (peaks[c] > peak) peak = peaks[c];
      if (peaks[c] > g_channelPeak[c]) g_channelPeak[c] = peaks[c];
    }
    if (peak > g_peak) g_peak = peak;
    if (peak >= 0.999f) ++g_clippedFrames;
  }
  ++g_frames;

  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (now.QuadPart - g_lastReport.QuadPart < g_freq.QuadPart * 5) return;
  g_lastReport = now;

  PerformanceData perf{};
  Method<void(STDMETHODCALLTYPE*)(void*, PerformanceData*)>(g_engine, kXA_GetPerformanceData)(
      g_engine, &perf);
  double cpu = perf.totalCyclesSinceLastQuery
                   ? 100.0 * double(perf.audioCyclesSinceLastQuery) / double(perf.totalCyclesSinceLastQuery)
                   : 0;
  LU_LOG("Audio: glitches +%u (total %u)  peak=%.2f (%.1f dBFS)  clipped frames=%u/%u  "
         "voices %u/%u  latency=%u samples  cpu=%.1f%%",
         perf.glitchesSinceEngineStarted - g_lastGlitches, perf.glitchesSinceEngineStarted, g_peak,
         g_peak > 0 ? 20.0 * log10(double(g_peak)) : -99.0, g_clippedFrames, g_frames,
         perf.activeSourceVoiceCount, perf.totalSourceVoiceCount, perf.currentLatencyInSamples,
         cpu);
  char line[160];
  int len = 0;
  for (UINT32 c = 0; c < g_channels && c < 8; ++c) {
    len += snprintf(line + len, sizeof(line) - len, "%.2f ", g_channelPeak[c]);
    g_channelPeak[c] = 0;
  }
  if (g_meter) LU_LOG("  channel peaks: %s", line);
  g_lastGlitches = perf.glitchesSinceEngineStarted;
  g_peak = 0;
  g_clippedFrames = 0;
  g_frames = 0;
}

}  // namespace lu
