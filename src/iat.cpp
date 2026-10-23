#include "iat.h"

#include <cstring>

namespace lu {

namespace {

void WriteProtected(void** slot, void* value) {
  DWORD old;
  VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
  *slot = value;
  VirtualProtect(slot, sizeof(void*), old, &old);
  FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
}

// Calls `match` for each import of `dll`; patches the first slot it accepts.
template <typename Match>
void* PatchFirst(HMODULE module, const char* dll, void* replacement, Match match) {
  auto base = reinterpret_cast<BYTE*>(module);
  auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return nullptr;

  for (auto desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
       desc->Name; ++desc) {
    if (_stricmp(reinterpret_cast<char*>(base + desc->Name), dll) != 0) continue;
    if (!desc->OriginalFirstThunk) continue;

    auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
    auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++slots) {
      if (!match(base, names)) continue;

      auto slot = reinterpret_cast<void**>(&slots->u1.Function);
      void* previous = *slot;
      WriteProtected(slot, replacement);
      return previous;
    }
  }
  return nullptr;
}

}  // namespace

void* PatchImport(HMODULE module, const char* dll, const char* function, void* replacement) {
  return PatchFirst(module, dll, replacement, [&](BYTE* base, IMAGE_THUNK_DATA* t) {
    if (IMAGE_SNAP_BY_ORDINAL(t->u1.Ordinal)) return false;
    auto byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + t->u1.AddressOfData);
    return strcmp(byName->Name, function) == 0;
  });
}

void* PatchImportOrdinal(HMODULE module, const char* dll, WORD ordinal, void* replacement) {
  return PatchFirst(module, dll, replacement, [&](BYTE*, IMAGE_THUNK_DATA* t) {
    return IMAGE_SNAP_BY_ORDINAL(t->u1.Ordinal) && IMAGE_ORDINAL(t->u1.Ordinal) == ordinal;
  });
}

void* PatchVtable(void* object, unsigned index, void* replacement) {
  auto slot = &(*reinterpret_cast<void***>(object))[index];
  void* previous = *slot;
  if (previous != replacement) WriteProtected(slot, replacement);
  return previous;
}

}  // namespace lu
