"""Compile production logic fragments and check the P6 input/color regressions.
Requires CMake and MSVC.

(The avatar TGA-decoder fragment regression was retired when the hand-rolled
decoder was replaced by framework::ImageLoader/FreeImage - the asset itself
migrated to PNG with its own byte-exact verification, see
scripts/convert_tga_to_png.py.)
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def between(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0]


def main():
    panel = (ROOT / 'src/panels/weaponchoose/WeaponChoosePanel.cpp').read_text()
    events = (ROOT.parent / 'AbcCore/src/Events.cpp').read_text()
    attack = between(panel, 'return !weaponchoose::attackConfirm();', '\n')
    assert not attack.strip()
    callback = between(panel, 'otherwise the attack runs.', '\n\t\t});').split('\n', 1)[1]
    colors = between(panel, 'unsigned char r =', '\n\t\t\tif (weaponchoose::RainbowMenu')
    chain = between(events, 'void CChainedCommand::RunChain() {', '\n}')
    source = r'''
#include <algorithm>
#include <cstring>
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
int main() {
    Chain chain;
    chain.run();
    if (attacks != 1) return 2;
    weaponchoose::confirm = true;
    chain.run();
    if (attacks != 1) return 3;
    if (!color(true) || !color(false)) return 4;
    std::cout << "PASS: attack passthrough/consume, ammo colors\n";
}
'''
    for key, value in [('CALLBACK', callback), ('CHAIN', chain), ('COLORS', colors)]:
        source = source.replace(key, value)
    with tempfile.TemporaryDirectory(prefix='abcui-regression-') as directory:
        work = Path(directory)
        (work / 'main.cpp').write_text(source)
        (work / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(regression LANGUAGES CXX)\nadd_executable(regression main.cpp)\ntarget_compile_features(regression PRIVATE cxx_std_17)\n')
        subprocess.run(['cmake', '-S', str(work), '-B', str(work / 'build'), '-G', 'Visual Studio 18 2026', '-A', 'Win32'], check=True)
        subprocess.run(['cmake', '--build', str(work / 'build'), '--config', 'Debug'], check=True)
        subprocess.run([str(work / 'build/Debug/regression.exe')], check=True)


if __name__ == '__main__':
    main()
