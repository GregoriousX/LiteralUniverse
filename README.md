# LiteralUniverse

Display fixes for the PC version of **Disney Universe** (2011, Steam).

The game's 3D scene is always rendered at 1280×720 and then stretched to the selected resolution, which makes 1440p and 4K look blurry. On top of that, the game only offers exclusive fullscreen or a small fixed-size window. LiteralUniverse fixes both and adds a few other basic display settings.

## Features

- **Native render resolution.** The scene, depth buffer and post-processing chain are rendered at the selected resolution (for example a true 3840×2160) instead of an upscaled 1280×720.
- **Borderless fullscreen** (default), **windowed** or the game's own **exclusive fullscreen**.
- **Alt+Enter** switches between borderless and windowed. The choice is saved to the ini.
- **Resizable window.** It keeps the picture's aspect ratio and can be maximized. It is sized to fit the screen on first use.
- **Ultrawide and 16:10 support.** On screens that aren't 16:9 the game normally shows a 16:9 picture with black bars. With LiteralUniverse the 3D view fills the screen: ultrawide screens (21:9, 32:9) see more to the sides, and narrower screens (16:10, 4:3) keep the 16:9 width and see more above and below. The HUD and menus stay in a centred 16:9 area, so they aren't stretched or pushed to the far edges.
- **Correct aspect ratio.** A resolution with a different shape than the screen (for example 2560×1600 on a 16:9 monitor) is letterboxed or pillarboxed instead of stretched.
- **High-DPI aware.** Sizes are correct on 4K screens with Windows scaling at 150% and above. The `HIGHDPIAWARE` compatibility flag is no longer needed.
- **Monitor selection** for borderless and windowed mode.
- **Stays visible on Alt+Tab.** The picture isn't minimized and the cursor is never confined while another program has focus.
- **60 FPS.** The game's own limit is 30. LiteralUniverse raises it to 60 by default. Pushed objects and the physics of objects in the world are adjusted to keep their 30 FPS speed. The game should run stably at 60 FPS but can occasionally misbehave. **F9** switches between 30 FPS and the configured limit while playing, so any section that misbehaves can be played at 30.
- **Fast start.** At start DirectInput normally waits on every USB input device. One device that doesn't answer (often a USB audio device) can delay the game by 10 seconds or more. When no game controller is connected, this scan is skipped.
- **Clean audio on 7.1 outputs.** The game asks for the sound device's speaker layout. With a 7.1 setup (common for S/PDIF and many USB DACs) it drives one channel at up to 25 dB over full scale, which comes out as heavy distortion. The mix is created as stereo instead (configurable).
- **16× anisotropic filtering** (configurable).
- Hides the mouse cursor over the game window (the game is played with keyboard or controller).

## Installation

1. Download `dinput8.dll` from the [Releases](../../releases) page.
2. Copy it into the game folder, next to `DisneyUPC.exe`
   (for example `C:\Program Files (x86)\Steam\steamapps\common\Disney Universe`).
3. Start the game. `LiteralUniverse.ini` is created next to the DLL on first launch.
4. For the sharpest picture, choose your monitor's native resolution in
   **Options → Visual Settings → Resolution**.

No ASI loader or other tools are needed.

To uninstall, delete `dinput8.dll`, `LiteralUniverse.ini` and `LiteralUniverse.log` from the game folder.

## Configuration

`LiteralUniverse.ini`. Restart the game after editing it.

