// framework::Localization implementation - see Localization.h.
#include "Localization.h"
#include "ResourceFile.h"
#include "TextUtil.h"

#include <abc/Console.h>

#include "Language.h"
#include "../modsys/ModuleVfs.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <map>
#include <unordered_map>
#include <vector>
#include <string_view>

namespace framework {

namespace {
std::unordered_map<std::string, std::string> Table;

using TranslationTable = std::unordered_map<std::string, std::string>;
std::map<std::string, TranslationTable> &moduleTables() {
  static std::map<std::string, TranslationTable> instance;
  return instance;
}

// "key = value" lines; '#' starts a comment. Values keep interior spaces.
void parseTable(std::span<const std::uint8_t> bytes, TranslationTable &table,
                const std::string &prefix = {}) {
  std::string line;
  auto parseLine = [&]() {
    std::string key, value;
    if (TextUtil::parseTranslationLine(line, key, value) &&
        (prefix.empty() || key.starts_with(prefix)))
      table[key] = value;
    line.clear();
  };
  for (const std::uint8_t byte : bytes) {
    const unsigned char ch = byte;
    if (ch == '\n')
      parseLine();
    else if (line.size() < 65536)
      line.push_back(static_cast<char>(ch));
  }
  parseLine();
}

void parseTableFile(const std::string &path) {
  std::vector<unsigned char> bytes;
  if (ResourceFile::read(path, bytes, 1024 * 1024))
    parseTable(bytes, Table);
}

void parseModuleTable(const std::string &id, const std::string &language) {
  PackageEntry entry;
  const std::string path = "lang/" + language + ".txt";
  if (ModuleVfs::openPath(id.c_str(), path.c_str(), entry, nullptr) &&
      entry.size <= 1024 * 1024)
    parseTable({static_cast<const std::uint8_t *>(entry.data), entry.size},
               moduleTables()[id], id + "/");
}
} // namespace

void Localization::load() {
  Table.clear();
  moduleTables().clear();

  const char *language = CurrentLanguage();
  if (!language || !*language)
    language = "english";

  // Sven Co-op reports "schinese"; keep a translation-free alias chain so a
  // missing language file degrades to english instead of an empty UI.
  std::string path = "abc/abcui/lang/";
  path += language;
  path += ".txt";
  parseTableFile("abc/abcui/lang/english.txt");
  if (std::string_view(language) != "english")
    parseTableFile(path);
  if (Table.empty() && std::string_view(language) != "english") {
    abc::ConsoleWarn(
        "[AbcUi] no localization for \"%s\", falling back to english\n",
        language);
    parseTableFile("abc/abcui/lang/english.txt");
  }
  for (const auto &id : ModuleVfs::mountedIds()) {
    parseModuleTable(id, "english");
    if (std::string_view(language) != "english")
      parseModuleTable(id, language);
  }
}

bool Localization::translate(std::string &out, const std::string &token) {
  // Package namespaces end with a slash (ids cannot contain one). Never expose
  // translations from a retired package.
  for (auto it = moduleTables().rbegin(); it != moduleTables().rend(); ++it) {
    if (!ModuleVfs::isMounted(it->first.c_str()))
      continue;
    const auto entry = it->second.find(token);
    if (entry != it->second.end()) {
      out = entry->second;
      return true;
    }
  }
  const auto itor = Table.find(token);
  if (itor == Table.end())
    return false;
  out = itor->second;
  return true;
}

} // namespace framework
