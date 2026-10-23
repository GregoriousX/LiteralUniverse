#pragma once

#include <windows.h>

namespace lu {

// Replaces an import of `module` in its Import Address Table. Returns the
// previous target, or nullptr when the module does not import that function.
void* PatchImport(HMODULE module, const char* dll, const char* function, void* replacement);

// Same for a function imported by ordinal.
void* PatchImportOrdinal(HMODULE module, const char* dll, WORD ordinal, void* replacement);

// Overwrites one slot of a COM vtable and returns the previous entry.
void* PatchVtable(void* object, unsigned index, void* replacement);

// Hooks a vtable slot once. Objects sharing a vtable that is already hooked keep `original`.
template <typename T>
void HookVtable(void* object, unsigned index, T replacement, T& original) {
  void* previous = PatchVtable(object, index, reinterpret_cast<void*>(replacement));
  if (previous != reinterpret_cast<void*>(replacement)) original = reinterpret_cast<T>(previous);
}

}  // namespace lu
