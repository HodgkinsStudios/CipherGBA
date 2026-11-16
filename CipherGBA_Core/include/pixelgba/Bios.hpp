// Created by Jacob Hodgkins.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace pixelgba {
class Bios {
public:
    static constexpr std::size_t Size = 16 * 1024;
    bool load(const std::filesystem::path& path, std::string& error);
    bool importCopy(const std::filesystem::path& path, const std::filesystem::path& dest, std::string& error);
    std::uint8_t read(std::uint32_t offset) const { return loaded_ && offset < Size ? data_[offset] : 0xff; }
    bool loaded() const { return loaded_; }
    void clear(){data_.fill(0);loaded_=false;}
private:
    std::array<std::uint8_t, Size> data_{};
    bool loaded_ = false;
};
} // namespace pixelgba
