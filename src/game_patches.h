#pragma once

namespace lu {

// Finds the scene-size and frame-limiter code by byte pattern, so any language build of
// DisneyUPC.exe works. A fix whose code isn't found exactly once is disabled and its game memory
// is never touched. Returns false when neither was found.
bool DetectGameBuild();

// Sets the size the engine renders the 3D scene and its post-processing chain at. The engine
// ships with this fixed at 1280x720 regardless of the selected resolution.
void SetSceneRenderSize(unsigned width, unsigned height);

// Fills screens that aren't 16:9: wider screens see more to the sides, narrower screens see more
// above and below. Call once after DetectGameBuild.
void InstallAspectFix();

// Called with the back buffer's aspect ratio before every device creation or reset, when the
// aspect fix is installed. The game otherwise lays its picture out for 16:9.
void SetOutputAspect(float aspect);

// Replaces the game's 30 FPS frame limit. 0 = no limit (only VSync limits the rate). Also makes
// the known frame-rate dependent game code follow the real frame time.
void InstallFrameLimit(int fps);

// F9: switches between 30 FPS and the configured frame limit while the game runs.
void ToggleFrameLimit();

}  // namespace lu
