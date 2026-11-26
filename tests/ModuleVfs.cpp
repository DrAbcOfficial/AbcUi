// abc_ui_module_vfs - the custom resource overlay of the module VFS
// (issue 35): a custom file replaces the package entry of its path, a new
// one is listed beside the packaged files, code paths stay package-only,
// traversal and foreign ids are refused, links are not followed, an
// unreadable custom file falls back to the package entry, and decoder
// threads read concurrently. The packages are built in memory
// (framework/PackageFixture.h) and mapped from a scratch directory; the
// custom root is a scratch directory as well, so nothing here needs the
// game.
#include "framework/NativeHandles.h"
#include "framework/PackageFixture.h"
#include "framework/TestEnvironment.h"
#include "framework/TestHarness.h"

#include "modsys/ModuleCustom.h"
#include "modsys/ModuleVfs.h"
#include "modsys/NativeText.h"
#include "modsys/PackageImage.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>
#include <winioctl.h>

namespace {

namespace ModuleVfs = framework::ModuleVfs;

constexpr const char *kRoot = "abc/abcui/custom";
constexpr const char *kSample = "abcui.sample";

const std::vector<abctest::FixtureEntry> kSamplePackage{
    {"manifest.json", "PACKAGE-MANIFEST"},
    {"entry/sample.dll", "PACKAGE-ENTRY"},
    {"gameui/rml/page.rml", "PACKAGE-PAGE"},
    {"gameui/scence/video/a.mp4", "PACKAGE-A"},
    {"gameui/img/logo.png", "PACKAGE-LOGO"},
    {"lang/english.txt", "PACKAGE-LANG"},
};
// The manifest and the entry image, reserved as the loader reserves them.
const std::vector<std::string> kSampleCode{"manifest.json", "entry/sample.dll"};

const std::vector<abctest::FixtureEntry> kClientPackage{
    {"manifest.json", "CLIENT-MANIFEST"},
    {"entry/client.dll", "CLIENT-ENTRY"},
    {"client/panel.rml", "CLIENT-PANEL"},
};

const std::vector<abctest::FixtureEntry> kStrangerPackage{
    {"manifest.json", "STRANGER-MANIFEST"},
    {"entry/stranger.dll", "STRANGER-ENTRY"},
    {"stranger/panel.rml", "STRANGER-PANEL"},
};

void write(const std::filesystem::path &path, std::string_view bytes) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write(const std::filesystem::path &path,
           const std::vector<std::byte> &bytes) {
  write(path, std::string_view(reinterpret_cast<const char *>(bytes.data()),
                               bytes.size()));
}

// A scratch path from its UTF-8 spelling, as the VFS spells package paths.
std::filesystem::path fromUtf8(std::string_view text) {
  return std::filesystem::path(framework::wtext::wideFromUtf8(text));
}

// The scratch tree: the packages on disk, the custom root and a directory
// outside both.
struct Fixture {
  abctest::TempDir scratch{"abcui-vfs"};
  std::filesystem::path packages = scratch.path() / "packages";
  std::filesystem::path custom = scratch.path() / "custom";
  std::filesystem::path outside = scratch.path() / "outside";

  Fixture() {
    std::error_code error;
    std::filesystem::create_directories(packages, error);
    std::filesystem::create_directories(outside, error);
  }

