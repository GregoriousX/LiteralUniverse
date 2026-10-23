#include "game_patches.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "log.h"

namespace lu {

namespace {

// Byte pattern with wildcards. `mask` has 'x' for bytes that must match and '?' for any byte.
struct Pattern {
  const char* bytes;
  const char* mask;
};

std::vector<BYTE*> FindAll(const BYTE* begin, size_t size, const Pattern& p) {
  std::vector<BYTE*> hits;
  size_t len = strlen(p.mask);
  for (size_t i = 0; i + len <= size; ++i) {
    size_t j = 0;
    while (j < len && (p.mask[j] == '?' || begin[i + j] == BYTE(p.bytes[j]))) ++j;
    if (j == len) hits.push_back(const_cast<BYTE*>(begin + i));
  }
  return hits;
}

// Reads the absolute address stored in an instruction operand.
template <typename T>
T* AddressAt(const BYTE* p) {
  return *reinterpret_cast<T* const*>(p);
}

// Scene size: a static initializer stores the fixed 1280x720 render size that the renderer passes
// to its render-target setup at start and after every device reset.
//   mov byte [..], cl ; mov [W], 1280 ; mov [H], 720 ; mov [..], ecx
const Pattern kSceneSizeInit = {
    "\x88\x0D????\xC7\x05????\x00\x05\x00\x00\xC7\x05????\xD0\x02\x00\x00\x89\x0D",
    "xx????xx????xxxxxx????xxxxxx"};

// Frame limiter in the main loop: waits until last frame time + period has passed.
//   movsd xmm1, [period] ; mulsd xmm0, [..] ; addsd xmm1, [last] ; comisd xmm1, xmm0 ; ja
const Pattern kLimiter = {
    "\xF2\x0F\x10\x0D????\xF2\x0F\x59\x05????\xF2\x0F\x58\x0D????\x66\x0F\x2F\xC8\x77",
    "xxxx????xxxx????xxxx????xxxxx"};

// Pushing an object sideways (DragObject_Strafe) ramps its speed up once per frame:
//   movss xmm0, [edi+4] ; movss xmm1, [2.0] ; comiss ; jae ; movaps ; mulss xmm0, [eax+0x178]
//   movss [edi+4], xmm0
// At more than 30 FPS the ramp runs faster and pushed objects lose their weight. The multiply is
// replaced with one scaled by the real frame time.
const Pattern kDragRamp = {
    "\xF3\x0F\x10\x47\x04\xF3\x0F\x10\x0D????\x0F\x2F\xC1\x73\x03\x0F\x28\xC1"
    "\xF3\x0F\x59\x80\x78\x01\x00\x00\xF3\x0F\x11\x47\x04",
    "xxxxxxxxx????xxxxxxxxxxxxxxxxxxxxx"};
constexpr size_t kDragRampMul = 21;  // offset of the 8-byte mulss in kDragRamp

// The physics world (EXSgPhysicsWorldDiscreteNode) runs max(1, (dt + step/2) / step) fixed steps
// of [+0x250] seconds (1/60 by default) per frame and drops the remainder. Frames shorter than the
// step still run a whole step, so above 60 FPS physics objects move too fast (2x at 120 FPS).
// While frames are shorter than the level's step, the step is set to the frame period, so every
// frame runs exactly one step of the right length. The step count function starts:
//   push ebp ; mov ebp, esp ; movss xmm0, [ebp+8] ; push esi ; mov esi, ecx
//   cmp byte [esi+0x260], 0 ; movss xmm1, [esi+0x250] ; je
const Pattern kPhysicsStepCount = {
    "\x55\x8B\xEC\xF3\x0F\x10\x45\x08\x56\x8B\xF1"
    "\x80\xBE\x60\x02\x00\x00\x00\xF3\x0F\x10\x8E\x50\x02\x00\x00\x74",
    "xxxxxxxxxxxxxxxxxxxxxxxxxxx"};
constexpr size_t kPhysicsStepEntryLength = 8;  // push ebp ; mov ebp, esp ; movss xmm0, [ebp+8]

volatile float* g_designAspect = nullptr;  // aspect ratio the picture is laid out for (16:9)
volatile unsigned* g_sceneWidth = nullptr;
volatile unsigned* g_sceneHeight = nullptr;
volatile double* g_gamePeriod = nullptr;  // the game's frame period variable
std::vector<BYTE*> g_periodLoads;         // `movsd xmm0, [1/30]` before each store to it
double g_framePeriod = 1.0 / 30.0;
int g_configuredFps = 30;
const BYTE* g_code = nullptr;
size_t g_codeSize = 0;
BYTE* g_dragRampReturn = nullptr;
BYTE* g_stepEntryReturn = nullptr;

// Frame time between calls from one per-frame site, clamped to 1..100 ms.
struct FrameTimer {
  LARGE_INTEGER last{};
  double Seconds() {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    double dt = last.QuadPart ? double(now.QuadPart - last.QuadPart) / double(freq.QuadPart) : 0;
    last = now;
    if (dt <= 0 || dt > 0.1) dt = 1.0 / 30.0;  // first call of a new push, or after a pause
    if (dt < 0.001) dt = 0.001;
    return dt;
  }
};
FrameTimer g_dragTimer;

// v * k once per 1/30 s, whatever the frame rate.
extern "C" float __cdecl DragRampStep(float v, float k) {
  return float(v * std::pow(double(k), g_dragTimer.Seconds() * 30.0));
}

// Replaces `mulss xmm0, [eax+0x178]`; eax points to the drag settings, xmm0 holds the speed.
__declspec(naked) void DragRampHook() {
  __asm {
    push ecx
    push edx
    push eax
    sub esp, 8
    movss xmm1, dword ptr [eax + 0x178]
    movss dword ptr [esp + 4], xmm1
    movss dword ptr [esp], xmm0
    call DragRampStep
    fstp dword ptr [esp]
    movss xmm0, dword ptr [esp]
    add esp, 8
    pop eax
    pop edx
    pop ecx
    jmp [g_dragRampReturn]
  }
}

struct WorldStep {
  void* world;
  float original;  // the step the level set
  float written;   // the step LiteralUniverse last wrote
};
WorldStep g_worlds[16];

// Called with the physics world at the start of every step count.
extern "C" void __cdecl AdjustWorldStep(BYTE* world) {
  auto step = reinterpret_cast<float*>(world + 0x250);
  auto halfStep = reinterpret_cast<float*>(world + 0x24C);
  WorldStep* entry = nullptr;
  for (WorldStep& w : g_worlds) {
    if (w.world == world) {
      entry = &w;
      break;
    }
    if (!entry && !w.world) entry = &w;
  }
  if (!entry) return;
  if (entry->world != world || *step != entry->written) {
    entry->world = world;
    entry->original = *step;  // new world, or the level changed its step
  }
  float want = entry->original;
  if (g_framePeriod > 0 && g_framePeriod < want)
    want = float(g_framePeriod < 1.0 / 240 ? 1.0 / 240 : g_framePeriod);
  *step = want;
  *halfStep = want * 0.5f;
  entry->written = want;
}

// Replaces the first 8 bytes of the step count function (ecx = physics world).
__declspec(naked) void PhysicsStepEntryHook() {
  __asm {
    pushad
    push ecx
    call AdjustWorldStep
    add esp, 4
    popad
    push ebp
    mov ebp, esp
    movss xmm0, dword ptr [ebp + 8]
    jmp [g_stepEntryReturn]
  }
}

void WriteJump(BYTE* at, size_t length, void* target) {
  DWORD old;
  VirtualProtect(at, length, PAGE_EXECUTE_READWRITE, &old);
  at[0] = 0xE9;
  *reinterpret_cast<int32_t*>(at + 1) = int32_t(static_cast<BYTE*>(target) - (at + 5));
  for (size_t i = 5; i < length; ++i) at[i] = 0x90;
  VirtualProtect(at, length, old, &old);
  FlushInstructionCache(GetCurrentProcess(), at, length);
}

// Makes per-frame game code that assumes 30 FPS run at the same speed at higher frame rates.
void FixFrameDependentCode() {
  auto drag = FindAll(g_code, g_codeSize, kDragRamp);
  if (drag.size() == 1) {
    BYTE* mul = drag[0] + kDragRampMul;
    g_dragRampReturn = mul + 8;
    WriteJump(mul, 8, &DragRampHook);
    LU_LOG("Pushed-object speed ramp made frame-rate independent");
  } else {
    LU_LOG("Pushed-object speed ramp found %zu times: left unchanged", drag.size());
  }

  auto steps = FindAll(g_code, g_codeSize, kPhysicsStepCount);
  if (steps.size() == 1) {
    g_stepEntryReturn = steps[0] + kPhysicsStepEntryLength;
    WriteJump(steps[0], kPhysicsStepEntryLength, &PhysicsStepEntryHook);
    LU_LOG("Physics world step follows the frame period above 60 FPS");
  } else {
    LU_LOG("Physics world step code found %zu times: left unchanged", steps.size());
  }
}

void SetFramePeriod(int fps) {
  g_framePeriod = fps > 0 ? 1.0 / fps : 0.0;
  *g_gamePeriod = g_framePeriod;
}

// --- vertical field of view on screens narrower than 16:9 ---

// The render camera takes its vertical field of view (degrees, +0x200) from the game camera every
// frame, blended during camera transitions or copied:
//   movss xmm0, [eax+0x200] ; ... ; addss xmm0, xmm1 ; movss [edi+0x200], xmm0   (blend)
//   mov eax, [ecx+0x200] ; mov [edi+0x200], eax                                  (copy)
// Both stores are hooked so that narrower screens keep the horizontal view of 16:9.
const Pattern kFovBlend = {
    "\xF3\x0F\x10\x80\x00\x02\x00\x00\x0F\xB6\x87??\x00\x00\xF3\x0F\x5C\xC1\x50"
    "\xF3\x0F\x59\xC2\xF3\x0F\x58\xC1\xF3\x0F\x11\x87\x00\x02\x00\x00",
    "xxxxxxxxxxx??xxxxxxxxxxxxxxxxxxxxxxx"};
constexpr size_t kFovBlendStore = 28;  // movss [edi+0x200], xmm0 (8 bytes)
const Pattern kFovCopy = {"\x8B\x81\x00\x02\x00\x00\x89\x87\x00\x02\x00\x00", "xxxxxxxxxxxx"};
constexpr size_t kFovCopyStore = 6;  // mov [edi+0x200], eax (6 bytes)

constexpr float kDesignAspect = 16.0f / 9.0f;
float g_outputAspect = kDesignAspect;
bool g_vertPlus = false;
BYTE* g_fovBlendReturn = nullptr;
BYTE* g_fovCopyReturn = nullptr;

extern "C" float __cdecl AdjustVerticalFov(float degrees) {
  if (!g_vertPlus || g_outputAspect >= kDesignAspect - 0.001f || degrees <= 0 || degrees >= 179)
    return degrees;
  const double toRadians = 3.14159265358979 / 360.0;  // half angle
  double t = std::tan(degrees * toRadians) * kDesignAspect / g_outputAspect;
  return float(std::atan(t) / toRadians);
}

__declspec(naked) void FovBlendHook() {
  __asm {
    pushad
    sub esp, 32
    movups [esp], xmm1
    movups [esp + 16], xmm2
    sub esp, 4
    movss dword ptr [esp], xmm0
    call AdjustVerticalFov
    fstp dword ptr [esp]
    movss xmm0, dword ptr [esp]
    add esp, 4
    movups xmm1, [esp]
    movups xmm2, [esp + 16]
    add esp, 32
    popad
    movss dword ptr [edi + 0x200], xmm0
    jmp [g_fovBlendReturn]
  }
}

__declspec(naked) void FovCopyHook() {
  __asm {
    pushad
    sub esp, 48
    movups [esp], xmm0
    movups [esp + 16], xmm1
    movups [esp + 32], xmm2
    push eax
    call AdjustVerticalFov
    fstp dword ptr [esp]
    pop eax
    movups xmm0, [esp]
    movups xmm1, [esp + 16]
    movups xmm2, [esp + 32]
    add esp, 48
    mov [esp + 28], eax  // eax slot of pushad
    popad
    mov [edi + 0x200], eax
    jmp [g_fovCopyReturn]
  }
}

void InstallVerticalFovFix() {
  auto blend = FindAll(g_code, g_codeSize, kFovBlend);
  auto copy = FindAll(g_code, g_codeSize, kFovCopy);
  if (blend.size() != 1 || copy.size() != 1) {
    LU_LOG("Camera field of view code found %zu/%zu times: narrow screens keep the game's view",
           blend.size(), copy.size());
    return;
  }
  g_fovBlendReturn = blend[0] + kFovBlendStore + 8;
  g_fovCopyReturn = copy[0] + kFovCopyStore + 6;
  WriteJump(blend[0] + kFovBlendStore, 8, &FovBlendHook);
  WriteJump(copy[0] + kFovCopyStore, 6, &FovCopyHook);
  g_vertPlus = true;
  LU_LOG("Camera keeps the 16:9 horizontal view on narrower screens");
}

}  // namespace

bool DetectGameBuild() {
  auto base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
      base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  auto section = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
    if (memcmp(section->Name, ".text", 6) == 0) {
      g_code = base + section->VirtualAddress;
      g_codeSize = section->Misc.VirtualSize;
    }
  }
  if (!g_code) {
    LU_LOG("Game code section not found: render size and frame limit fixes disabled");
    return false;
  }

