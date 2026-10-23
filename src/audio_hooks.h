#pragma once

#include <windows.h>

namespace lu {

// Hooks CoCreateInstance in the game's imports to find its XAudio2 2.7 engine.
void InstallAudioHooks(HMODULE game);

// Diagnostics, called once per presented frame: samples the master output level and writes
// audio engine statistics (glitches, peaks, voices) to the log every 5 seconds.
void AudioDiagnosticsTick();

}  // namespace lu
