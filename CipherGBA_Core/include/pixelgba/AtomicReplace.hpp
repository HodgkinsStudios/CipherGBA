// Cross-platform replacement of a completed temporary file.
// On Windows std::filesystem::rename does not overwrite existing destinations.
// Keep the previous save intact if replacement fails.
#pragma once
#include <filesystem>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace pixelgba {
inline bool replaceFile(const std::filesystem::path& from,
                        const std::filesystem::path& to,
                        std::error_code& error) {
#ifdef _WIN32
    if(::MoveFileExW(from.c_str(),to.c_str(),
       MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        error.clear();return true;
    }
    error=std::error_code(static_cast<int>(::GetLastError()),std::system_category());
    return false;
#else
    std::filesystem::rename(from,to,error);
    return !error;
#endif
}
} // namespace pixelgba