  // A custom file of the module `id`: <custom root>/<id>/<path>.
  std::filesystem::path customFile(std::string_view id,
                                   std::string_view path) const {
    return custom / fromUtf8(id) / fromUtf8(path);
  }
};

std::filesystem::path
install(const Fixture &fixture, const char *file,
        const std::vector<abctest::FixtureEntry> &entries) {
  const auto path = fixture.packages / file;
  write(path, abctest::buildPackage(entries));
  return path;
}

// Maps `file` and mounts it under `id`, as the loader does.
bool mountPackage(const std::filesystem::path &file, const char *id,
                  std::vector<std::string> dependencies = {},
                  std::vector<std::string> codePaths = {}) {
  std::string error;
  auto image = framework::PackageImage::open(
      framework::wtext::utf8FromWide(file.wstring()).c_str(), &error);
  return image &&
         ModuleVfs::mount(id, std::move(image), std::move(dependencies),
                          std::move(codePaths));
}

// Names the scratch directory as the custom root, on a clean mount table.
void nameRoot(const Fixture &fixture) {
  ModuleVfs::unmountAll();
  ModuleVfs::setCustomRoot(
      kRoot, framework::wtext::utf8FromWide(fixture.custom.wstring()).c_str());
}

// The custom tree the cases share.
void writeCustomTree(const Fixture &fixture) {
  write(fixture.customFile(kSample, "gameui/rml/page.rml"), "CUSTOM-PAGE");
  write(fixture.customFile(kSample, "gameui/scence/video/b.mp4"), "CUSTOM-B");
  write(fixture.customFile(kSample, "lang/schinese.txt"), "CUSTOM-LANG");
  write(fixture.customFile(kSample, "gameui/img/logo.png"), "CUSTOM-LOGO");
  write(fixture.customFile(kSample, "deep/nest/ed/file.txt"), "CUSTOM-DEEP");
  // Look-alikes of the package's code: never served.
  write(fixture.customFile(kSample, "manifest.json"), "HIJACKED-MANIFEST");
  write(fixture.customFile(kSample, "entry/sample.dll"), "HIJACKED-ENTRY");
  // Directories without a mounted module of that id.
  write(fixture.customFile("abcui.disabled", "panel.rml"), "DISABLED-PANEL");
  write(fixture.customFile("ABCUI.NOBODY", "panel.rml"), "MISTYPED-PANEL");
}

struct Read {
  bool ok = false;
  std::string bytes;
  std::string error;
  // The entry's bytes are a private buffer / a view into the mapped package.
  bool owned = false;
  bool mapped = false;
};

Read open(const char *uri) {
  framework::PackageEntry entry;
  Read read;
  read.ok = ModuleVfs::open(uri, entry, &read.error);
  if (read.ok) {
    read.bytes.assign(static_cast<const char *>(entry.data), entry.size);
    read.owned = static_cast<bool>(entry.custom);
    read.mapped = static_cast<bool>(entry.mappingPin);
  }
  return read;
}

Read openPath(const char *id, const char *path) {
  framework::PackageEntry entry;
  Read read;
  read.ok = ModuleVfs::openPath(id, path, entry, &read.error);
  if (read.ok) {
    read.bytes.assign(static_cast<const char *>(entry.data), entry.size);
    read.owned = static_cast<bool>(entry.custom);
    read.mapped = static_cast<bool>(entry.mappingPin);
  }
  return read;
}

// A file of `bytes` logical size without writing that much: NTFS extends a
// file without filling the new part until it is written.
bool extend(const std::filesystem::path &path, unsigned long long bytes) {
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  framework::NativeHandle handle(file);
  LARGE_INTEGER size{};
  size.QuadPart = static_cast<LONGLONG>(bytes);
  return SetFilePointerEx(file, size, nullptr, FILE_BEGIN) &&
         SetEndOfFile(file);
}

// A directory junction, which needs no privilege to create (unlike a
// symbolic link). It is removed as a link, so the target's files stay.
class Junction {
public:
  Junction(const std::filesystem::path &link,
           const std::filesystem::path &target)
      : Link(link), Created(create(link, target)) {}
  ~Junction() {
    if (Created)
      RemoveDirectoryW(Link.c_str());
  }

  Junction(const Junction &) = delete;
  Junction &operator=(const Junction &) = delete;

