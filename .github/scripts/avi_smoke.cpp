#include "PortableAVI.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
int main(int argc,char** argv){
    if(argc!=2)return 2;
    ciphergba_host::AviWriter writer;
    std::string error;
    if(!writer.open(std::filesystem::u8path(argv[1]),error)){
        std::fprintf(stderr,"%s\n",error.c_str());return 1;
    }
    std::array<std::uint32_t,240u*160u> frame{};
    std::vector<std::int16_t> audio(2*550u);
    for(unsigned t=0;t<10;t++){
        for(unsigned y=0;y<160;y++)for(unsigned x=0;x<240;x++){
            frame[y*240+x]=((x+t*7u)&255u)<<16|((y+t*5u)&255u)<<8|(t*20u)&255u;
        }
        for(unsigned i=0;i<audio.size();i++)audio[i]=static_cast<std::int16_t>(i%100==0?1500:0);
        if(!writer.appendFrame(frame)||!writer.appendAudio(audio)){
            std::fprintf(stderr,"AVI write failed\n");return 1;
        }
    }
    if(writer.frames()!=10||!writer.close()){
        std::fprintf(stderr,"AVI close failed\n");return 1;
    }
    return 0;
}
