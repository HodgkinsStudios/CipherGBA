// Created by Jacob Hodgkins.
#include "pixelgba/Bios.hpp"
#include "pixelgba/AtomicReplace.hpp"
#include <fstream>
#include <system_error>

namespace pixelgba {
bool Bios::load(const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != static_cast<std::streamoff>(Size)) {
        error = "GBA BIOS must be an uncompressed 16,384-byte .bin file.";
        return false;
    }
    std::array<std::uint8_t, Size> next{};
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(next.data()), Size)) {
        error = "Could not read complete BIOS image.";
        return false;
    }
    data_ = next;
    loaded_ = true;
    error.clear();
    return true;
}
bool Bios::importCopy(const std::filesystem::path& path, const std::filesystem::path& dest, std::string& error) {
    // Validate before touching any previously imported BIOS.
    Bios proposed;
    if (!proposed.load(path, error)) return false;
    try {
        std::filesystem::create_directories(dest.parent_path());
        if (std::filesystem::equivalent(path, dest)) {
            *this = proposed;
            return true;
        }
    } catch (const std::filesystem::filesystem_error&) {
        // Destination may not yet exist; the copy below creates it.
    }
    auto temp=dest;temp+=".importing";
    try {
        std::filesystem::copy_file(path, temp, std::filesystem::copy_options::overwrite_existing);
        std::error_code ec;
        if(!replaceFile(temp,dest,ec))
            throw std::filesystem::filesystem_error("BIOS replace",temp,dest,ec);
    } catch (const std::filesystem::filesystem_error& e) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        error = std::string("Failed to save BIOS alongside emulator: ") + e.what();
        return false;
    }
    *this = proposed;
    error.clear();
    return true;
}
} // namespace pixelgba