| Section | Key | Default | Description |
|---|---|---|---|
| Display | `Mode` | `borderless` | `borderless`, `windowed` or `fullscreen` (the game's exclusive mode). |
| Display | `Monitor` | `0` | `0` = primary display, `1`, `2`, … = display number as in Windows display settings. Applies to borderless and windowed. |
| Display | `WindowWidth`, `WindowHeight` | `0` | Client size in windowed mode. `0` = automatic. |
| Display | `AltEnter` | `1` | Alt+Enter switches between borderless and windowed. |
| Display | `RememberMode` | `1` | Save Alt+Enter changes to the ini. |
| Display | `KeepVisibleOnFocusLoss` | `1` | Don't minimize the game when it loses focus. |
| Display | `HideCursor` | `1` | Hide the mouse cursor over the game window. |
| Display | `FixAspectRatio` | `1` | Fill screens that aren't 16:9 (see Features). `0` = the game's own 16:9 picture with black bars. |
| Rendering | `NativeResolution` | `1` | Render the 3D scene at the selected resolution instead of 1280×720. |
| Rendering | `FrameLimit` | `60` | Frame rate limit: `60` = recommended, `30` = the game's own limit, up to `1000`, or `0` = off (VSync still applies). Above 60 more of the game misbehaves. |
| Rendering | `AnisotropicFiltering` | `16` | `0` = game default, or `2`, `4`, `8`, `16`. |
| Input | `FastDeviceScan` | `1` | Skip DirectInput's scan of all USB input devices when no game controller is connected. |
| Audio | `Channels` | `2` | Speaker channels for the game's sound mix: `2` = stereo, `6` = 5.1, `0` = the game's own choice. |
| Troubleshooting | `Diagnostics` | `0` | Write frame times, input and audio statistics and render-target details to `LiteralUniverse.log`. The previous session's log is kept as `LiteralUniverse.prev.log`. With diagnostics on, F12 starts and stops a trace of which projectile, cannon, throw and drag methods the game calls. |

## Compatibility

- Works with the Steam release of `DisneyUPC.exe`. Tested with the English and Polish executables, which are different builds.
- The render-resolution and frame-limit fixes find the code they change by byte pattern. If that code isn't found exactly once, the fix is disabled (the log says so) and the game's memory is left alone. The window, display and input features still work.
- Tested on Windows 11 with a 4K 240 Hz monitor at 150% scaling and a 1440p monitor at 100%.

## Known limitations

- The game was made for 30 FPS. At 60 it should run stably, but a section can occasionally misbehave; press F9 to play it at 30. Above 60 FPS more of the game misbehaves; for example, cannon shots in London miss at 120 FPS.
- On screens that aren't 16:9, cutscenes and some loading screens keep their 16:9 frame.
- The game doesn't remember the selected resolution between sessions. At start it uses the primary display's desktop resolution.
- In exclusive `fullscreen` mode the game uses only the primary display.

## How it works

`dinput8.dll` is a proxy for the system DirectInput 8 library, which the game loads at start. DirectInput calls are forwarded to the system library. The DLL also:

- answers DirectInput's device enumeration without the full USB scan when Windows lists no game controller. It reports the system keyboard and mouse, which is all the game uses;
- hooks `CoCreateInstance` to reach the game's XAudio2 2.7 engine and creates the mastering voice with the configured channel count;
- redirects the two places where the game sets its frame period to 1/30 s to the configured period, scales the per-frame speed ramp of pushed objects by the real frame time, and shortens the physics world's fixed step when frames get shorter than it;
- hooks `Direct3DCreate9` in the game's import table and then `CreateDevice`, `Reset`, `Present` and `SetSamplerState` on the Direct3D 9 device;
- before each device creation or reset, sets the engine's scene-size variables (fixed at 1280×720 by a static initializer) to the back-buffer size, so the engine rebuilds its full-, half- and quarter-resolution render targets at the real resolution;
- sets the engine's design aspect ratio (fixed at 16:9) to the screen's, so render windows stop letterboxing and the camera derives a wider horizontal view, while the HUD keeps its 16:9 layout; on narrower screens it widens the render camera's vertical field of view to keep the 16:9 horizontal view;
- in borderless and windowed mode, turns the game's fullscreen device into a windowed device, takes over the window's style and placement, and presents with `D3DSWAPEFFECT_COPY` and a destination rectangle to keep the aspect ratio;
- subclasses the game window for Alt+Enter, resizing, DPI changes and the cursor.

## Building

Requirements: Visual Studio 2022 or newer with the C++ desktop workload (MSVC, Windows SDK) and CMake 3.20+.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

The output is `build/Release/dinput8.dll`. The game is 32-bit, so `-A Win32` is required.

## Related

Inspired by LiteralCrab for Disney Infinity. Disney Universe runs on a different engine (Eurocom EngineXT), so LiteralUniverse is a separate implementation.

Disney Universe is © Disney. This project is not affiliated with or endorsed by Disney or Eurocom.
