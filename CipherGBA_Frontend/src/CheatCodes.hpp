// HodgkinsStudios CipherGBA - portable GBA cheat-code decoder/executor.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pixelgba::cheats {
enum class Format { Raw, CodeBreaker, GameShark12, ActionReplay34, GameSharkRaw, ActionReplayRaw };
enum class Action { Write, Or, And, Add, Equal, NotEqual, Greater, Less, GreaterEqual, LessEqual,
                    BitSet, BitClear, UnsignedLess, UnsignedGreater, Button, EndIf, Else, WriteIndirect, RomPatch };
struct Operation {
    Action action=Action::Write;
    std::uint32_t address=0, value=0;
    unsigned width=2, count=1, addressStep=0, valueStep=0, skip=0;
};
struct Program {
    std::string source;
    Format format=Format::Raw;
    bool enabled=true;
    std::vector<Operation> instructions;
};
struct Bus {
    std::function<std::uint32_t(std::uint32_t,unsigned)> read;
    std::function<void(std::uint32_t,std::uint32_t,unsigned)> write;
    std::function<bool()> deviceButton; // Mapped to GBA L+R in the CipherGBA frontend.
    std::function<void(std::uint32_t,std::uint16_t)> patchRom16;
};
// Input: RAW:02000000:0000002A:32; CB:32000000 0032; GS:XXXXXXXX YYYYYYYY;
// AR:XXXXXXXX YYYYYYYY. Semicolons join multi-line code groups.
// Unprefixed 12-digit code is CB, 16-digit code defaults GS v1/v2;
// AR v3/v4 requires explicit AR: prefix (the lengths are ambiguous).
bool compile(const std::string& input,Program& out,std::string& error);
void execute(const Program& program,const Bus& bus);
std::string formatName(Format f);
// Exposed for deterministic cryptographic regression vectors.
void encryptGameShark(std::uint32_t& first,std::uint32_t& second,bool v3=false);
void decryptGameShark(std::uint32_t& first,std::uint32_t& second,bool v3=false);
} // namespace pixelgba::cheats
