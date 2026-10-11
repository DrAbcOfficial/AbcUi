"""Compile production logic fragments and check the P6 input/color/avatar regressions.
Requires CMake, MSVC and Pillow (the independent TGA decoder).
"""
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def between(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0]


def main():
    panel = (ROOT / 'src/panels/weaponchoose/WeaponChoosePanel.cpp').read_text()
    avatars = (ROOT / 'src/data/Avatars.cpp').read_text()
    events = (ROOT.parent / 'AbcCore/src/Events.cpp').read_text()
    attack = between(panel, 'return !weaponchoose::attackConfirm();', '\n')
    assert not attack.strip()
    callback = between(panel, 'otherwise the attack runs.', '\n\t\t});').split('\n', 1)[1]
    colors = between(panel, 'unsigned char r =', '\n\t\t\tif (weaponchoose::RainbowMenu')
    decoder = between(avatars, '\tif (bytes.size() < 18)', '\n\tif (!vgui2ext::available())')
    chain = between(events, 'void CChainedCommand::RunChain() {', '\n}')
    source = r'''
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <iostream>
#include <functional>
namespace weaponchoose { bool confirm = false; bool attackConfirm() { return confirm; } }
namespace weaponres { bool ammo = false; bool hasAmmo(const void*) { return ammo; } }
bool callback() { CALLBACK }
struct CallbackTypes { using Handle = int; using Callback = std::function<bool()>; };
using Callback = CallbackTypes;
struct Event { template<class F> void forEach(F f) { f(0, callback); } };
int attacks = 0;
void original() { ++attacks; }
struct Chain {
    Event m_event;
    void (*m_pfnOriginal)() = original;
    void run() { CHAIN }
};
bool color(bool ammo) {
    weaponres::ammo = ammo;
    const void* weapon = nullptr;
    unsigned char r = COLORS
    return ammo ? (r == 0 && g == 255 && ri == 0 && gi == 255 && ai == 125)
                : (r == 255 && g == 0 && ri == 125 && gi == 0 && ai == 80);
}
bool decode(const std::vector<unsigned char>& bytes, std::vector<unsigned char>& output) {
    if (bytes.size() < 18) DECODER
    output = rgba;
    return true;
}
std::vector<unsigned char> read(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
int main(int argc, char** argv) {
    if (argc != 3) return 1;
    Chain chain;
    chain.run();
    if (attacks != 1) return 2;
    weaponchoose::confirm = true;
    chain.run();
    if (attacks != 1) return 3;
    if (!color(true) || !color(false)) return 4;
    auto bytes = read(argv[1]);
    std::vector<unsigned char> output;
    if (!decode(bytes, output) || output != read(argv[2])) return 5;
    bytes.resize(20);
    if (decode(bytes, output)) return 6;
    std::cout << "PASS: attack passthrough/consume, ammo colors, avatar pixels, truncated TGA rejection\n";
}
'''
    for key, value in [('CALLBACK', callback), ('CHAIN', chain), ('COLORS', colors), ('DECODER', decoder)]:
        source = source.replace(key, value)
    with tempfile.TemporaryDirectory(prefix='abcui-regression-') as directory:
        work = Path(directory)
        (work / 'main.cpp').write_text(source)
        (work / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(regression LANGUAGES CXX)\nadd_executable(regression main.cpp)\ntarget_compile_features(regression PRIVATE cxx_std_17)\n')
        image = ROOT / 'assets/abcui/img/default_avatar.tga'
        expected = work / 'expected.rgba'
        expected.write_bytes(Image.open(image).convert('RGBA').tobytes())
        subprocess.run(['cmake', '-S', str(work), '-B', str(work / 'build'), '-G', 'Visual Studio 18 2026', '-A', 'Win32'], check=True)
        subprocess.run(['cmake', '--build', str(work / 'build'), '--config', 'Debug'], check=True)
        subprocess.run([str(work / 'build/Debug/regression.exe'), str(image), str(expected)], check=True)


if __name__ == '__main__':
    main()