  bool created() const {
    return Created;
  }

private:
  static bool create(const std::filesystem::path &link,
                     const std::filesystem::path &target) {
    if (!CreateDirectoryW(link.c_str(), nullptr))
      return false;
    const HANDLE directory = CreateFileW(
        link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory == INVALID_HANDLE_VALUE)
      return false;
    framework::NativeHandle handle(directory);
    // The mount-point reparse data: offsets, then the substitute name
    // ("\??\<target>") and the print name (<target>), each NUL-terminated.
    const std::wstring substitute = L"\\??\\" + target.wstring();
    const std::wstring print = target.wstring();
    const auto substituteBytes =
        static_cast<std::uint16_t>(substitute.size() * 2);
    const auto printBytes = static_cast<std::uint16_t>(print.size() * 2);
    std::vector<std::byte> names;
    abctest::put16(names, 0); // substitute name offset
    abctest::put16(names, substituteBytes);
    abctest::put16(names, static_cast<std::uint16_t>(substituteBytes + 2));
    abctest::put16(names, printBytes);
    for (const wchar_t c : substitute)
      abctest::put16(names, static_cast<std::uint16_t>(c));
    abctest::put16(names, 0);
    for (const wchar_t c : print)
      abctest::put16(names, static_cast<std::uint16_t>(c));
    abctest::put16(names, 0);
    std::vector<std::byte> buffer;
    abctest::put32(buffer, IO_REPARSE_TAG_MOUNT_POINT);
    abctest::put16(buffer, static_cast<std::uint16_t>(names.size()));
    abctest::put16(buffer, 0);
    buffer.insert(buffer.end(), names.begin(), names.end());
    DWORD returned = 0;
    return DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, buffer.data(),
                           static_cast<DWORD>(buffer.size()), nullptr, 0,
                           &returned, nullptr) != FALSE;
  }

  std::filesystem::path Link;
  bool Created = false;
};

bool mentions(const std::vector<std::string> &reports, std::string_view part) {
  return reports.size() == 1 && reports.front().find(part) != std::string::npos;
}

} // namespace

