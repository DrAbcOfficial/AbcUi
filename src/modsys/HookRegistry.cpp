// framework::hooks implementation - see HookRegistry.h.
#include "HookRegistry.h"

#include <format>
#include <map>
#include <sstream>
#include <utility>

namespace framework::hooks {
namespace {

using Key = std::pair<Kind, std::string>;

std::map<Key, Entry> &entries() {
  static std::map<Key, Entry> instance;
  return instance;
}

std::uint64_t NextDispatch = 1;

} // namespace

void add(Kind kind, const char *name, void *installed, void *predecessor) {
  if (!name || !*name)
    return;
  Key key{kind, name};
  auto found = entries().find(key);
  if (found == entries().end()) {
    Entry entry;
    entry.kind = kind;
    entry.name = name;
    entry.installed = installed;
    entry.predecessor = predecessor;
    entry.generation = 1;
    entry.head = true;
    entries().emplace(std::move(key), std::move(entry));
    return;
  }
  found->second.installed = installed;
  found->second.predecessor = predecessor;
  ++found->second.generation;
  found->second.head = true;
}

void note(Kind kind, const char *name, bool handled) {
  if (!name)
    return;
  const auto found = entries().find(Key{kind, name});
  if (found == entries().end())
    return;
  ++found->second.dispatches;
  if (handled)
    ++found->second.handled;
  found->second.lastDispatch = NextDispatch++;
}

void setHead(Kind kind, const char *name, bool head) {
  if (!name)
    return;
  const auto found = entries().find(Key{kind, name});
  if (found != entries().end())
    found->second.head = head;
}

void remove(Kind kind, const char *name) {
  if (name)
    entries().erase(Key{kind, name});
}

std::vector<Entry> snapshot() {
  std::vector<Entry> result;
  result.reserve(entries().size());
  for (const auto &[key, entry] : entries()) {
    (void)key;
    result.push_back(entry);
  }
  return result;
}

std::string describe() {
  std::ostringstream out;
  for (const Entry &entry : snapshot()) {
    out << (entry.kind == Kind::kCommand ? "command " : "message ")
        << entry.name << ": head=" << (entry.head ? "ours" : "other")
        << " generation=" << entry.generation
        << " dispatches=" << entry.dispatches << " handled=" << entry.handled;
    if (entry.lastDispatch)
      out << " last=" << entry.lastDispatch;
    out << '\n';
  }
  return out.str();
}

} // namespace framework::hooks
