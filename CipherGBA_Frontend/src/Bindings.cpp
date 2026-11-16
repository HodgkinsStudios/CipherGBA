// Created by Jacob Hodgkins.
#include "pixelgba/Bindings.hpp"
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
namespace pixelgba {
const std::array<const char*, KeyCount>& keyNames() {
    static const std::array<const char*, KeyCount> names = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};
    return names;
}
Bindings Bindings::defaults() {
    Bindings b;
    // X11 KeySym values: Return, BackSpace, arrow keys, and lowercase ASCII.
    b.keyboard = {'z', 'x', 0xff08, 0xff0d, 0xff53, 0xff51, 0xff52, 0xff54, 's', 'a'}; // Only the ten GBA hardware buttons are configurable.
    // Linux /dev/input/js0 button and axis indices (common Xbox layout).
    // Buttons: 0=A, 1=B, 4=LB, 5=RB, 6=Back, 7=Start.
    // D-pad is commonly axes 6/7. Remap if your controller differs.
    b.pad = {PadMapping{PadMapping::Type::Button, 0},
             PadMapping{PadMapping::Type::Button, 1},
             PadMapping{PadMapping::Type::Button, 6},
             PadMapping{PadMapping::Type::Button, 7},
             PadMapping{PadMapping::Type::Axis, 6, 1},
             PadMapping{PadMapping::Type::Axis, 6, -1},
             PadMapping{PadMapping::Type::Axis, 7, -1},
             PadMapping{PadMapping::Type::Axis, 7, 1},
             PadMapping{PadMapping::Type::Button, 5},
             PadMapping{PadMapping::Type::Button, 4}};
    return b;
}
bool Bindings::save(const std::filesystem::path& path, std::string& error) const {
    const auto temp = std::filesystem::path(path.string() + ".tmp");
    try {
        std::filesystem::create_directories(path.parent_path());
        {
            std::ofstream output(temp, std::ios::trunc);
            if (!output) { error = "Unable to open input configuration for writing."; return false; }
            output << "CipherGBA_INPUT_V6\n";
            for (std::size_t i=0; i<KeyCount; ++i) {
                const auto& p = pad[i];
                output << keyNames()[i] << ' ' << keyboard[i] << ' '
                       << (p.type == PadMapping::Type::Button ? 'B' : 'X')
                       << ' ' << p.index << ' ' << p.direction << '\n';
            }
            output.flush();
            if (!output) { error = "Unable to finish writing input configuration."; return false; }
        }
        std::filesystem::rename(temp, path);
    } catch (const std::filesystem::filesystem_error& e) {
        std::error_code ignored; std::filesystem::remove(temp, ignored);
        error = e.what(); return false;
    }
    error.clear(); return true;
}
bool Bindings::load(const std::filesystem::path& path, Bindings& result, std::string& error) {
    std::ifstream input(path);
    if (!input) { error = "Input configuration does not exist."; return false; }
    std::string magic;
    if (!(input >> magic)) {
        error = "Unrecognized input configuration version."; return false;
    }
    const bool v5 = (magic == "CipherGBA_INPUT_V6" || magic == "CipherGBA_INPUT_V5");
    const bool v4 = (magic == "CipherGBA_INPUT_V4" || magic == "PixelGBA_INPUT_V4");
    const bool v3 = (magic == "CipherGBA_INPUT_V3" || magic == "PixelGBA_INPUT_V3");
    const bool v2 = (magic == "CipherGBA_INPUT_V2" || magic == "PixelGBA_INPUT_V2" || magic == "PixelGBA_INPUT_V1");
    if (!(v5 || v4 || v3 || v2)) {
        error = "Unrecognized input configuration version."; return false;
    }
    Bindings candidate = defaults();
    for (std::size_t i=0; i<KeyCount; ++i) {
        std::string name; unsigned long long key; char type; int index, direction;
        if (!(input >> name >> key >> type >> index >> direction) ||
            name != keyNames()[i] || key > std::numeric_limits<std::uint32_t>::max() ||
            (type != 'B' && type != 'X') || index < 0 ||
            (type == 'B' && index > 31) || (type == 'X' && index > 15) ||
            (direction != -1 && direction != 1)) {
            error = "Input configuration is malformed or contains invalid mappings.";
            return false;
        }
        candidate.keyboard[i] = static_cast<std::uint32_t>(key);
        candidate.pad[i] = PadMapping{type == 'B' ? PadMapping::Type::Button : PadMapping::Type::Axis, index, direction};
    }
    if (v2) {
        // Version 1 stored SDL controller button/axis IDs. Their numeric IDs
        // are not equivalent to Linux joystick IDs; migrate known mappings.
        for (std::size_t i=0;i<10;++i) {
            auto& map=candidate.pad[i];
            if (map.type == PadMapping::Type::Button) {
                constexpr int buttonToJs[15] = {
                    0, 1, 2, 3, 6, 8, 7, 9, 10, 4, 5, -1, -1, -1, -1};
                const int id = map.index <= 14 ? buttonToJs[map.index] : -1;
                if (id >= 0) map.index = id;
                else {
                    // SDL D-pad buttons 11..14 are joystick hat axes 7/6.
                    if (map.index == 11 || map.index == 12)
                        map = {PadMapping::Type::Axis, 7, map.index == 11 ? -1 : 1};
                    else map = {PadMapping::Type::Axis, 6, map.index == 13 ? -1 : 1};
                }
            }
        }
    }
    result = candidate;
    error.clear(); return true;
}
}
