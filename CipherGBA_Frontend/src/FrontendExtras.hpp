// CipherGBA host-only presentation utilities: PNG, SDL2 queued PCM, AVI capture.
// No emulator-core hardware logic lives here.
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <string>
#include <vector>
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <zlib.h>
namespace pixelgba {
namespace host {
inline void writeBe32(std::vector<std::uint8_t>& b,std::uint32_t x){for(int i=3;i>=0;--i)b.push_back(std::uint8_t(x>>(8*i)));}
inline void pngChunk(std::vector<std::uint8_t>& output,const char* type,const std::vector<std::uint8_t>& data){
    writeBe32(output,std::uint32_t(data.size()));
    const auto start=output.size();for(unsigned i=0;i<4;i++)output.push_back(type[i]);
    output.insert(output.end(),data.begin(),data.end());
    writeBe32(output,std::uint32_t(crc32(0L,output.data()+start,uInt(output.size()-start))));
}
// Source pixels: CipherGBA 0x00RRGGBB, GBA native 240x160. PNG is lossless RGBA.
inline bool savePng(const std::filesystem::path& path,const std::uint32_t* pixels,int w,int h){
    if(!pixels||w<=0||h<=0)return false;
    std::vector<std::uint8_t> scanlines(std::size_t(h)*std::size_t(1+4*w));
    for(int y=0;y<h;y++){
        const auto row=std::size_t(y)*std::size_t(1+4*w);
        for(int x=0;x<w;x++){
            const auto v=pixels[std::size_t(y)*w+x];const auto a=row+1u+4u*std::size_t(x);
            scanlines[a]=std::uint8_t(v>>16);scanlines[a+1]=std::uint8_t(v>>8);
            scanlines[a+2]=std::uint8_t(v);scanlines[a+3]=255;
        }
    }
    uLongf len=compressBound(uLong(scanlines.size()));
    std::vector<std::uint8_t> compressed(len);
    if(compress2(compressed.data(),&len,scanlines.data(),uLong(scanlines.size()),Z_BEST_SPEED)!=Z_OK)return false;
    compressed.resize(len);
    std::vector<std::uint8_t> output={137,80,78,71,13,10,26,10};
    std::vector<std::uint8_t> ihdr;writeBe32(ihdr,std::uint32_t(w));writeBe32(ihdr,std::uint32_t(h));
    ihdr.insert(ihdr.end(),{8,6,0,0,0});pngChunk(output,"IHDR",ihdr);
    pngChunk(output,"IDAT",compressed);pngChunk(output,"IEND",{});
    std::FILE* fp=std::fopen(path.c_str(),"wb");if(!fp)return false;
    const bool wrote=std::fwrite(output.data(),1,output.size(),fp)==output.size();
    const bool closed=std::fclose(fp)==0;const bool ok=wrote&&closed;
    if(!ok){std::error_code ec;std::filesystem::remove(path,ec);}return ok;
}
class SdlAudio {
    struct Spec {int freq;std::uint16_t format;std::uint8_t channels;std::uint8_t silence;
        std::uint16_t samples;std::uint16_t padding;std::uint32_t size;
        void(*callback)(void*,std::uint8_t*,int);void* userdata;};
    using Device=std::uint32_t;
    void* module_=nullptr;Device device_=0;
    int (*init_)(std::uint32_t)=nullptr;
    void (*quit_)(std::uint32_t)=nullptr;
    Device (*open_)(const char*,int,const Spec*,Spec*,int)=nullptr;
    void (*close_)(Device)=nullptr;
    void (*pause_)(Device,int)=nullptr;
    int (*queue_)(Device,const void*,std::uint32_t)=nullptr;
    std::uint32_t (*size_)(Device)=nullptr;
    void (*clear_)(Device)=nullptr;
    template<typename T>bool bind(T& fn,const char* n){fn=reinterpret_cast<T>(dlsym(module_,n));return fn!=nullptr;}
public:
    SdlAudio(){
        module_=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_LOCAL);if(!module_)return;
        if(!bind(init_,"SDL_InitSubSystem")||!bind(quit_,"SDL_QuitSubSystem")||
            !bind(open_,"SDL_OpenAudioDevice")||!bind(close_,"SDL_CloseAudioDevice")||
            !bind(pause_,"SDL_PauseAudioDevice")||!bind(queue_,"SDL_QueueAudio")||
            !bind(size_,"SDL_GetQueuedAudioSize")||!bind(clear_,"SDL_ClearQueuedAudio"))return;
        if(init_(0x10u)!=0)return;
        Spec want{};want.freq=32768;want.format=0x8010;want.channels=2;want.samples=1024;
        Spec got{};device_=open_(nullptr,0,&want,&got,0);
        if(device_)pause_(device_,0);
    }
    ~SdlAudio(){if(device_&&close_)close_(device_);if(quit_)quit_(0x10u);if(module_)dlclose(module_);}
    bool available()const{return device_!=0;}
    void reset(){if(device_)clear_(device_);}
    // Output-only measurement; never feeds back into the GBA's clock.
    double queuedMilliseconds()const{
        return device_ ? double(size_(device_))*(1000.0/(32768.0*4.0)) : 0.0;
    }
    void push(const std::vector<std::int16_t>& samples,bool audible){
        if(!device_||!audible||samples.empty())return;
        if(size_(device_)>32768u){clear_(device_);} // Avoid unbounded latency after an OS stall.
        queue_(device_,samples.data(),std::uint32_t(samples.size()*sizeof(std::int16_t)));
    }
};
// One encoder child accepts BGRA frames; PCM is spooled and muxed into the AVI
// after close. Keeping video and audio pipes separate prevents interleaved-pipe stalls.
class AviRecorder {
    int fd_=-1;pid_t pid_=-1;
    std::filesystem::path file_,videoTemp_,audioTemp_;
    std::FILE* audioFile_=nullptr;
    bool overlayToast_=false,overlayTimer_=false;
    std::uint64_t frames_=0;
    static void glyph(char c,std::array<std::uint8_t,5>& row){
        switch(c){
        case '0':row={7,5,5,5,7};break; case '1':row={2,6,2,2,7};break;
        case '2':row={7,1,7,4,7};break; case '3':row={7,1,7,1,7};break;
        case '4':row={5,5,7,1,1};break; case '5':row={7,4,7,1,7};break;
        case '6':row={7,4,7,5,7};break; case '7':row={7,1,1,1,1};break;
        case '8':row={7,5,7,5,7};break; case '9':row={7,5,7,1,7};break;
        case 'R':row={6,5,6,5,5};break; case 'E':row={7,4,6,4,7};break;
        case 'C':row={7,4,4,4,7};break; case ':':row={0,2,0,2,0};break;
        default:row={0,0,0,0,0};break;
        }
    }
    static void overlay(std::array<std::uint32_t,240*160>& frame,const std::string& message){
        if(message.empty())return;
        const int width=std::min(240,9+int(message.size())*8);
        for(int y=0;y<19;y++)for(int x=0;x<width;x++)frame[std::size_t(y)*240u+std::size_t(x)]=0xff09111cu;
        for(std::size_t j=0;j<message.size();j++){
            std::array<std::uint8_t,5> rows{};glyph(message[j],rows);
            for(int y=0;y<5;y++)for(int x=0;x<3;x++)if((rows[y]>>(2-x))&1u){
                for(int yy=0;yy<2;yy++)for(int xx=0;xx<2;xx++){
                    const int px=5+int(j)*8+x*2+xx,py=4+y*2+yy;
                    if(px<240&&py<160)frame[std::size_t(py)*240u+std::size_t(px)]=0xffe64b49u;
                }
            }
        }
    }
    static bool runMux(const std::filesystem::path& video,const std::filesystem::path& pcm,
                       const std::filesystem::path& output){
        const pid_t child=fork();if(child<0)return false;
        if(child==0){
            const char* args[]={"ffmpeg","-y","-hide_banner","-loglevel","error", "-i",video.c_str(),
                 "-f","s16le","-ar","32768","-ac","2","-i",pcm.c_str(),
                 "-map","0:v:0","-map","1:a:0","-c:v","copy","-c:a","pcm_s16le",
                 "-shortest",output.c_str(),nullptr};
            execvp("ffmpeg",const_cast<char* const*>(args));_exit(127);
        }
        int stat=0;while(waitpid(child,&stat,0)<0&&errno==EINTR){}
        return WIFEXITED(stat)&&WEXITSTATUS(stat)==0;
    }
public:
    ~AviRecorder(){stop();}
    bool active()const{return fd_>=0;}
    const std::filesystem::path& path()const{return file_;}
    bool start(const std::filesystem::path& output,bool toast=false,bool timer=false){
        stop();
        // A missing/failed external encoder must never SIGPIPE-kill the emulator.
        signal(SIGPIPE,SIG_IGN);
        if(::access("/usr/bin/ffmpeg",X_OK)!=0&&::access("/bin/ffmpeg",X_OK)!=0)return false;
        file_=output;videoTemp_=std::filesystem::path(output.string()+".video.avi");
        audioTemp_=std::filesystem::path(output.string()+".audio.raw");
        audioFile_=std::fopen(audioTemp_.c_str(),"wb");if(!audioFile_)return false;
        overlayToast_=toast;overlayTimer_=timer;frames_=0;
        int pair[2];if(pipe(pair)!=0){std::fclose(audioFile_);audioFile_=nullptr;return false;}
        pid_=fork();
        if(pid_<0){close(pair[0]);close(pair[1]);std::fclose(audioFile_);audioFile_=nullptr;return false;}
        if(pid_==0){
            dup2(pair[0],STDIN_FILENO);close(pair[0]);close(pair[1]);
            const char* args[]={"ffmpeg","-y","-hide_banner","-loglevel","error","-f","rawvideo",
               "-pixel_format","bgra","-video_size","240x160","-framerate","59.7275",
               "-i","pipe:0","-an","-c:v","mjpeg","-q:v","4",videoTemp_.c_str(),nullptr};
            execvp("ffmpeg",const_cast<char* const*>(args));_exit(127);
        }
        close(pair[0]);fd_=pair[1];return true;
    }
    void append(const std::uint32_t* pixels){
        if(fd_<0||!pixels)return;
        std::array<std::uint32_t,240*160> frame{};
        for(std::size_t i=0;i<frame.size();++i)frame[i]=pixels[i]|0xff000000u;
        std::string stamp;
        if(overlayToast_)stamp="REC";
        if(overlayTimer_){
            const auto seconds=std::uint64_t(double(frames_)/59.7275);
            char buf[18];std::snprintf(buf,sizeof(buf),"%02llu:%02llu:%02llu",
                (unsigned long long)((seconds/3600)%100),
                (unsigned long long)((seconds/60)%60),(unsigned long long)(seconds%60));
            if(!stamp.empty())stamp+=" ";
            stamp+=buf;
        }
        overlay(frame,stamp);
        const char* data=reinterpret_cast<const char*>(frame.data());std::size_t left=sizeof(frame);
        while(left){const auto n=write(fd_,data,left);if(n<=0){stop();return;}left-=std::size_t(n);data+=n;}
        ++frames_;
    }
    void appendAudio(const std::int16_t* samples,std::size_t count){
        if(audioFile_&&samples&&count)std::fwrite(samples,sizeof(std::int16_t),count,audioFile_);
    }
    void stop(){
        if(fd_>=0){close(fd_);fd_=-1;}
        bool videoGood=false;
        if(pid_>0){int stat=0;while(waitpid(pid_,&stat,0)<0&&errno==EINTR){}
            videoGood=WIFEXITED(stat)&&WEXITSTATUS(stat)==0;pid_=-1;}
        if(audioFile_){std::fclose(audioFile_);audioFile_=nullptr;}
        if(!videoTemp_.empty()){
            std::error_code ec;
            if(videoGood && std::filesystem::exists(videoTemp_,ec)){
                const bool audioPresent=std::filesystem::exists(audioTemp_,ec)&&
                   std::filesystem::file_size(audioTemp_,ec)>0;
                const bool muxed=audioPresent&&runMux(videoTemp_,audioTemp_,file_);
                if(!muxed){
                    std::filesystem::remove(file_,ec);
                    std::filesystem::rename(videoTemp_,file_,ec);
                }
            }
            std::filesystem::remove(videoTemp_,ec);
            std::filesystem::remove(audioTemp_,ec);
            videoTemp_.clear();audioTemp_.clear();
        }
    }
};
} // namespace host
} // namespace pixelgba
