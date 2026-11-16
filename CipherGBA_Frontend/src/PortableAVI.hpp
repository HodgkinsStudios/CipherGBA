// Standalone portable RIFF AVI writer for CipherGBA's optional video capture.
// No OS APIs, ffmpeg subprocess or emulator hardware state is involved.
// Output: 240x160 BGR24 uncompressed video, 32768 Hz 16-bit stereo PCM audio.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
namespace ciphergba_host {
class AviWriter {
    struct Entry{char id[4];std::uint32_t flags=0,offset=0,length=0;};
    std::fstream stream_;
    std::vector<Entry> index_;
    std::array<std::uint8_t,240u*160u*3u> frame_{};
    std::uint32_t frames_=0,sampleFrames_=0;
    std::streamoff riffSize_=0,hdrlSize_=0,moviSize_=0,moviPosition_=0;
    std::streamoff totalFramesPosition_=0,videoLengthPosition_=0,audioLengthPosition_=0;
    static constexpr std::uint64_t MaxFile=1950000000ull; // Classic AVI 32-bit offsets.
    static constexpr std::uint32_t FrameBytes=240u*160u*3u;
    void fourcc(const char* s){stream_.write(s,4);}
    void word(std::uint16_t x){stream_.put(char(x));stream_.put(char(x>>8));}
    void dword(std::uint32_t x){for(unsigned i=0;i<4;i++)stream_.put(char(x>>(8*i)));}
    void patch(std::streamoff at,std::uint32_t value){
        const auto pos=stream_.tellp();stream_.seekp(at);
        dword(value);stream_.seekp(pos);
    }
    std::streamoff list(const char* tag){
        fourcc("LIST");
        const auto off=static_cast<std::streamoff>(stream_.tellp());
        dword(0);fourcc(tag);return off;
    }
    void closeList(std::streamoff sizePosition){
        const auto here=static_cast<std::streamoff>(stream_.tellp());
        patch(sizePosition,static_cast<std::uint32_t>(here-sizePosition-4));
    }
    void chunk(const char* tag,const void* data,std::uint32_t bytes,bool video){
        const auto here=static_cast<std::streamoff>(stream_.tellp());
        if(here<0 || std::uint64_t(here)+bytes+24+index_.size()*16>MaxFile){
            return;
        }
        fourcc(tag);dword(bytes);
        if(bytes)stream_.write(reinterpret_cast<const char*>(data),bytes);
        if(bytes&1u)stream_.put(char(0));
        if(!stream_)return;
        Entry entry{};
        for(unsigned i=0;i<4;i++)entry.id[i]=tag[i];
        entry.flags=video?0x10u:0u;
        entry.offset=static_cast<std::uint32_t>(here-moviPosition_-8);
        entry.length=bytes;
        index_.push_back(entry);
    }
public:
    ~AviWriter(){close();}
    bool active()const{return stream_.is_open();}
    std::uint32_t frames()const{return frames_;}
    bool open(const std::filesystem::path& path,std::string& error){
        close();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(),ec);
        if(ec){error=ec.message();return false;}
        stream_.open(path,std::ios::binary|std::ios::out|std::ios::in|std::ios::trunc);
        if(!stream_){error="Cannot open AVI destination";return false;}
        fourcc("RIFF");riffSize_=static_cast<std::streamoff>(stream_.tellp());
        dword(0);fourcc("AVI ");
        hdrlSize_=list("hdrl");
        fourcc("avih");dword(56);
        dword(16743); // 1,000,000 / (16,777,216 / (1232*228)).
        dword(7500000);dword(0);dword(0x10u); // AVIF_HASINDEX
        totalFramesPosition_=static_cast<std::streamoff>(stream_.tellp());dword(0);
        dword(0);dword(2);dword(FrameBytes);dword(240);dword(160);
        for(unsigned i=0;i<4;i++)dword(0);
        const auto vidList=list("strl");
        fourcc("strh");dword(56);fourcc("vids");fourcc("DIB ");
        dword(0);word(0);word(0);dword(0);
        dword(10000);dword(597275);dword(0);
        videoLengthPosition_=static_cast<std::streamoff>(stream_.tellp());dword(0);
        dword(FrameBytes);dword(0xFFFFFFFFu);dword(0);
        word(0);word(0);word(240);word(160);
        fourcc("strf");dword(40);
        dword(40);dword(240);dword(160);word(1);word(24);
        dword(0);dword(FrameBytes);dword(0);dword(0);dword(0);dword(0);
        closeList(vidList);
        const auto audioList=list("strl");
        fourcc("strh");dword(56);fourcc("auds");dword(0);
        dword(0);word(0);word(0);dword(0);
        dword(4);dword(131072);dword(0);
        audioLengthPosition_=static_cast<std::streamoff>(stream_.tellp());dword(0);
        dword(8192);dword(0xFFFFFFFFu);dword(4);
        word(0);word(0);word(0);word(0);
        fourcc("strf");dword(16);word(1);word(2);
        dword(32768);dword(131072);word(4);word(16);
        closeList(audioList);
        closeList(hdrlSize_);
        moviSize_=list("movi");
        moviPosition_=moviSize_-4; // 'LIST' begins four bytes before size field.
        frames_=sampleFrames_=0;index_.clear();
        if(!stream_){error="AVI header write failed";close();return false;}
        error.clear();return true;
    }
    bool appendFrame(const std::array<std::uint32_t,240u*160u>& rgb){
        if(!active())return false;
        // Source pixels are 0x00RRGGBB, output is bottom-up DIB BGR24.
        std::size_t dst=0;
        for(int y=159;y>=0;--y)for(unsigned x=0;x<240;++x){
            const auto pixel=rgb[std::size_t(y)*240u+x];
            frame_[dst++]=std::uint8_t(pixel);
            frame_[dst++]=std::uint8_t(pixel>>8);
            frame_[dst++]=std::uint8_t(pixel>>16);
        }
        const auto before=index_.size();
        chunk("00db",frame_.data(),FrameBytes,true);
        if(index_.size()==before)return false;
        ++frames_;return true;
    }
    bool appendAudio(const std::vector<std::int16_t>& samples){
        if(!active())return false;
        if(samples.empty())return true;
        if(samples.size()>std::numeric_limits<std::uint32_t>::max()/2 || samples.size()%2)
            return false;
        const auto count=static_cast<std::uint32_t>(samples.size()/2);
        if(sampleFrames_>std::numeric_limits<std::uint32_t>::max()-count)return false;
        const auto before=index_.size();
        chunk("01wb",samples.data(),static_cast<std::uint32_t>(samples.size()*2),false);
        if(before==index_.size())return false;
        sampleFrames_+=count;return true;
    }
    bool close(){
        if(!active())return true;
        const auto moviEnd=static_cast<std::streamoff>(stream_.tellp());
        patch(moviSize_,static_cast<std::uint32_t>(moviEnd-moviSize_-4));
        fourcc("idx1");dword(static_cast<std::uint32_t>(index_.size()*16u));
        for(const auto& item:index_){
            fourcc(item.id);dword(item.flags);dword(item.offset);dword(item.length);
        }
        patch(totalFramesPosition_,frames_);
        patch(videoLengthPosition_,frames_);
        patch(audioLengthPosition_,sampleFrames_);
        const auto end=static_cast<std::streamoff>(stream_.tellp());
        patch(riffSize_,static_cast<std::uint32_t>(end-8));
        stream_.flush();
        const bool okay=bool(stream_);
        stream_.close();index_.clear();
        frames_=sampleFrames_=0;
        return okay;
    }
};
}
