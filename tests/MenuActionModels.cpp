// abc_ui_menu_action_models - where a bare action model spelling resolves
// and how the packaged stance models reach the disk (issue 35). The groups
// live in the menu module's custom directory, one source shared by the
// extraction and the settings scan and derived from the module's id. The
// extraction writes a model only when its file is missing, so a user's model
// of the same name is never rewritten. The engine cannot name a model under
// that directory (its names are capped at MAX_MODEL_NAME), so it loads a
// short mirror of it that follows the directory. The package is built in
// memory (framework/PackageFixture.h) and the scratch tree stands in for the
// engine's file system as well.
#include "framework/PackageFixture.h"
#include "framework/TestEnvironment.h"
#include "framework/TestHarness.h"

#include "modsys/ModuleCustom.h"
#include "modsys/ModuleVfs.h"
#include "modsys/NativeText.h"
#include "modsys/PackageImage.h"
#include "rmlui/modelpreview/ActionModels.h"
#include "rmlui/modelpreview/Options.h"

#include <algorithm>
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
// The path the issue names for abcui.gameui: where the groups live.
constexpr std::string_view kDirectory =
    "abc/abcui/custom/abcui.gameui/models/menu_action/";
// Where the engine loads them from.
constexpr std::string_view kLoadDir = "abc/abcui/cache/menu_action/";

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

// The file names directly in `directory`, sorted; none while it is absent.
std::vector<std::string> files(const fs::path &directory) {
  std::vector<std::string> names;
  std::error_code error;
  for (fs::directory_iterator it(directory, error), end; it != end;
       it.increment(error))
    names.push_back(it->path().filename().string());
  std::sort(names.begin(), names.end());
  return names;
}

// A scratch tree: the package on disk, the custom root and the mirror.
// In the game the engine's file system reaches these under the same
// relative paths the framework spells them with.
struct Fixture {
  abctest::TempDir scratch{"abcui-action"};
  fs::path packages = scratch.path() / "packages";
  fs::path custom = scratch.path() / "custom";

  // The action model directory of the scratch tree.
  fs::path directory() const {
    return custom / "abcui.gameui" / "models" / "menu_action";
  }

  // The engine's mirror of it.
  fs::path mirror() const {
    return scratch.path() / "cache" / "menu_action";
  }