  auto scene = FindAll(g_code, g_codeSize, kSceneSizeInit);
  if (scene.size() == 1) {
    g_sceneWidth = AddressAt<volatile unsigned>(scene[0] + 8);
    g_sceneHeight = AddressAt<volatile unsigned>(scene[0] + 18);
    LU_LOG("Scene size variables at %p/%p", g_sceneWidth, g_sceneHeight);
    // The same initializer then stores the 16:9 design aspect: mov [aspect], 1.7777778f
    const char aspect[10] = {'\xC7', '\x05', 0, 0, 0, 0, '\x39', '\x8E', '\xE3', '\x3F'};
    auto store = FindAll(scene[0], 0x60, Pattern{aspect, "xx????xxxx"});
    if (store.size() == 1) {
      g_designAspect = AddressAt<volatile float>(store[0] + 2);
      LU_LOG("Design aspect variable at %p", g_designAspect);
    }
  } else {
    LU_LOG("Scene size code found %zu times: render size fix disabled", scene.size());
  }

  auto limiter = FindAll(g_code, g_codeSize, kLimiter);
  if (limiter.size() == 1) {
    const BYTE* periodOperand = limiter[0] + 4;
    g_gamePeriod = AddressAt<volatile double>(periodOperand);
    // Every place that sets the period: movsd xmm0, [constant] ; movsd [period], xmm0
    char bytes[16] = {'\xF2', '\x0F', '\x10', '\x05', 0, 0, 0, 0, '\xF2', '\x0F', '\x11', '\x05'};
    memcpy(bytes + 12, periodOperand, 4);
    for (BYTE* load : FindAll(g_code, g_codeSize, Pattern{bytes, "xxxx????xxxxxxxx"})) {
      double value = *AddressAt<const double>(load + 4);
      if (std::fabs(value - 1.0 / 30.0) < 1e-6) g_periodLoads.push_back(load);
    }
    LU_LOG("Frame period at %p, set in %zu places", g_gamePeriod, g_periodLoads.size());
  }
  if (limiter.size() != 1 || g_periodLoads.empty()) {
    LU_LOG("Frame limiter not found (%zu matches): frame limit option disabled", limiter.size());
    g_gamePeriod = nullptr;
    g_periodLoads.clear();
  }
  return g_sceneWidth || g_gamePeriod;
}

