// Created by Jacob Hodgkins.
#include <cstring>
#include "pixelgba/Cartridge.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <string_view>
#include <zlib.h>

namespace pixelgba {
namespace {
std::string trimHeader(const std::vector<std::uint8_t>& bytes, std::size_t begin, std::size_t end) {
    std::string text;
    for (auto i = begin; i < end; ++i) {
        const auto c = bytes[i];
        if (c == 0 || c == 0xff) break;
        text.push_back((c >= 32 && c < 127) ? static_cast<char>(c) : '?');
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}
}
namespace {
// ZIP central-directory parsing uses length-bounded reads, never unzips to
// disk, and supports only traditional single-volume ZIP32 archives.
constexpr std::size_t MaxArchiveBytes=64u*1024u*1024u;
constexpr std::size_t MaxRomBytes=32u*1024u*1024u;
bool has(std::size_t at,std::size_t length,std::size_t total){
    return at<=total && length<=total-at;
}
std::uint16_t le16(const std::vector<std::uint8_t>& b,std::size_t at){
    return std::uint16_t(b[at]) | (std::uint16_t(b[at+1])<<8);
}
std::uint32_t le32(const std::vector<std::uint8_t>& b,std::size_t at){
    return std::uint32_t(le16(b,at)) | (std::uint32_t(le16(b,at+2))<<16);
}
bool gbaName(std::string_view s){
    if(s.size()<5)return false;
    const auto suffix=s.substr(s.size()-4);
    return (suffix[0]=='.') &&
           (suffix[1]=='g'||suffix[1]=='G') &&
           (suffix[2]=='b'||suffix[2]=='B') &&
           (suffix[3]=='a'||suffix[3]=='A');
}
bool readArchiveRom(const std::vector<std::uint8_t>& archive,
                    std::vector<std::uint8_t>& rom,std::string& error){
    // EOCD must be in the last 65557 bytes, including the optional comment.
    if(archive.size()<22){error="Invalid ZIP: end-of-central-directory missing.";return false;}
    std::size_t eocd=archive.size();
    const std::size_t lower=archive.size()>65557u?archive.size()-65557u:0u;
    for(std::size_t p=archive.size()-22;;--p){
        if(le32(archive,p)==0x06054b50u && has(p,22u+le16(archive,p+20),archive.size()) &&
           p+22u+le16(archive,p+20)==archive.size()){eocd=p;break;}
        if(p==lower)break;
    }
    if(eocd==archive.size()){error="Invalid ZIP: central directory not found.";return false;}
    if(le16(archive,eocd+4)!=0 || le16(archive,eocd+6)!=0 ||
       le16(archive,eocd+8)!=le16(archive,eocd+10)){
        error="Multi-volume ZIP archives are not supported.";return false;
    }
    const auto count=le16(archive,eocd+10);
    const auto directoryBytes=static_cast<std::size_t>(le32(archive,eocd+12));
    const auto directoryAt=static_cast<std::size_t>(le32(archive,eocd+16));
    if(count==0||count>4096 || directoryAt==0xFFFFFFFFu || directoryBytes==0xFFFFFFFFu ||
       !has(directoryAt,directoryBytes,eocd)){
        error="Invalid, unsupported or empty ZIP central directory.";return false;
    }
    const std::size_t directoryEnd=directoryAt+directoryBytes;
    struct Entry {std::uint32_t offset,crc,compressed,uncompressed;unsigned method,flags;};
    Entry selected{};
    unsigned matches=0;
    auto at=directoryAt;
    for(unsigned i=0;i<count;++i){
        if(!has(at,46,directoryEnd) || le32(archive,at)!=0x02014b50u){
            error="Malformed ZIP central directory entry.";return false;
        }
        const std::size_t nameSize=le16(archive,at+28);
        const std::size_t extraSize=le16(archive,at+30);
        const std::size_t commentSize=le16(archive,at+32);
        const std::size_t total=46u+nameSize+extraSize+commentSize;
        if(!has(at,total,directoryEnd)){
            error="Truncated ZIP central directory entry.";return false;
        }
        std::string_view name(reinterpret_cast<const char*>(archive.data()+at+46),nameSize);
        if(gbaName(name)){
            ++matches;
            selected={le32(archive,at+42),le32(archive,at+16),
                      le32(archive,at+20),le32(archive,at+24),
                      le16(archive,at+10),le16(archive,at+8)};
        }
        at+=total;
    }
    if(at!=directoryEnd){error="ZIP central-directory size mismatch.";return false;}
    if(matches!=1){
        error=matches?"ZIP contains multiple .gba ROMs; please select an archive with one.":
                      "ZIP contains no .gba ROM.";return false;
    }
    if((selected.flags&1u)!=0u || (selected.flags&0x40u)!=0u){
        error="Encrypted ZIP ROMs are not supported.";return false;
    }
    if(selected.method!=0 && selected.method!=8){
        error="ZIP ROM requires unsupported compression method.";return false;
    }
    if(selected.uncompressed<0xC0u || selected.uncompressed>MaxRomBytes ||
       selected.compressed>MaxArchiveBytes){
        error="ZIP ROM size is invalid or exceeds the 32 MiB GBA limit.";return false;
    }
    const std::size_t local=selected.offset;
    if(!has(local,30,archive.size()) || le32(archive,local)!=0x04034b50u){
        error="Malformed ZIP local file header.";return false;
    }
    if(le16(archive,local+8)!=selected.method){
        error="ZIP local and central compression methods disagree.";return false;
    }
    const auto filenameBytes=le16(archive,local+26);
    const auto extraBytes=le16(archive,local+28);
    const std::size_t dataStart=local+30u+filenameBytes+extraBytes;
    if(!has(local,30u+filenameBytes+extraBytes,archive.size()) ||
       !has(dataStart,selected.compressed,archive.size()) ||
       dataStart+selected.compressed>directoryAt){
        error="Truncated ZIP ROM data.";return false;
    }
    rom.resize(selected.uncompressed);
    if(selected.method==0){
        if(selected.compressed!=selected.uncompressed){
            error="Stored ZIP ROM has inconsistent lengths.";return false;
        }
        std::copy_n(archive.data()+dataStart,selected.uncompressed,rom.data());
    }else{
        z_stream z{};
        z.next_in=const_cast<Bytef*>(archive.data()+dataStart);
        z.avail_in=selected.compressed;
        z.next_out=rom.data();
        z.avail_out=selected.uncompressed;
        if(inflateInit2(&z,-MAX_WBITS)!=Z_OK){
            error="Could not initialize ZIP DEFLATE decoder.";return false;
        }
        const int result=inflate(&z,Z_FINISH);
        const bool complete=result==Z_STREAM_END &&
                            z.total_in==selected.compressed &&
                            z.total_out==selected.uncompressed;
        inflateEnd(&z);
        if(!complete){error="Truncated or invalid ZIP DEFLATE data.";return false;}
    }
    const auto crc=static_cast<std::uint32_t>(crc32(0,rom.data(),static_cast<uInt>(rom.size())));
    if(crc!=selected.crc){error="ZIP ROM CRC32 mismatch.";return false;}
    return true;
}
bool isZipPath(const std::filesystem::path& path){
    std::string ext=path.extension().u8string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){
        return static_cast<char>(std::tolower(c));});
    return ext==".zip";
}
} // namespace

