// Created by Jacob Hodgkins.
#include "pixelgba/Paths.hpp"
#include <array>
#include <unistd.h>
namespace pixelgba {
Paths Paths::discover() {
    std::array<char, 4096> exe{};
    const auto count = readlink("/proc/self/exe", exe.data(), exe.size() - 1);
    const auto directory = count > 0
        ? std::filesystem::path(std::string(exe.data(), static_cast<std::size_t>(count))).parent_path()
        : std::filesystem::current_path();
    Paths p{};
    p.executableDirectory = directory;
    p.dataDirectory = directory / "CipherGBA_Data";
    p.biosFile = p.dataDirectory / "bios" / "gba_bios.bin";
    p.inputConfig = p.dataDirectory / "config" / "input.map";
    return p;
}
}