void SetSceneRenderSize(unsigned width, unsigned height) {
  if (!g_sceneWidth || !width || !height) return;
  if (*g_sceneWidth == width && *g_sceneHeight == height) return;
  LU_LOG("Scene render size %ux%u -> %ux%u", *g_sceneWidth, *g_sceneHeight, width, height);
  *g_sceneWidth = width;
  *g_sceneHeight = height;
}

void InstallAspectFix() {
  if (!g_designAspect) {
    LU_LOG("Design aspect not found: the game keeps its 16:9 picture");
    return;
  }
  InstallVerticalFovFix();
}

void SetOutputAspect(float aspect) {
  if (!g_designAspect || aspect <= 0) return;
  g_outputAspect = aspect;
  if (*g_designAspect == aspect) return;
  LU_LOG("Design aspect %.4f -> %.4f", *g_designAspect, aspect);
  *g_designAspect = aspect;
}

void InstallFrameLimit(int fps) {
  if (!g_gamePeriod || fps == 30) return;
  g_configuredFps = fps;

  // The 1/30 constant is shared with unrelated code, so the loads are redirected instead.
  for (BYTE* load : g_periodLoads) {
    auto operand = reinterpret_cast<double**>(load + 4);
    DWORD old;
    VirtualProtect(operand, sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    *operand = &g_framePeriod;
    VirtualProtect(operand, sizeof(void*), old, &old);
    FlushInstructionCache(GetCurrentProcess(), operand, sizeof(void*));
  }
  SetFramePeriod(fps);
  FixFrameDependentCode();
  if (fps > 0)
    LU_LOG("Frame limit %d FPS", fps);
  else
    LU_LOG("Frame limit off");
}

void ToggleFrameLimit() {
  if (!g_gamePeriod || g_configuredFps == 30) return;
  bool atConfigured = g_framePeriod != 1.0 / 30.0;
  SetFramePeriod(atConfigured ? 30 : g_configuredFps);
  if (atConfigured)
    LU_LOG("F9: frame limit 30 FPS");
  else
    LU_LOG("F9: frame limit %d FPS", g_configuredFps);
}

}  // namespace lu
