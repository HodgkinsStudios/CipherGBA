// Created by Jacob Hodgkins.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>

namespace pixelgba {
class Cartridge {
public:
    bool load(const std::filesystem::path& path, std::string& error);
    std::uint8_t read(std::uint32_t offset) const;
    std::uint16_t read16(std::uint32_t offset) const;
    std::uint32_t read32(std::uint32_t offset) const;
    bool containsMarker(const char* marker) const;
    bool setRomCheatPatch16(std::uint32_t offset,std::uint16_t value);
    void clearRomCheatPatches(){romCheatPatches_.clear();}
    bool loaded() const { return !rom_.empty(); }
    const std::string& title() const { return title_; }
    const std::string& gameCode() const { return gameCode_; }
    std::size_t size() const { return rom_.size(); }
    std::uint32_t fingerprint() const { return fingerprint_; }
    const std::filesystem::path& sourcePath() const { return path_; }
private:
    std::vector<std::uint8_t> rom_;
    std::unordered_map<std::uint32_t,std::uint16_t> romCheatPatches_;
    std::string title_, gameCode_;
    std::uint32_t fingerprint_=0;
    std::size_t decodeSize_=0; // effective ROM address decode window, cached at load
    std::filesystem::path path_;
};
} // namespace pixelgba