bool Cartridge::load(const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { error = "Cannot open ROM file."; return false; }
    const auto length = input.tellg();
    const bool zip=isZipPath(path);
    if (length < (zip ? 22 : 0xC0) ||
        length > static_cast<std::streamoff>(zip ? MaxArchiveBytes : MaxRomBytes)) {
        error = zip?"ZIP must be nonempty and no larger than 64 MiB.":
                    "ROM must contain a GBA header and be no larger than 32 MiB.";
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), length)) {
        error = "Could not read the complete ROM.";
        return false;
    }
    if(zip){
        std::vector<std::uint8_t> extracted;
        if(!readArchiveRom(bytes,extracted,error))return false;
        bytes=std::move(extracted);
    }
    if (bytes.size()<0xC0 || bytes[0xB2] != 0x96) {
        error = "Not a recognized GBA ROM (fixed header byte is invalid).";
        return false;
    }
    unsigned checksum = 0;
    for (std::size_t i = 0xA0; i <= 0xBC; ++i) checksum += bytes[i];
    const auto complement = static_cast<std::uint8_t>(0x100u - ((checksum + 0x19u) & 0xffu));
    if (complement != bytes[0xBD]) {
        error = "The GBA ROM header checksum is incorrect.";
        return false;
    }
    title_ = trimHeader(bytes, 0xA0, 0xAC);
    gameCode_ = trimHeader(bytes, 0xAC, 0xB0);
    path_ = path;
    fingerprint_=static_cast<std::uint32_t>(crc32(0,bytes.data(),static_cast<uInt>(bytes.size())));
    romCheatPatches_.clear();
    rom_ = std::move(bytes);
    decodeSize_=1;
    while(decodeSize_<rom_.size())decodeSize_<<=1;
    if(rom_.size()==0x100000u)decodeSize_=0x400000u;
    error.clear();
    return true;
}
bool Cartridge::setRomCheatPatch16(std::uint32_t offset,std::uint16_t value){
    const auto index=offset&0x01FFFFFFu;
    if(!loaded()||(index&1u)||index+1u>=rom_.size())return false;
    romCheatPatches_[index]=value;
    return true;
}
bool Cartridge::containsMarker(const char* marker) const {
    const auto length=std::char_traits<char>::length(marker);
    return std::search(rom_.begin(),rom_.end(),marker,marker+length)!=rom_.end();
}
// Most instruction fetches are inside the actual ROM image. Avoid a costly
// variable integer division on that overwhelmingly common path. Wrapped and
// truncated cartridges keep the exact original modulo behavior.
std::uint8_t Cartridge::read(std::uint32_t offset) const {
    if(rom_.empty())return 0xff;
    const std::size_t index=offset&0x01ffffffu;
    // The Game Pak exposes 16 address bits on its 16-bit data lines when
    // nothing drives the ROM bus. Do not wrap every out-of-range address to
    // ROM byte zero: this previously made unpopulated ROM look like a ROM
    // mirror across the entire 32 MiB window.
    // The address-decode span is precomputed once when the ROM is loaded.
    if(index>=decodeSize_)
        return std::uint8_t((index>>1u)>>(8u*(index&1u)));
    if(!romCheatPatches_.empty()){
        const auto position=index<rom_.size()?index:index%rom_.size();
        const auto it=romCheatPatches_.find(std::uint32_t(position&~std::size_t(1)));
        if(it!=romCheatPatches_.end())return std::uint8_t(it->second>>((position&1u)*8));
    }
    return rom_[index<rom_.size()?index:index%rom_.size()];
}
std::uint16_t Cartridge::read16(std::uint32_t offset) const {
    const std::size_t index=offset&0x01ffffffu;
    if(romCheatPatches_.empty()&&index+2u<=rom_.size()){
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        std::uint16_t value;
        std::memcpy(&value,rom_.data()+index,sizeof(value));
        return value;
#else
        return std::uint16_t(unsigned(rom_[index])|(unsigned(rom_[index+1u])<<8u));
#endif
    }
    return std::uint16_t(unsigned(read(offset))|(unsigned(read(offset+1u))<<8u));
}
std::uint32_t Cartridge::read32(std::uint32_t offset) const {
    const std::size_t index=offset&0x01ffffffu;
    if(romCheatPatches_.empty()&&index+4u<=rom_.size()){
        const auto* b=rom_.data()+index;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        std::uint32_t value;
        std::memcpy(&value,b,sizeof(value));
        return value;
#else
        return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8u)|
               (std::uint32_t(b[2])<<16u)|(std::uint32_t(b[3])<<24u);
#endif
    }
    return std::uint32_t(read16(offset))|(std::uint32_t(read16(offset+2u))<<16u);
}
} // namespace pixelgba
