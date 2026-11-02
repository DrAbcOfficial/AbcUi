#pragma once

// framework::hooks - the engine entry-point table behind the arbitration
// dispatchers. One record per hooked command or user message; the record
// keeps the installed thunk, the displaced predecessor, the install
// generation and the dispatch/handled counters so contention with another
// plugin or a client-DLL reload is visible instead of silent.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace framework::hooks {

enum class Kind : int {
  kCommand = 0,
  kMessage = 1,
};

struct Entry {
  Kind kind = Kind::kCommand;
  std::string name;
  void *installed = nullptr;
  void *predecessor = nullptr;
  std::uint64_t generation = 0;
  std::uint32_t dispatches = 0;
  std::uint32_t handled = 0;
  std::uint64_t lastDispatch = 0;
  bool head = true;
};

// Registers or re-registers one entry. Re-registration bumps generation.
void add(Kind kind, const char *name, void *installed, void *predecessor);
void note(Kind kind, const char *name, bool handled);
void setHead(Kind kind, const char *name, bool head);
void remove(Kind kind, const char *name);

std::vector<Entry> snapshot();
std::string describe();

} // namespace framework::hooks