int main() {
  {
    TEST_CASE("package-alone-without-a-custom-root");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    // The files exist, but no root is named: the package stands alone.
    writeCustomTree(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    const auto page = open("mod://abcui.sample/gameui/rml/page.rml");
    CHECK(page.ok && page.bytes == "PACKAGE-PAGE" && page.mapped);
    CHECK((ModuleVfs::list("mod://abcui.sample/gameui/scence/video") ==
           std::vector<std::string>{
               "mod://abcui.sample/gameui/scence/video/a.mp4"}));
    CHECK(ModuleVfs::unmountedCustomIds().empty());
    std::string path = "untouched";
    CHECK(!ModuleVfs::customPath("mod://abcui.sample/gameui/rml/page.rml",
                                 path) &&
          path.empty());
    CHECK(framework::ModuleCustom::takeReports().empty());
  }
  {
    TEST_CASE("custom-file-replaces-package-entry");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    writeCustomTree(fixture);
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    const char *uri = "mod://abcui.sample/gameui/rml/page.rml";
    const auto opened = open(uri);
    CHECK(opened.ok && opened.bytes == "CUSTOM-PAGE" && opened.owned &&
          !opened.mapped);
    // Every reader reaches the same bytes: the module-path entry point, a
    // dotted spelling and readText.
    const auto viaPath = openPath(kSample, "gameui/rml/page.rml");
    CHECK(viaPath.ok && viaPath.bytes == "CUSTOM-PAGE");
    const auto dotted =
        open("mod://abcui.sample/gameui/rml/./../rml//page.rml");
    CHECK(dotted.ok && dotted.bytes == "CUSTOM-PAGE");
    std::string contents;
    CHECK(ModuleVfs::readText(uri, contents) && contents == "CUSTOM-PAGE");
    // Everything else still comes from the package, zero-copy.
    const auto video = open("mod://abcui.sample/gameui/scence/video/a.mp4");
    CHECK(video.ok && video.bytes == "PACKAGE-A" && video.mapped &&
          !video.owned);
    const auto language = open("mod://abcui.sample/lang/english.txt");
    CHECK(language.ok && language.bytes == "PACKAGE-LANG");
    CHECK(framework::ModuleCustom::takeReports().empty());
  }
  {
    TEST_CASE("new-custom-files-are-listed");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    writeCustomTree(fixture);
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    CHECK((ModuleVfs::list("mod://abcui.sample/gameui/scence/video") ==
           std::vector<std::string>{
               "mod://abcui.sample/gameui/scence/video/a.mp4",
               "mod://abcui.sample/gameui/scence/video/b.mp4"}));
    // A replaced entry is listed once.
    CHECK((ModuleVfs::list("mod://abcui.sample/gameui/rml") ==
           std::vector<std::string>{"mod://abcui.sample/gameui/rml/page.rml"}));
    // A directory only the custom tree has, at any depth.
    CHECK(
        (ModuleVfs::list("mod://abcui.sample/deep") ==
         std::vector<std::string>{"mod://abcui.sample/deep/nest/ed/file.txt"}));
    // The whole module, package and custom alike: sorted, no duplicate.
    CHECK((
        ModuleVfs::list("mod://abcui.sample") ==
        std::vector<std::string>{"mod://abcui.sample/deep/nest/ed/file.txt",
                                 "mod://abcui.sample/entry/sample.dll",
                                 "mod://abcui.sample/gameui/img/logo.png",
                                 "mod://abcui.sample/gameui/rml/page.rml",
                                 "mod://abcui.sample/gameui/scence/video/a.mp4",
                                 "mod://abcui.sample/gameui/scence/video/b.mp4",
                                 "mod://abcui.sample/lang/english.txt",
                                 "mod://abcui.sample/lang/schinese.txt",
                                 "mod://abcui.sample/manifest.json"}));
  }
  {
    TEST_CASE("code-paths-stay-packaged");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    writeCustomTree(fixture);
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    const auto manifest = open("mod://abcui.sample/manifest.json");
    CHECK(manifest.ok && manifest.bytes == "PACKAGE-MANIFEST" &&
          manifest.mapped);
    const auto entryDll = openPath(kSample, "entry/sample.dll");
    CHECK(entryDll.ok && entryDll.bytes == "PACKAGE-ENTRY" && entryDll.mapped);
    // However the reader spells the reserved path.
    const auto dotted = openPath(kSample, "./entry/./sample.dll");
    CHECK(dotted.ok && dotted.bytes == "PACKAGE-ENTRY");
    CHECK(framework::ModuleCustom::takeReports().empty());
  }
  {
    TEST_CASE("module-isolation");
    Fixture fixture;
    const auto sample = install(fixture, "abcui.sample.zip", kSamplePackage);
    const auto client = install(fixture, "abcui.client.zip", kClientPackage);
    const auto stranger =
        install(fixture, "abcui.stranger.zip", kStrangerPackage);
    writeCustomTree(fixture);
    write(fixture.customFile("abcui.client", "client/panel.rml"),
          "CUSTOM-CLIENT-PANEL");
    nameRoot(fixture);
    CHECK(mountPackage(sample, kSample, {}, kSampleCode));
    CHECK(mountPackage(client, "abcui.client", {kSample},
                       {"manifest.json", "entry/client.dll"}));
    CHECK(mountPackage(stranger, "abcui.stranger"));
    // A dependency reads the module it depends on, overlay included, and
    // every module has the overlay of its own directory only.
    CHECK(ModuleVfs::canRead("abcui.client",
                             "mod://abcui.sample/gameui/rml/page.rml"));
    const auto borrowed = open("mod://abcui.sample/gameui/rml/page.rml");
    CHECK(borrowed.ok && borrowed.bytes == "CUSTOM-PAGE");
    const auto own = open("mod://abcui.client/client/panel.rml");
    CHECK(own.ok && own.bytes == "CUSTOM-CLIENT-PANEL");
    // The requester rule is the one it was.
    CHECK(!ModuleVfs::canRead("abcui.stranger",
                              "mod://abcui.sample/gameui/rml/page.rml"));
    CHECK(ModuleVfs::canRead("abcui.client", "mod://abcui.client/client/x"));
    // Another module's custom file is reached through no other id.
    CHECK(!openPath("abcui.stranger", "gameui/rml/page.rml").ok);
    CHECK(!openPath("abcui.stranger", "deep/nest/ed/file.txt").ok);
    CHECK(!open("mod://abcui.client/gameui/rml/page.rml").ok);
    CHECK(ModuleVfs::list("mod://abcui.stranger").size() == 3);
    CHECK(framework::ModuleCustom::takeReports().empty());
  }
  {
    TEST_CASE("ids-outside-the-mounted-set");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    writeCustomTree(fixture);
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    const auto disabled = open("mod://abcui.disabled/panel.rml");
    CHECK(!disabled.ok && disabled.error == "module is not mounted");
    // The id is spelled exactly or not at all.
    CHECK(!open("mod://ABCUI.NOBODY/panel.rml").ok);
    CHECK(!open("mod://ABCUI.SAMPLE/panel.rml").ok);
    CHECK((ModuleVfs::unmountedCustomIds() ==
           std::vector<std::string>{"ABCUI.NOBODY", "abcui.disabled"}));
    std::string path;
    CHECK(!ModuleVfs::customPath("mod://abcui.disabled/panel.rml", path) &&
          path.empty());
    CHECK(framework::ModuleCustom::takeReports().empty());
  }
  {
    TEST_CASE("directory-named-unlike-the-id-is-not-used");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    // The same letters in another case: on a case-insensitive file system
    // the module's directory answers to any spelling, but its name is the
    // one on disk, and only that name is the id.
    write(fixture.customFile("ABCUI.SAMPLE", "gameui/rml/page.rml"),
          "MISTYPED-CASE");
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    const auto page = open("mod://abcui.sample/gameui/rml/page.rml");
    CHECK(page.ok && page.bytes == "PACKAGE-PAGE" && page.mapped);
    CHECK(ModuleVfs::list("mod://abcui.sample").size() == 6);
    CHECK((ModuleVfs::unmountedCustomIds() ==
           std::vector<std::string>{"ABCUI.SAMPLE"}));
    CHECK(framework::ModuleCustom::takeReports().empty());
  }
  {
    TEST_CASE("traversal-is-refused");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    writeCustomTree(fixture);
    write(fixture.custom / "secret.txt", "ROOT-SECRET");
    write(fixture.outside / "secret.txt", "OUTSIDE-SECRET");
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    const std::string control =
        std::string("mod://abcui.sample/gameui/rml/page.rml") + '\x01';
    for (const std::string &uri :
         {std::string("mod://abcui.sample/../secret.txt"),
          std::string("mod://abcui.sample/gameui/rml/../../../secret.txt"),
          std::string("mod://abcui.sample/..\\secret.txt"),
          std::string("mod://abcui.sample/C:/secret.txt"),
          std::string("mod://abcui.sample/gameui/rml/page.rml?x"), control,
          std::string("mod://abcui.sample/.."),
          std::string("mod://abcui.sample/"), std::string("mod://abcui.sample"),
          std::string("mod://"),
          std::string("abcui.sample/gameui/rml/page.rml"), std::string("")}) {
      const auto refused = open(uri.c_str());
      CHECK(!refused.ok);
      CHECK(refused.bytes.empty());
    }
    for (const char *path : {"/secret.txt", "..\\secret.txt", "C:\\secret.txt",
                             "../secret.txt", "./", "."})
      CHECK(!openPath(kSample, path).ok);
    CHECK(ModuleVfs::list("mod://abcui.sample/../outside").empty());
    CHECK(ModuleVfs::list("mod://../outside").empty());
    std::string custom;
    CHECK(!ModuleVfs::customPath("mod://abcui.sample/../secret.txt", custom) &&
          custom.empty());
    // The path itself is still a legal one, one level up and back.
    const auto legal = open("mod://abcui.sample/gameui/../gameui/rml/page.rml");
    CHECK(legal.ok && legal.bytes == "CUSTOM-PAGE");
  }
  {
    TEST_CASE("link-is-not-followed");
    Fixture fixture;
    const auto sample = install(fixture, "abcui.sample.zip", kSamplePackage);
    const auto client = install(fixture, "abcui.client.zip", kClientPackage);
    writeCustomTree(fixture);
    write(fixture.outside / "secret.txt", "OUTSIDE-SECRET");
    write(fixture.scratch.path() / "elsewhere" / "panel.rml",
          "ELSEWHERE-PANEL");
    // A junction out of the module's directory, and one in place of a
    // module's directory.
    const Junction escape(fixture.customFile(kSample, "escape"),
                          fixture.outside);
    const Junction relocated(fixture.custom / "abcui.client",
                             fixture.scratch.path() / "elsewhere");
    CHECK(escape.created());
    CHECK(relocated.created());
    nameRoot(fixture);
    CHECK(mountPackage(sample, kSample, {}, kSampleCode));
    CHECK(mountPackage(client, "abcui.client", {kSample},
                       {"manifest.json", "entry/client.dll"}));
    CHECK(ModuleVfs::list("mod://abcui.sample/escape").empty());
    CHECK(!openPath(kSample, "escape/secret.txt").ok);
    // The index stops at the link; the files around it are still served.
    const auto page = open("mod://abcui.sample/gameui/rml/page.rml");
    CHECK(page.ok && page.bytes == "CUSTOM-PAGE");
    // A module directory that is itself a link is reported and not used.
    CHECK(ModuleVfs::list("mod://abcui.client").size() == 3);
    const auto reports = framework::ModuleCustom::takeReports();
    CHECK(mentions(reports, "is a link") && mentions(reports, "abcui.client"));
  }
  {
    TEST_CASE("unreadable-custom-file-falls-back");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    writeCustomTree(fixture);
    // A custom file past the entry ceiling, in place of a packaged one.
    write(fixture.customFile(kSample, "gameui/scence/video/a.mp4"), "HUGE");
    CHECK(extend(fixture.customFile(kSample, "gameui/scence/video/a.mp4"),
                 framework::kMaxEntryBytes + 1));
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    const char *logo = "mod://abcui.sample/gameui/img/logo.png";
    const auto first = open(logo);
    CHECK(first.ok && first.bytes == "CUSTOM-LOGO");

    // Removed after the module mounted.
    std::error_code removed;
    std::filesystem::remove(fixture.customFile(kSample, "gameui/img/logo.png"),
                            removed);
    CHECK(!removed);
    const auto afterRemove = open(logo);
    CHECK(afterRemove.ok && afterRemove.bytes == "PACKAGE-LOGO" &&
          afterRemove.mapped);
    // The index is the mount-time snapshot: the path stays listed.
    CHECK((ModuleVfs::list("mod://abcui.sample/gameui/img") ==
           std::vector<std::string>{logo}));
    // Reported once, however often a decoder retries.
    CHECK(mentions(framework::ModuleCustom::takeReports(),
                   "gameui/img/logo.png"));
    CHECK(open(logo).bytes == "PACKAGE-LOGO");
    CHECK(framework::ModuleCustom::takeReports().empty());

    // A file past the entry ceiling counts as unreadable as well.
    const auto huge = open("mod://abcui.sample/gameui/scence/video/a.mp4");
    CHECK(huge.ok && huge.bytes == "PACKAGE-A");
    CHECK(mentions(framework::ModuleCustom::takeReports(), "exceeds"));

    // So does one that is held open without sharing.
    const auto page = fixture.customFile(kSample, "gameui/rml/page.rml");
    const HANDLE held = CreateFileW(page.c_str(), GENERIC_READ, 0, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
    CHECK(held != INVALID_HANDLE_VALUE);
    framework::NativeHandle unshare(held);
    const auto locked = open("mod://abcui.sample/gameui/rml/page.rml");
    CHECK(locked.ok && locked.bytes == "PACKAGE-PAGE");
    CHECK(mentions(framework::ModuleCustom::takeReports(),
                   "gameui/rml/page.rml"));
    // Released, the custom file is served again (without a second report).
    unshare.reset();
    const auto released = open("mod://abcui.sample/gameui/rml/page.rml");
    CHECK(released.ok && released.bytes == "CUSTOM-PAGE");
  }
  {
    TEST_CASE("reported-once-per-file");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    writeCustomTree(fixture);
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    std::error_code removed;
    std::filesystem::remove(
        fixture.customFile(kSample, "gameui/scence/video/b.mp4"), removed);
    CHECK(!removed);
    const char *video = "mod://abcui.sample/gameui/scence/video/b.mp4";
    // A custom-only file that cannot be read is simply not there.
    CHECK(!open(video).ok);
    CHECK(!open(video).ok);
    CHECK(!open(video).ok);
    CHECK(mentions(framework::ModuleCustom::takeReports(),
                   "gameui/scence/video/b.mp4"));
    CHECK(framework::ModuleCustom::takeReports().empty());
  }
  {
    TEST_CASE("snapshot-holds-until-remount");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    writeCustomTree(fixture);
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    const char *page = "mod://abcui.sample/gameui/rml/page.rml";
    const char *fresh = "mod://abcui.sample/gameui/rml/fresh.rml";
    write(fixture.customFile(kSample, "gameui/rml/fresh.rml"), "FRESH");
    write(fixture.customFile(kSample, "gameui/rml/page.rml"), "REPLACED");
    // A file added while the game runs is not in the index: only a restart
    // counts for it.
    CHECK(!open(fresh).ok);
    CHECK((ModuleVfs::list("mod://abcui.sample/gameui/rml") ==
           std::vector<std::string>{page}));
    // The bytes are no snapshot: a listed file is read as it lies on disk
    // at the open.
    CHECK(open(page).bytes == "REPLACED");
    // A remount, which is what the next start does, reads the tree again.
    ModuleVfs::unmount(kSample);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    CHECK(open(fresh).bytes == "FRESH");
    CHECK(open(page).bytes == "REPLACED");
    CHECK((ModuleVfs::list("mod://abcui.sample/gameui/rml") ==
           std::vector<std::string>{fresh, page}));
    // Without a root the next mount has no overlay at all.
    ModuleVfs::unmount(kSample);
    ModuleVfs::setCustomRoot(nullptr, nullptr);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    CHECK(!open(fresh).ok);
    CHECK(open(page).bytes == "PACKAGE-PAGE");
    CHECK(ModuleVfs::unmountedCustomIds().empty());
    std::string path;
    CHECK(!ModuleVfs::customPath(page, path));
    // Neither does an empty game path: left alone it would name the root of
    // the current drive and report its folders as custom directories.
    ModuleVfs::unmount(kSample);
    ModuleVfs::setCustomRoot(kRoot, "");
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    CHECK(!open(fresh).ok);
    CHECK(ModuleVfs::unmountedCustomIds().empty());
  }
  {
    TEST_CASE("entry-outlives-later-reads-and-unmount");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    writeCustomTree(fixture);
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    framework::PackageEntry custom, packaged;
    CHECK(ModuleVfs::open("mod://abcui.sample/gameui/rml/page.rml", custom,
                          nullptr));
    CHECK(ModuleVfs::open("mod://abcui.sample/gameui/scence/video/a.mp4",
                          packaged, nullptr));
    // Later opens of the same files, then the mount itself going away.
    CHECK(open("mod://abcui.sample/gameui/rml/page.rml").ok);
    CHECK(open("mod://abcui.sample/deep/nest/ed/file.txt").ok);
    ModuleVfs::unmountAll();
    CHECK(std::string_view(static_cast<const char *>(custom.data),
                           custom.size) == "CUSTOM-PAGE");
    CHECK(std::string_view(static_cast<const char *>(packaged.data),
                           packaged.size) == "PACKAGE-A");
  }
  {
    TEST_CASE("decoder-threads-read-concurrently");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    writeCustomTree(fixture);
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    struct Target {
      const char *uri;
      std::string_view bytes;
    };
    // A replaced entry, a custom-only one and a packaged one.
    constexpr std::array<Target, 3> kTargets{{
        {"mod://abcui.sample/gameui/rml/page.rml", "CUSTOM-PAGE"},
        {"mod://abcui.sample/deep/nest/ed/file.txt", "CUSTOM-DEEP"},
        {"mod://abcui.sample/gameui/scence/video/a.mp4", "PACKAGE-A"},
    }};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int index = 0; index < 8; ++index)
      threads.emplace_back([&] {
        for (int round = 0; round < 300; ++round) {
          for (const auto &target : kTargets) {
            framework::PackageEntry entry;
            if (!ModuleVfs::open(target.uri, entry, nullptr) ||
                std::string_view(static_cast<const char *>(entry.data),
                                 entry.size) != target.bytes) {
              failures.fetch_add(1);
              continue;
            }
            // The entry's buffer is its own: marking it here must not
            // reach what any other thread holds.
            if (entry.custom)
              entry.custom[0] = std::byte{0};
          }
          if (ModuleVfs::list("mod://abcui.sample/gameui").size() != 4)
            failures.fetch_add(1);
        }
      });
    for (auto &thread : threads)
      thread.join();
    CHECK(failures.load() == 0);
    CHECK(open("mod://abcui.sample/gameui/rml/page.rml").bytes ==
          "CUSTOM-PAGE");
    CHECK(framework::ModuleCustom::takeReports().empty());
  }
  {
    TEST_CASE("custom-path-spelling");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    writeCustomTree(fixture);
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    std::string path;
    CHECK(
        ModuleVfs::customPath("mod://abcui.sample/models/menu_action", path) &&
        path == "abc/abcui/custom/abcui.sample/models/menu_action");
    CHECK(ModuleVfs::customPath("mod://abcui.sample/./x//y/../z.txt", path) &&
          path == "abc/abcui/custom/abcui.sample/x/z.txt");
    CHECK(!ModuleVfs::customPath("mod://abcui.sample/../z.txt", path) &&
          path.empty());
    CHECK(!ModuleVfs::customPath("mod://abcui.disabled/z.txt", path));
    CHECK(!ModuleVfs::customPath("abc/abcui/custom/abcui.sample/z.txt", path));
  }
  {
    TEST_CASE("names-outside-ascii");
    Fixture fixture;
    const auto file = install(fixture, "abcui.sample.zip", kSamplePackage);
    // "matérial/été.txt", spelled in UTF-8 so the case needs no source
    // encoding.
    const std::string directory = "mat\xc3\xa9rial";
    const std::string name = "\xc3\xa9t\xc3\xa9.txt";
    write(fixture.customFile(kSample, directory + "/" + name), "ACCENTED");
    nameRoot(fixture);
    CHECK(mountPackage(file, kSample, {}, kSampleCode));
    const std::string expected =
        std::string("mod://abcui.sample/") + directory + "/" + name;
    const auto accented = open(expected.c_str());
    CHECK(accented.ok && accented.bytes == "ACCENTED");
    CHECK((ModuleVfs::list(
               (std::string("mod://abcui.sample/") + directory).c_str()) ==
           std::vector<std::string>{expected}));
    // Sorted bytewise, the accented name sorts after the manifest.
    CHECK((ModuleVfs::list("mod://abcui.sample") ==
           std::vector<std::string>{
               "mod://abcui.sample/entry/sample.dll",
               "mod://abcui.sample/gameui/img/logo.png",
               "mod://abcui.sample/gameui/rml/page.rml",
               "mod://abcui.sample/gameui/scence/video/a.mp4",
               "mod://abcui.sample/lang/english.txt",
               "mod://abcui.sample/manifest.json", expected}));
  }
  return abctest::finish("abc_ui_module_vfs");
}
