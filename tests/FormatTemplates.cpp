// abc_ui_format_templates - the one translation-template filler and the
// shipped language tables. Pins:
//   - the abc::Format contract every {0}/{1} call site relies on: a marker
//     takes the argument at its index, every occurrence is replaced, an
//     argument is inserted verbatim and never rescanned, and a malformed or
//     out-of-range marker stays visible;
//   - every shipped lang/*.txt entry is a well-formed template: {N} markers
//     only (no printf specifier such as %s, %d or %s1, no named marker such
//     as %%NAME%% or {name}), numbered from 0 without gaps;
//   - a key carries the same markers in every language that translates it;
//   - every template the code fills exists in the English table and uses
//     exactly the arguments its call site passes.
// Pure: no engine, no game installation, no RmlUi. The tables come from the
// checkout that built the test.
#include <abc/Format.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "framework/TestEnvironment.h"
#include "framework/TestHarness.h"
#include "framework/TextUtil.h"

namespace {

using framework::TextUtil::parseTranslationLine;
using Table = std::map<std::string, std::string>; // key -> template
using Module = std::map<std::string, Table>;      // language -> table
using Modules = std::map<std::string, Module>;    // module id -> languages
using Problems = std::vector<std::string>;

// No shipped template reaches this index; it only ends a runaway digit run.
constexpr int kIndexOverflow = 1000000;
// The table scan reports every problem it finds, up to this many.
constexpr std::size_t kReportedProblems = 40;

// The markers of a template in abc::Format's grammar: "{N}" is a marker and
// "{{" a literal brace. False when a brace opens anything else (an
// unterminated or a non-numeric "{..."): abc::Format prints those verbatim.
bool markersOf(const std::string &templateText, std::vector<int> &markers) {
  for (std::size_t i = 0; i < templateText.size(); i++) {
    if (templateText[i] != '{')
      continue;
    if (i + 1 < templateText.size() && templateText[i + 1] == '{') {
      i++;
      continue;
    }
    std::size_t end = i + 1;
    int index = 0;
    while (end < templateText.size() && templateText[end] >= '0' &&
           templateText[end] <= '9') {
      index = std::min(index * 10 + (templateText[end] - '0'), kIndexOverflow);
      end++;
    }
    if (end == i + 1 || end >= templateText.size() || templateText[end] != '}')
      return false;
    markers.push_back(index);
    i = end;
  }
  return true;
}

// A printf-style placeholder ("%s", "%d", "%s1", the "%%" of "%%NAME%%"): a
// table string is never a format string. A bare percent ("50%") is text.
bool hasPrintfSpecifier(const std::string &templateText) {
  for (std::size_t i = 0; i + 1 < templateText.size(); i++)
    if (templateText[i] == '%' &&
        std::isalnum(static_cast<unsigned char>(templateText[i + 1])) != 0)
      return true;
  return false;
}

// 0 .. count - 1, each exactly once (in any order): the template uses every
// argument its call site passes.
bool usesArguments(const std::vector<int> &markers, std::size_t count) {
  if (markers.size() != count)
    return false;
  std::vector<int> sorted = markers;
  std::sort(sorted.begin(), sorted.end());
  for (std::size_t i = 0; i < sorted.size(); i++)
    if (sorted[i] != static_cast<int>(i))
      return false;
  return true;
}

std::vector<int> sortedMarkers(std::vector<int> markers) {
  std::sort(markers.begin(), markers.end());
  return markers;
}

// The production loader's line parser (Localization::parseTable).
Table readTable(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  Table table;
  std::string line;
  std::string key;
  std::string value;
  while (std::getline(in, line))
    if (parseTranslationLine(line, key, value))
      table[key] = value;
  return table;
}

// modules/<id>/assets/lang/<language>.txt, for every module shipping tables.
Modules readModules() {
  Modules modules;
  const std::filesystem::path root = abctest::sourceDir() / "modules";
  for (const auto &module : std::filesystem::directory_iterator(root)) {
    const std::filesystem::path lang = module.path() / "assets" / "lang";
    if (!std::filesystem::is_directory(lang))
      continue;
    for (const auto &file : std::filesystem::directory_iterator(lang))
      if (file.path().extension() == ".txt")
        modules[module.path().filename().string()]
               [file.path().stem().string()] = readTable(file.path());
  }
  return modules;
}

// Every entry is a well-formed {N} template numbered from 0 without gaps,
// and a key carries the same markers in every language that translates it.
// English is the reference; the command menu's literal-label keys have no
// English entry, so their first language is.
void checkTables(const Modules &modules, Problems &problems) {
  for (const auto &[id, languages] : modules) {
    std::vector<std::pair<std::string, const Table *>> order;
    const auto english = languages.find("english");
    if (english != languages.end())
      order.emplace_back("english", &english->second);
    for (const auto &[language, table] : languages)
      if (language != "english")
        order.emplace_back(language, &table);

    struct Reference {
      std::string language;
      std::vector<int> markers;
    };
    std::map<std::string, Reference> reference;
    for (const auto &[language, table] : order)
      for (const auto &[key, templateText] : *table) {
        // "<module>/<language>: <key> = <template>" names the table entry.
        const std::string where =
            id + "/" + language + ": " + key + " = " + templateText;
        std::vector<int> markers;
        if (!markersOf(templateText, markers)) {
          problems.push_back("not a {N} template: " + where);
          continue;
        }
        if (hasPrintfSpecifier(templateText))
          problems.push_back("printf-style placeholder: " + where);
        const std::vector<int> sorted = sortedMarkers(markers);
        for (std::size_t i = 0; i < sorted.size(); i++)
          if (sorted[i] > static_cast<int>(i)) {
            problems.push_back("marker index leaves a gap: " + where);
            break;
          }
        const auto [slot, fresh] =
            reference.try_emplace(key, Reference{language, sorted});
        if (!fresh && slot->second.markers != sorted)
          problems.push_back("markers differ from " + slot->second.language +
                             ": " + where);
      }
  }
}

// Every template the code fills through abc::Format, with the argument count
// its call site passes: an index past the last argument renders verbatim.
constexpr struct FilledTemplate {
  const char *key;
  std::size_t arguments;
} kFilledTemplates[] = {
    {"abcui.gameui/Console_UnknownCommand", 1},
    {"abcui.gameui/SB_Querying", 1},
    {"abcui.gameui/SB_Completed", 1},
    {"abcui.gameui/SB_Cancelled", 1},
    {"abcui.gameui/SB_ServerCount", 1},
    {"abcui.gameui/SB_BlacklistCount", 1},
    {"abcui.gameui/VideoConfirm_TimeoutTime", 1},
    {"abcui.itemhighlight/GameUI_ABC_ItemPickupNotice", 2},
    {"abcui.damage/PopNumber_O", 1},
    {"abcui.damage/PopNumber_K", 1},
    {"abcui.damage/PopNumber_M", 1},
    {"abcui.damage/PopNumber_B", 1},
    {"abcui.killfeed/DeathMsg_ConsolePrint", 3},
};

// The English entry of a key ("abcui.<module>/<token>"): the entry every
// other language falls back to when it does not translate the key.
const std::string *englishTemplate(const Modules &modules,
                                   std::string_view key) {
  const auto slash = key.find('/');
  const auto module = modules.find(std::string(key.substr(6, slash - 6)));
  if (module == modules.end())
    return nullptr;
  const auto table = module->second.find("english");
  if (table == module->second.end())
    return nullptr;
  const auto entry = table->second.find(std::string(key));
  return entry == table->second.end() ? nullptr : &entry->second;
}

// Each filled template exists in English and uses every argument its call
// site passes.
void checkFilledTemplates(const Modules &modules, Problems &problems) {
  for (const FilledTemplate &filled : kFilledTemplates) {
    const std::string key(filled.key);
    const std::string *templateText = englishTemplate(modules, key);
    if (!templateText) {
      problems.push_back("no English entry for a template the code fills: " +
                         key);
      continue;
    }
    std::vector<int> markers;
    if (!markersOf(*templateText, markers) ||
        !usesArguments(markers, filled.arguments))
      problems.push_back("needs markers 0.." +
                         std::to_string(filled.arguments - 1) + ": " + key +
                         " = " + *templateText);
  }
}

} // namespace

