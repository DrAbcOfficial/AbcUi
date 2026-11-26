// abc_ui_menu_action_models - where a bare action model spelling resolves
// and how the packaged stance models reach the disk (issue 35). The
// directory is one source shared by the resolver, the extraction and the
// settings scan, and follows the menu module's id into its custom mount.
// The extraction writes a model only when its file is missing, so a user's
// model of the same name is never rewritten. The package is built in memory
// (framework/PackageFixture.h) and the custom root is a scratch directory,
// which stands in for the engine's file system as well.
#include "framework/PackageFixture.h"
#include "framework/TestEnvironment.h"
#include "framework/TestHarness.h"

#include "modsys/ModuleCustom.h"
#include "modsys/ModuleVfs.h"
#include "modsys/NativeText.h"
#include "modsys/PackageImage.h"
#include "rmlui/modelpreview/ActionModels.h"
#include "rmlui/modelpreview/Options.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace ModuleVfs = framework::ModuleVfs;
using modelpreview::resolveActionModelPath;

constexpr const char *kModels = "mod://abcui.gameui/models/menu_action";
constexpr const char *kRelative = "abc/abcui/custom";
// The path the issue names for abcui.gameui.
constexpr const char *kDirectory =
    "abc/abcui/custom/abcui.gameui/models/menu_action/";

const std::vector<abctest::FixtureEntry> kGameUiPackage{
    {"manifest.json", "GAMEUI-MANIFEST"},
    {"entry/abcui_gameui.dll", "GAMEUI-ENTRY"},
    {"models/menu_action/display_male.mdl", "PACKAGED-MALE"},
    {"models/menu_action/display_female.mdl", "PACKAGED-FEMALE"},
    {"models/menu_action/nested/inner.mdl", "PACKAGED-INNER"},
    {"models/menu_action/notes.txt", "PACKAGED-NOTES"},
};