  // abc::ModPath for the scratch tree.
  modelpreview::NativePath native() const {
    struct Root {
      std::string_view game;
      std::wstring disk;
    };
    const std::vector<Root> roots{
        {std::string_view("abc/abcui/custom/"), custom.wstring()},
        {std::string_view("abc/abcui/cache/"),
         (scratch.path() / "cache").wstring()}};
    return [roots](const std::string &relative) {
      for (const Root &root : roots)
        if (relative.starts_with(root.game))
          return framework::wtext::utf8FromWide(
              root.disk + L"\\" +
              framework::wtext::wideFromUtf8(
                  relative.substr(root.game.size())));
      return std::string();
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

// What the menuconfig api runs when the menu module declares its root.
void prepare(const Fixture &fixture) {
  modelpreview::prepareActionModels(kModels, fixture.native());
  modelpreview::mirrorActionModels(fixture.native());
}

} // namespace

int main() {
  {
    TEST_CASE("bare-spelling-names-a-file-of-the-mirror");
    modelpreview::setActionModelDir(std::string(kDirectory));
    CHECK(resolveActionModelPath("display_female", {}) ==
          std::string(kLoadDir) + "display_female.mdl");
    CHECK(resolveActionModelPath("my_group", {}) ==
          std::string(kLoadDir) + "my_group.mdl");
    // An empty spelling is the default group.
    CHECK(resolveActionModelPath("", {}) ==
          std::string(kLoadDir) + "display_male.mdl");
  }
  {
    TEST_CASE("mdl-path-is-used-verbatim");
    modelpreview::setActionModelDir(std::string(kDirectory));
    // A complete path is any game model path, whatever directory it names.
    for (const char *spelling :
         {"models/player/foo/foo.mdl", "abc/abcui/models/menu_action/x.mdl"}) {
      CHECK(resolveActionModelPath(spelling, {}) == spelling);
    }
  }
  {
    TEST_CASE("unusable-spelling-selects-the-default-group");
    modelpreview::setActionModelDir(std::string(kDirectory));
    for (const char *spelling :
         {"../evil.mdl", "C:/evil.mdl", "..\\evil.mdl", "models//evil"}) {
      CHECK(resolveActionModelPath(spelling, {}) ==
            std::string(kLoadDir) + "display_male.mdl");
    }
  }
  {
    TEST_CASE("group-the-engine-cannot-see-is-not-presented");
    modelpreview::setActionModelDir(std::string(kDirectory));
    const auto present =
        resolveActionModelPath("display_male", [](const std::string &path) {
          return path == std::string(kLoadDir) + "display_male.mdl";
        });
    CHECK(present == std::string(kLoadDir) + "display_male.mdl");
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
          std::string(kLoadDir) + "display_male.mdl");
  }
  {
    TEST_CASE("model-names-fit-the-engine");
    // The reason for the mirror: under the directory a packaged group's name
    // is already past what the engine can hold, under the mirror there is
    // room for a stem of thirty-one characters.
    const std::string_view female = "display_female.mdl";
    CHECK(kDirectory.size() + female.size() >
          modelpreview::kActionModelNameMax);
    CHECK(kLoadDir.size() + female.size() <= modelpreview::kActionModelNameMax);
    const std::string longest(modelpreview::kActionModelNameMax -
                                  kLoadDir.size() -
                                  std::string_view(".mdl").size(),
                              'a');
    CHECK(resolveActionModelPath(longest, {}) ==
          std::string(kLoadDir) + longest + ".mdl");
    // One character more is an unusable spelling, like any other: the
    // default group, never a truncated model.
    CHECK(resolveActionModelPath(longest + "a", {}) ==
          std::string(kLoadDir) + "display_male.mdl");
    // Only a plain stem names a file of the mirror.
    CHECK(modelpreview::actionModelLoadPath("").empty());
    CHECK(modelpreview::actionModelLoadPath("../evil").empty());
    CHECK(modelpreview::actionModelLoadPath("dir/name").empty());
    CHECK(modelpreview::actionModelLoadPath("c:evil").empty());
    // An explicit path the engine cannot name selects the default group.
    CHECK(resolveActionModelPath(std::string(60, 'm') + "/x.mdl", {}) ==
          std::string(kLoadDir) + "display_male.mdl");
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
    CHECK((files(fixture.directory()) ==
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
    modelpreview::mirrorActionModels({});
    CHECK(!fs::exists(fixture.directory()));
    CHECK(!fs::exists(fixture.mirror()));
    // A path that cannot become a directory, a file being in the way.
    const fs::path blocker = fixture.scratch.path() / "blocker";
    write(blocker, "FILE");
    modelpreview::prepareActionModels(kModels, [&](const std::string &) {
      return framework::wtext::utf8FromWide(
          (blocker / "models" / "x.mdl").wstring());
    });
    modelpreview::mirrorActionModels([&](const std::string &) {
      return framework::wtext::utf8FromWide(
          (blocker / "menu_action" / "x.mdl").wstring());
    });
    CHECK(read(blocker) == "FILE");
    CHECK(!fs::exists(fixture.directory()));
    // Without an action model directory there is nowhere to write to.
    modelpreview::setActionModelDir({});
    modelpreview::prepareActionModels(kModels, fixture.native());
    modelpreview::mirrorActionModels(fixture.native());
    CHECK(!fs::exists(fixture.directory()));
    CHECK(!fs::exists(fixture.mirror()));
  }
  {
    TEST_CASE("mirror-holds-the-directorys-models");
    Fixture fixture;
    // The user's models, there before the module mounts.
    write(fixture.directory() / "display_female.mdl", "USER-FEMALE");
    write(fixture.directory() / "mine.mdl", "USER-MINE");
    write(fixture.directory() / "notes.txt", "NOT-A-MODEL");
    setUp(fixture);
    prepare(fixture);
    CHECK(read(fixture.mirror() / "display_male.mdl") == "PACKAGED-MALE");
    CHECK(read(fixture.mirror() / "display_female.mdl") == "USER-FEMALE");
    CHECK(read(fixture.mirror() / "mine.mdl") == "USER-MINE");
    // Only the directory's direct models: nothing nested, nothing else, no
    // temporary file.
    CHECK((files(fixture.mirror()) ==
           std::vector<std::string>{"display_female.mdl", "display_male.mdl",
                                    "mine.mdl"}));
    // The mirror is a copy: the directory itself is left as it was.
    CHECK(read(fixture.directory() / "display_male.mdl") == "PACKAGED-MALE");
    CHECK(read(fixture.directory() / "display_female.mdl") == "USER-FEMALE");
  }
  {
    TEST_CASE("mirror-follows-the-directory");
    Fixture fixture;
    setUp(fixture);
    prepare(fixture);
    // Nothing changed in the directory: the copy is not rewritten.
    const auto written =
        fs::last_write_time(fixture.mirror() / "display_male.mdl");
    prepare(fixture);
    CHECK(fs::last_write_time(fixture.mirror() / "display_male.mdl") ==
          written);

    // Changed in the directory - the user's edit - : copied over.
    write(fixture.directory() / "display_male.mdl", "EDITED-MALE");
    prepare(fixture);
    CHECK(read(fixture.mirror() / "display_male.mdl") == "EDITED-MALE");

    // Added to the directory: copied, and loadable by its bare stem.
    write(fixture.directory() / "mine.mdl", "USER-MINE");
    prepare(fixture);
    CHECK(read(fixture.mirror() / "mine.mdl") == "USER-MINE");
    CHECK(resolveActionModelPath("mine", {}) ==
          std::string(kLoadDir) + "mine.mdl");

    // Removed from the directory: its copy goes with it. A user's model is
    // the case to use: a packaged one the extraction would put back.
    std::error_code removed;
    fs::remove(fixture.directory() / "mine.mdl", removed);
    CHECK(!removed);
    modelpreview::mirrorActionModels(fixture.native());
    CHECK(!fs::exists(fixture.mirror() / "mine.mdl"));

    // A stem the engine cannot name gets no copy; the others are untouched.
    const std::string tooLong(32, 'b');
    write(fixture.directory() / (tooLong + ".mdl"), "TOO-LONG");
    prepare(fixture);
    CHECK(!fs::exists(fixture.mirror() / (tooLong + ".mdl")));
    CHECK((files(fixture.mirror()) ==
           std::vector<std::string>{"display_female.mdl", "display_male.mdl"}));

    // A temporary file an interrupted copy left does not survive a sync.
    write(fixture.directory() / "mine.mdl", "USER-MINE");
    prepare(fixture);
    write(fixture.mirror() / "mine.mdl.tmp", "PARTIAL");
    prepare(fixture);
    CHECK(!fs::exists(fixture.mirror() / "mine.mdl.tmp"));
    CHECK(read(fixture.mirror() / "mine.mdl") == "USER-MINE");
  }
  {
    TEST_CASE("mirror-without-a-directory-removes-its-copies");
    Fixture fixture;
    setUp(fixture);
    prepare(fixture);
    CHECK(files(fixture.mirror()).size() == 2);
    // The directory is gone (the user removed it): the mirror follows. The
    // extraction is not part of this step, or it would fill it again.
    std::error_code error;
    fs::remove_all(fixture.directory(), error);
    modelpreview::mirrorActionModels(fixture.native());
    CHECK(files(fixture.mirror()).empty());
  }
  return abctest::finish("abc_ui_menu_action_models");
}