int main() {
  TEST_CASE("format-contract");
  // The shipped kill-feed wording: three server-derived names, in order.
  CHECK(abc::Format("{0} was killed by {1} ({2})", "V", "A", "I") ==
        "V was killed by A (I)");
  // Positional, and every occurrence of a marker.
  CHECK(abc::Format("{1} {0} {0}", "a", "b") == "b a a");
  // An index past the last argument and a malformed marker stay visible.
  CHECK(abc::Format("{0} {2}", "a") == "a {2}");
  CHECK(abc::Format("{x}|{0|{0 }|{", "a") == "{x}|{0|{0 }|{");
  // An argument is inserted verbatim and never rescanned.
  CHECK(abc::Format("{0} {1}", "{1}", "b") == "{1} b");
  // A missing name (null) is empty; a number takes std::to_string.
  CHECK(abc::Format("[{0}]", static_cast<const char *>(nullptr)) == "[]");
  CHECK(abc::Format("{0}/{1}", 12, 3) == "12/3");
  // No marker: the text passes through. "{{" is a literal brace.
  CHECK(abc::Format("plain", "unused") == "plain");
  CHECK(abc::Format("{{0}} {0}}", "a") == "{0}} a}");
  // UTF-8 bytes are not characters to the filler.
  CHECK(abc::Format("\xE6\x8C\x89[{0}]", "E") == "\xE6\x8C\x89[E]");

  TEST_CASE("shipped-tables");
  const Modules modules = readModules();
  CHECK(!modules.empty());
  Problems problems;
  checkTables(modules, problems);
  checkFilledTemplates(modules, problems);
  for (std::size_t i = 0; i < problems.size() && i < kReportedProblems; i++)
    std::fprintf(stderr, "  %s\n", problems[i].c_str());
  CHECK(problems.empty());
  return abctest::finish("abc_ui_format_templates");
}
