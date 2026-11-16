// Portable CGTAS01 movie persistence compatible with the original X11 UI.
// This is frontend input state, not an emulator hardware modification.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
namespace ciphergba_host {
class Tas {
    std::vector<std::uint16_t> frames_;
    std::size_t cursor_=0;
    bool recording_=false,playback_=false;
    static void put16(std::ostream& f,std::uint16_t n){
        f.put(char(n));f.put(char(n>>8));
    }
    static void put32(std::ostream& f,std::uint32_t n){
        for(unsigned i=0;i<4;i++)f.put(char(n>>(i*8)));
    }
    static bool get16(std::istream& f,std::uint16_t& n){
        unsigned char a=0,b=0;char x=0,y=0;
        if(!f.get(x)||!f.get(y))return false;
        a=static_cast<unsigned char>(x);b=static_cast<unsigned char>(y);
        n=std::uint16_t(a|(unsigned(b)<<8));return true;
    }
    static bool get32(std::istream& f,std::uint32_t& n){
        n=0;
        for(unsigned i=0;i<4;i++){
            char c=0;if(!f.get(c))return false;
            n|=std::uint32_t(static_cast<unsigned char>(c))<<(8*i);
        }
        return true;
    }
public:
    static constexpr std::size_t MaxFrames=10000000u;
    bool recording()const{return recording_;}
    bool playback()const{return playback_;}
    std::size_t count()const{return frames_.size();}
    std::size_t cursor()const{return cursor_;}
    void clear(){recording_=playback_=false;frames_.clear();cursor_=0;}
    void begin(){clear();recording_=true;}
    void stopPlayback(){playback_=false;}
    bool consume(std::uint16_t live,std::uint16_t& output){
        output=live;
        if(playback_){
            if(cursor_<frames_.size())output=frames_[cursor_++];
            else playback_=false;
            if(cursor_>=frames_.size())playback_=false;
        }
        if(recording_){
            if(frames_.size()<MaxFrames)frames_.push_back(output);
            else return false;
        }
        return true;
    }
    bool save(const std::filesystem::path& file,std::uint32_t rom,std::string& error){
        if(!recording_)return true;
        recording_=false;
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(),ec);
        if(ec){error=ec.message();return false;}
        std::ofstream f(file,std::ios::binary|std::ios::trunc);
        if(!f){error="TAS destination not writable";return false;}
        const char tag[8]={'C','G','T','A','S','0','1','\0'};
        f.write(tag,8);put32(f,rom);put32(f,static_cast<std::uint32_t>(frames_.size()));
        for(auto n:frames_)put16(f,n);
        f.flush();f.close();
        if(!f){error="TAS write incomplete";return false;}
        error.clear();return true;
    }
    bool load(const std::filesystem::path& file,std::uint32_t rom,std::string& error){
        std::ifstream f(file,std::ios::binary|std::ios::ate);
        if(!f){error="No saved TAS movie for this ROM";return false;}
        const auto size=f.tellg();
        if(size<std::streamoff(16)||size>std::streamoff(16+MaxFrames*2)){
            error="Invalid TAS file size";return false;
        }
        f.seekg(0);char tag[8]{};
        std::uint32_t recordedRom=0,count=0;
        const char expected[8]={'C','G','T','A','S','0','1','\0'};
        if(!f.read(tag,8)||!std::equal(tag,tag+8,expected)||
           !get32(f,recordedRom)||!get32(f,count)||
           recordedRom!=rom||count>MaxFrames||
           size!=std::streamoff(16+count*2u)){
            error="TAS format, ROM CRC or length mismatch";return false;
        }
        std::vector<std::uint16_t> frames(count);
        for(auto& n:frames){
            if(!get16(f,n)||n>1023u){error="TAS data corrupt";return false;}
        }
        frames_=std::move(frames);cursor_=0;recording_=false;playback_=true;
        error.clear();return true;
    }
};
}