void write(const fs::path &path, std::string_view bytes) {
  std::error_code error;
  fs::create_directories(path.parent_path(), error);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write(const fs::path &path, const std::vector<std::byte> &bytes) {
  write(path, std::string_view(reinterpret_cast<const char *>(bytes.data()),
                               bytes.size()));
}

std::string read(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

// A scratch tree: the package on disk and the custom root. In the game the
// engine's file system reaches a custom file under the same relative path
// the VFS names, so one directory plays both here.
struct Fixture {
  abctest::TempDir scratch{"abcui-action"};
  fs::path packages = scratch.path() / "packages";
  fs::path custom = scratch.path() / "custom";

  // The action model directory of the scratch tree.
  fs::path directory() const {
    return custom / "abcui.gameui" / "models" / "menu_action";
  }

  // abc::ModPath for the scratch tree: the file-API path of an
  // engine-relative one below the custom root.
  modelpreview::NativePath native() const {
    const std::string root = std::string(kRelative) + "/";
    return [base = custom.wstring(), root](const std::string &relative) {
      if (!relative.starts_with(root))
        return std::string();
      const std::wstring suffix =
          framework::wtext::wideFromUtf8(relative.substr(root.size()));
      return framework::wtext::utf8FromWide(base + L"\\" + suffix);
    };
  }
};

// Mounts abcui.gameui over its custom directory and declares the action
// model directory the way the menuconfig api does.
void setUp(const Fixture &fixture) {
  const fs::path file = fixture.packages / "gameui.zip";
  write(file, abctest::buildPackage(kGameUiPackage));
  ModuleVfs::unmountAll();
  ModuleVfs::setCustomRoot(
      kRelative,
      framework::wtext::utf8FromWide(fixture.custom.wstring()).c_str());
  std::string error;
  auto image = framework::PackageImage::open(
      framework::wtext::utf8FromWide(file.wstring()).c_str(), &error);
  CHECK(image && ModuleVfs::mount("abcui.gameui", std::move(image), {},
                                  {"manifest.json", "entry/abcui_gameui.dll"}));
  std::string directory;
  CHECK(ModuleVfs::customPath(kModels, directory));
  modelpreview::setActionModelDir(directory + "/");
}

} // namespace

int main() {
  {
    TEST_CASE("bare-spelling-names-a-file-of-the-directory");
    modelpreview::setActionModelDir(kDirectory);
    CHECK(resolveActionModelPath("display_female", {}) ==
          std::string(kDirectory) + "display_female.mdl");
    CHECK(resolveActionModelPath("my_group", {}) ==
          std::string(kDirectory) + "my_group.mdl");
    // An empty spelling is the default group.
    CHECK(resolveActionModelPath("", {}) ==
          std::string(kDirectory) + "display_male.mdl");
  }
  {
    TEST_CASE("mdl-path-is-used-verbatim");
    modelpreview::setActionModelDir(kDirectory);
    // A complete path is any game model path, whatever directory it names.
    for (const char *spelling :
         {"models/player/foo/foo.mdl", "abc/abcui/models/menu_action/x.mdl"}) {
      CHECK(resolveActionModelPath(spelling, {}) == spelling);
    }
  }
  {
    TEST_CASE("unusable-spelling-selects-the-default-group");
    modelpreview::setActionModelDir(kDirectory);
    for (const char *spelling :
         {"../evil.mdl", "C:/evil.mdl", "..\\evil.mdl", "models//evil"}) {
      CHECK(resolveActionModelPath(spelling, {}) ==
            std::string(kDirectory) + "display_male.mdl");
    }
  }
  {
    TEST_CASE("group-the-engine-cannot-see-is-not-presented");
    modelpreview::setActionModelDir(kDirectory);
    const auto present =
        resolveActionModelPath("display_male", [](const std::string &path) {
          return path == std::string(kDirectory) + "display_male.mdl";
        });
    CHECK(present == std::string(kDirectory) + "display_male.mdl");
    CHECK(resolveActionModelPath("nobody", [](const std::string &) {
            return false;
          }).empty());
  }
  {
    TEST_CASE("no-directory-names-no-group");
    modelpreview::setActionModelDir({});
    // Before the resource root is declared a bare spelling has no file.
    CHECK(resolveActionModelPath("display_male", {}).empty());
    CHECK(resolveActionModelPath("", {}).empty());
    CHECK(resolveActionModelPath("../evil.mdl", {}).empty());
    CHECK(resolveActionModelPath("models/player.mdl", {}) ==
          "models/player.mdl");
  }
  {
    TEST_CASE("directory-follows-the-module-id");
    Fixture fixture;
    setUp(fixture);
    // Derived from the resource root's module id: nowhere is "abcui.gameui"
    // spelled in the framework for it.
    CHECK(modelpreview::actionModelDir() == kDirectory);
    CHECK(resolveActionModelPath("display_male", {}) ==
          "abc/abcui/custom/abcui.gameui/models/menu_action/display_male.mdl");
  }
  {
    TEST_CASE("packaged-models-are-written-when-missing");
    Fixture fixture;
    setUp(fixture);
    modelpreview::prepareActionModels(kModels, fixture.native());
    CHECK(read(fixture.directory() / "display_male.mdl") == "PACKAGED-MALE");
    CHECK(read(fixture.directory() / "display_female.mdl") ==
          "PACKAGED-FEMALE");
    // Only the direct model children: nothing else is written and no
    // temporary file stays behind.
    std::vector<std::string> listed;
    for (const auto &entry : fs::directory_iterator(fixture.directory()))
      listed.push_back(entry.path().filename().string());
    CHECK((listed ==
           std::vector<std::string>{"display_female.mdl", "display_male.mdl"}));
    CHECK(!fs::exists(fixture.directory() / "nested"));
    CHECK(!fs::exists(fixture.directory() / "notes.txt"));
  }
  {
    TEST_CASE("existing-file-is-never-rewritten");
    Fixture fixture;
    // The user's model, there before the module mounts.
    write(fixture.directory() / "display_female.mdl", "USER-FEMALE");
    setUp(fixture);
    modelpreview::prepareActionModels(kModels, fixture.native());
    CHECK(read(fixture.directory() / "display_female.mdl") == "USER-FEMALE");
    CHECK(read(fixture.directory() / "display_male.mdl") == "PACKAGED-MALE");

    // A file changed afterwards - by the user - survives the next start.
    write(fixture.directory() / "display_male.mdl", "EDITED-MALE");
    modelpreview::prepareActionModels(kModels, fixture.native());
    CHECK(read(fixture.directory() / "display_male.mdl") == "EDITED-MALE");
    CHECK(read(fixture.directory() / "display_female.mdl") == "USER-FEMALE");

    // A file a previous run wrote shadows the packaged model through the
    // VFS on the next start, exactly like the user's.
    ModuleVfs::unmount("abcui.gameui");
    setUp(fixture);
    std::string text;
    CHECK(
        ModuleVfs::readText(
            "mod://abcui.gameui/models/menu_action/display_female.mdl", text) &&
        text == "USER-FEMALE");
    // Listed once each, beside the packaged files.
    CHECK((ModuleVfs::list(kModels) ==
           std::vector<std::string>{
               "mod://abcui.gameui/models/menu_action/display_female.mdl",
               "mod://abcui.gameui/models/menu_action/display_male.mdl",
               "mod://abcui.gameui/models/menu_action/nested/inner.mdl",
               "mod://abcui.gameui/models/menu_action/notes.txt"}));
    CHECK(framework::ModuleCustom::takeReports().empty());
  }
  {
    TEST_CASE("unwritable-target-only-warns");
    Fixture fixture;
    setUp(fixture);
    // No path for the engine-relative spelling: nothing is written.
    modelpreview::prepareActionModels(kModels, {});
    CHECK(!fs::exists(fixture.directory()));
    // A path that cannot become a directory, a file being in the way.
    const fs::path blocker = fixture.scratch.path() / "blocker";
    write(blocker, "FILE");
    modelpreview::prepareActionModels(kModels, [&](const std::string &) {
      return framework::wtext::utf8FromWide(
          (blocker / "models" / "x.mdl").wstring());
    });
    CHECK(read(blocker) == "FILE");
    CHECK(!fs::exists(fixture.directory()));
    // Without an action model directory there is nowhere to write to.
    modelpreview::setActionModelDir({});
    modelpreview::prepareActionModels(kModels, fixture.native());
    CHECK(!fs::exists(fixture.directory()));
  }
  return abctest::finish("abc_ui_menu_action_models");
}
