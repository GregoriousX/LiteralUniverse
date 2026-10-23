#include "call_trace.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "log.h"

namespace lu {

namespace {

// Classes whose methods are traced (substring match on the MSVC RTTI name).
const char* const kKeywords[] = {"Projectile", "Cannon", "Thrown", "Throw", "Bullet", "Ball",
                                 "Steer",      "Drag",   "Carry",  "Grab",  "Launch", "Target"};

struct Slot {
  void** entry;       // vtable slot
  void* original;     // function the slot pointed to
  const char* name;   // class name
  unsigned index;     // slot number
  LONG* counter;
};

std::vector<Slot> g_slots;
BYTE* g_thunks = nullptr;
LONG* g_counters = nullptr;
bool g_tracing = false;

struct Section {
  BYTE* begin;
  BYTE* end;
};

bool InSection(const Section& s, const void* p) {
  return p >= s.begin && p < s.end;
}

void FindSlots() {
  auto base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
      base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  Section text{}, image{base, base + nt->OptionalHeader.SizeOfImage};
  std::vector<Section> data;
  auto sec = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
    Section s{base + sec->VirtualAddress, base + sec->VirtualAddress + sec->Misc.VirtualSize};
    if (memcmp(sec->Name, ".text", 6) == 0) text = s;
    else if (memcmp(sec->Name, ".rdata", 7) == 0 || memcmp(sec->Name, ".data", 6) == 0)
      data.push_back(s);
  }

  // Complete object locators: signature 0, then a type descriptor whose name starts ".?AV".
  std::vector<std::pair<BYTE*, const char*>> locators;
  for (const Section& s : data) {
    for (BYTE* p = s.begin; p + 20 <= s.end; p += 4) {
      if (*reinterpret_cast<DWORD*>(p) != 0) continue;
      auto td = *reinterpret_cast<BYTE**>(p + 12);
      if (!InSection(image, td) || td + 16 > image.end) continue;
      auto name = reinterpret_cast<const char*>(td + 8);
      if (memcmp(name, ".?AV", 4) != 0) continue;
      bool match = false;
      for (const char* k : kKeywords) match = match || strstr(name, k);
      if (match) locators.push_back({p, name});
    }
  }
  std::sort(locators.begin(), locators.end());

  // A vtable is a pointer to a locator followed by code pointers.
  for (const Section& s : data) {
    for (BYTE* p = s.begin; p + 8 <= s.end; p += 4) {
      BYTE* col = *reinterpret_cast<BYTE**>(p);
      auto it = std::lower_bound(locators.begin(), locators.end(),
                                 std::pair<BYTE*, const char*>{col, nullptr});
      if (it == locators.end() || it->first != col) continue;
      auto vtable = reinterpret_cast<void**>(p + 4);
      for (unsigned i = 0; i < 64; ++i) {
        if (reinterpret_cast<BYTE*>(vtable + i) + 4 > s.end || !InSection(text, vtable[i])) break;
        g_slots.push_back(Slot{vtable + i, vtable[i], it->second, i, nullptr});
      }
    }
  }
}

void WriteSlot(void** entry, void* value) {
  DWORD old;
  VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old);
  *entry = value;
  VirtualProtect(entry, sizeof(void*), old, &old);
}

void Start() {
  if (g_slots.empty()) {
    FindSlots();
    if (g_slots.empty()) {
      LU_LOG("F12: no matching classes found");
      return;
    }
    // Each thunk: inc dword [counter] ; jmp original  (6 + 5 bytes, padded to 12)
    g_thunks = static_cast<BYTE*>(VirtualAlloc(nullptr, g_slots.size() * 12,
                                               MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    g_counters = static_cast<LONG*>(
        VirtualAlloc(nullptr, g_slots.size() * sizeof(LONG), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_thunks || !g_counters) return;
    for (size_t i = 0; i < g_slots.size(); ++i) {
      BYTE* t = g_thunks + i * 12;
      g_slots[i].counter = g_counters + i;
      t[0] = 0xFF;
      t[1] = 0x05;
      *reinterpret_cast<LONG**>(t + 2) = g_slots[i].counter;
      t[6] = 0xE9;
      *reinterpret_cast<int32_t*>(t + 7) =
          int32_t(static_cast<BYTE*>(g_slots[i].original) - (t + 11));
      t[11] = 0xCC;
    }
    FlushInstructionCache(GetCurrentProcess(), g_thunks, g_slots.size() * 12);
  }
  memset(g_counters, 0, g_slots.size() * sizeof(LONG));
  for (size_t i = 0; i < g_slots.size(); ++i) WriteSlot(g_slots[i].entry, g_thunks + i * 12);
  g_tracing = true;
  LU_LOG("F12: call trace started (%zu methods)", g_slots.size());
}

void Stop() {
  for (const Slot& s : g_slots) WriteSlot(s.entry, s.original);
  g_tracing = false;
  std::vector<const Slot*> hit;
  for (const Slot& s : g_slots)
    if (*s.counter) hit.push_back(&s);
  std::sort(hit.begin(), hit.end(), [](const Slot* a, const Slot* b) {
    return *a->counter > *b->counter;
  });
  LU_LOG("F12: call trace stopped, %zu methods were called", hit.size());
  for (const Slot* s : hit)
    LU_LOG("  %8ld  %s slot %u -> %p", *s->counter, s->name, s->index, s->original);
}

}  // namespace

void ToggleCallTrace() {
  if (g_tracing) Stop();
  else Start();
}

}  // namespace lu
