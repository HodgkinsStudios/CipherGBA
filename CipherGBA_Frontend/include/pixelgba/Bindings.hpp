// Created by Jacob Hodgkins.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace pixelgba {
// Indices match the GBA KEYINPUT register bit positions.
enum class Key : std::size_t { A, B, Select, Start, Right, Left, Up, Down, R, L, Count };
constexpr std::size_t KeyCount = static_cast<std::size_t>(Key::Count);
const std::array<const char*, KeyCount>& keyNames();
struct PadMapping {
    enum class Type { Button, Axis } type = Type::Button;
    int index = 0;
    int direction = 1;
    bool operator==(const PadMapping& o) const {
        return type == o.type && index == o.index && direction == o.direction;
    }
};
struct Bindings {
    std::array<std::uint32_t, KeyCount> keyboard{}; // X11 KeySym keyvals; no GUI headers needed by core.
    std::array<PadMapping, KeyCount> pad{}; // Linux joystick button/axis indices (/dev/input/jsN).
    static Bindings defaults();
    static bool load(const std::filesystem::path&, Bindings& result, std::string& error);
    bool save(const std::filesystem::path&, std::string& error) const;
};
} // namespace pixelgba
