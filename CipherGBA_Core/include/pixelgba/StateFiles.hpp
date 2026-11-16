// HodgkinsStudios CipherGBA: portable-to-same-ABI, checked, per-game disk snapshots.
#pragma once
#include "pixelgba/Core.hpp"
#include <filesystem>
#include <string>
namespace pixelgba {
class StateCodec {
public:
    static bool save(const GbaCore&,const std::filesystem::path&,std::string&);
    static bool load(GbaCore&,const std::filesystem::path&,std::string&);
};
}
