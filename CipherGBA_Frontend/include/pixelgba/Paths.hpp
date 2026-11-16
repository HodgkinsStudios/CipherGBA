// Created by Jacob Hodgkins.
#pragma once
#include <filesystem>
namespace pixelgba {
struct Paths {
    std::filesystem::path executableDirectory;
    std::filesystem::path dataDirectory;
    std::filesystem::path biosFile;
    std::filesystem::path inputConfig;
    static Paths discover();
};
}
