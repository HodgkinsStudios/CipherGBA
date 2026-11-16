// CipherGBA portable SDL2 frontend.
// Copyright HodgkinsStudios. Uses the existing GbaCore implementation.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "pixelgba/Core.hpp"
#include "pixelgba/StateFiles.hpp"
#include "OpenFileDialog.hpp"
#include "CheatCodes.hpp"
#include "PortableTAS.hpp"
#include "PortableAVI.hpp"
#include "BrandArtwork.hpp"
#include "AboutFont.hpp"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
constexpr int kWidth=240, kHeight=160;
constexpr double kFramesPerSecond=59.7275;
constexpr std::uint32_t kButtons[10]={1,2,4,8,16,32,64,128,256,512};
void errorMessage(SDL_Window* window,const std::string& message) {
    std::fprintf(stderr,"CipherGBA: %s\n",message.c_str());
    if(window)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"CipherGBA",message.c_str(),window);
}
fs::path userPath(const std::string& utf8){return fs::u8path(utf8);}
std::string fileName(const fs::path& p){return p.filename().u8string();}
void writeBig32(std::vector<std::uint8_t>& out,std::uint32_t value) {
    for(int shift=24;shift>=0;shift-=8)out.push_back(static_cast<std::uint8_t>(value>>shift));
}
void pngChunk(std::vector<std::uint8_t>& png,const char* tag,const std::vector<std::uint8_t>& payload) {
    writeBig32(png,static_cast<std::uint32_t>(payload.size()));
    const auto begin=png.size();
    for(int i=0;i<4;i++)png.push_back(static_cast<std::uint8_t>(tag[i]));
    png.insert(png.end(),payload.begin(),payload.end());
    const auto crc=crc32(0L,png.data()+begin,static_cast<uInt>(png.size()-begin));
    writeBig32(png,static_cast<std::uint32_t>(crc));
}
bool savePng(const fs::path& destination,const std::array<std::uint32_t,kWidth*kHeight>& frame) {
    std::vector<std::uint8_t> raw;
    raw.reserve(kHeight*(1+kWidth*3));
    for(int y=0;y<kHeight;y++){
        raw.push_back(0); // PNG filter: None
        for(int x=0;x<kWidth;x++){
            const std::uint32_t rgb=frame[static_cast<std::size_t>(y*kWidth+x)];
            raw.push_back(static_cast<std::uint8_t>(rgb>>16));
            raw.push_back(static_cast<std::uint8_t>(rgb>>8));
            raw.push_back(static_cast<std::uint8_t>(rgb));
        }
    }
    uLongf compressedSize=compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(static_cast<std::size_t>(compressedSize));
    if(compress2(compressed.data(),&compressedSize,raw.data(),
                 static_cast<uLong>(raw.size()),Z_BEST_SPEED)!=Z_OK)return false;
    compressed.resize(static_cast<std::size_t>(compressedSize));
    std::vector<std::uint8_t> png{137,80,78,71,13,10,26,10};
    std::vector<std::uint8_t> ihdr;
    writeBig32(ihdr,kWidth);writeBig32(ihdr,kHeight);
    ihdr.insert(ihdr.end(),{8,2,0,0,0});
    pngChunk(png,"IHDR",ihdr);
    pngChunk(png,"IDAT",compressed);
    pngChunk(png,"IEND",{});
    std::ofstream out(destination,std::ios::binary|std::ios::trunc);
    if(!out)return false;
    out.write(reinterpret_cast<const char*>(png.data()),static_cast<std::streamsize>(png.size()));
    out.close();
    return bool(out);
}
struct Application {
    pixelgba::GbaCore core;
    SDL_Window* window=nullptr;
    SDL_Renderer* renderer=nullptr;
    SDL_Texture* texture=nullptr;
    // Artwork is decoded only by the About dialog; never a startup splash.
    SDL_GameController* controller=nullptr;
    SDL_AudioDeviceID audio=0;
    fs::path data;
    std::vector<pixelgba::cheats::Program> cheats_;
    bool cheatsEnabled_=false;
    ciphergba_host::Tas tas_;
    ciphergba_host::AviWriter avi_;
    std::array<std::uint32_t,kWidth*kHeight> rgba{};
    bool running=true,paused=false,fullscreen=false,stretched=false,muted=false;
    int speedPercent_=100;
    enum class RemapMode { None, Keyboard, Gamepad };
    RemapMode remap_=RemapMode::None;
    unsigned remapIndex_=0;
    bool pausedBeforeInput_=false;
    const std::array<const char*,10> buttonNames_{{"A","B","SELECT","START",
        "RIGHT","LEFT","UP","DOWN","R","L"}};
    using KeyMap=std::array<SDL_Scancode,10>;
    using PadMap=std::array<SDL_GameControllerButton,10>;
    const KeyMap defaultKeys_{{SDL_SCANCODE_Z,SDL_SCANCODE_X,SDL_SCANCODE_BACKSPACE,
        SDL_SCANCODE_RETURN,SDL_SCANCODE_RIGHT,SDL_SCANCODE_LEFT,SDL_SCANCODE_UP,
        SDL_SCANCODE_DOWN,SDL_SCANCODE_S,SDL_SCANCODE_A}};
    const PadMap defaultPad_{{SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_BACK,SDL_CONTROLLER_BUTTON_START,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT,SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_UP,SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,SDL_CONTROLLER_BUTTON_LEFTSHOULDER}};
    KeyMap keyMap_=defaultKeys_,oldKeyMap_=defaultKeys_;
    PadMap padMap_=defaultPad_,oldPadMap_=defaultPad_;

    int stateSlot=1;
    int uiMenu_=-1, uiMouseX_=-1, uiMouseY_=-1;
    std::string romName;
    std::chrono::steady_clock::time_point frameDeadline{};
    ~Application() {
        avi_.close();
        if(core.cartridge().loaded()){
            std::string error;
            if(tas_.recording())tas_.save(tasPath(),core.cartridge().fingerprint(),error);
            if(!core.flushSave(error)&&!error.empty())
                std::fprintf(stderr,"CipherGBA save warning: %s\n",error.c_str());
        }
        if(audio)SDL_CloseAudioDevice(audio);
        if(controller)SDL_GameControllerClose(controller);
        if(texture)SDL_DestroyTexture(texture);
        if(renderer)SDL_DestroyRenderer(renderer);
        if(window)SDL_DestroyWindow(window);
        SDL_Quit();
    }
    bool init() {
        if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)!=0){
            errorMessage(nullptr,SDL_GetError());return false;
        }
        window=SDL_CreateWindow("CipherGBA v0.1 - F1 Menu | Ctrl+O Open ROM",
            SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,720,480,
            SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        if(!window){errorMessage(nullptr,SDL_GetError());return false;}
        SDL_SetWindowMinimumSize(window,560,360);
        renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
        if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        if(!renderer){errorMessage(window,SDL_GetError());return false;}
        texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING,kWidth,kHeight);
        if(!texture){errorMessage(window,SDL_GetError());return false;}
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
        // Same embedded image for Linux/Windows/macOS window chrome and About.
        // The icon does not change GBA hardware emulation or frame timing.
        std::vector<unsigned char> iconPixels;
        if(pixelgba::artwork::decode(pixelgba::artwork::kIcon64,iconPixels)){
            SDL_Surface* icon=SDL_CreateRGBSurfaceWithFormatFrom(iconPixels.data(),
                64,64,32,64*4,SDL_PIXELFORMAT_RGBA32);
            if(icon){SDL_SetWindowIcon(window,icon);SDL_FreeSurface(icon);}
        }
        if(char* pref=SDL_GetPrefPath("HodgkinsStudios","CipherGBA")){
            data=fs::u8path(pref);
            SDL_free(pref);
        }else{
            errorMessage(window,"Unable to determine a writable user data directory");
            return false;
        }
        std::string error;
        if(!core.configureSaveDirectory(data/"saves",error)){
            errorMessage(window,"Save directory: "+error);return false;
        }
        const auto installed=data/"bios"/"gba_bios.bin";
        std::error_code ec;
        if(fs::is_regular_file(installed,ec) && !core.loadInstalledBios(installed,error))
            std::fprintf(stderr,"CipherGBA BIOS warning: %s\n",error.c_str());
        if(SDL_InitSubSystem(SDL_INIT_AUDIO)==0){
            SDL_AudioSpec wanted{},obtained{};
            wanted.freq=32768;wanted.format=AUDIO_S16SYS;
            wanted.channels=2;wanted.samples=1024;
            audio=SDL_OpenAudioDevice(nullptr,0,&wanted,&obtained,0);
            if(audio)SDL_PauseAudioDevice(audio,0);
        }
        for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_IsGameController(i)){
            controller=SDL_GameControllerOpen(i);
            if(controller)break;
        }
        loadBindings();
        frameDeadline=std::chrono::steady_clock::now();
        return true;
    }
    // Explicit, non-destructive migration from the existing per-executable data
    // directory to SDL2's OS-standard user data directory. No overwrite and
    // no copy of incompatible X11 input maps or arbitrary nested files.
    bool importLegacyData(fs::path oldData){
        std::error_code ec;
        if(fs::is_directory(oldData/"CipherGBA_Data",ec))
            oldData/="CipherGBA_Data";
        if(!fs::is_directory(oldData,ec)){
            message("Import directory does not exist: "+oldData.u8string());
            return false;
        }
        struct Source {const char* folder,*extension;};
        const std::array<Source,5> sources{{
            {"saves",".sav"},{"bios",".bin"},{"states",".cgs"},
            {"tas",".cgtas"},{"cheats",".txt"}
        }};
        unsigned copied=0,skipped=0,failed=0;
        for(const auto& source:sources){
            const auto sourceDir=oldData/source.folder;
            ec.clear();
            if(!fs::is_directory(sourceDir,ec))continue;
            for(fs::directory_iterator it(sourceDir,ec),end;it!=end && !ec;
                it.increment(ec)){
                std::error_code fileError;
                if(it->is_symlink(fileError)||!it->is_regular_file(fileError))continue;
                std::string ext=it->path().extension().u8string();
                std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){
                    return static_cast<char>(std::tolower(c));});
                if(ext!=source.extension)continue;
                const auto length=it->file_size(fileError);
                if(fileError||length>16u*1024u*1024u){++failed;continue;}
                const auto destination=data/source.folder/it->path().filename();
                fs::create_directories(destination.parent_path(),fileError);
                if(fileError){++failed;continue;}
                if(fs::copy_file(it->path(),destination,
                      fs::copy_options::skip_existing,fileError))++copied;
                else if(fileError)++failed;
                else ++skipped;
            }
            if(ec)++failed;
        }
        const auto installed=data/"bios"/"gba_bios.bin";
        ec.clear();
        if(fs::is_regular_file(installed,ec) && !core.biosLoaded()){
            std::string error;
            if(!core.loadInstalledBios(installed,error))
                std::fprintf(stderr,"Imported BIOS warning: %s\n",error.c_str());
        }
        std::printf("Data import: copied=%u existing=%u errors=%u\n",
            copied,skipped,failed);
        if(failed)message("Import incomplete; inspect terminal errors and source permissions");
        // Existing files are never overwritten and the original folder is unchanged.
        return failed==0;
    }

    void message(const std::string& message){
        errorMessage(window,message);
    }
    bool loadRom(const fs::path& file){
        std::string ext=file.extension().u8string();
        std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){
            return static_cast<char>(std::tolower(c));});
        if(ext!=".gba"&&ext!=".zip"){
            message("Drop a .gba file or ZIP archive containing a GBA ROM");
            return false;
        }
        std::string error;
        if(!core.loadCartridge(file,error)){
            message("Unable to open ROM: "+error);return false;
        }
        romName=fileName(file);
        tas_.clear();
        avi_.close();
        cheats_.clear();
        core.clearRomCheatPatches();
        loadCheats();
        cheatsEnabled_=false;
        paused=false;
        if(audio)SDL_ClearQueuedAudio(audio);
        frameDeadline=std::chrono::steady_clock::now();
        SDL_SetWindowTitle(window,("CipherGBA v0.1 - "+romName).c_str());
        return true;
    }
    bool loadBios(const fs::path& file){
        std::string error;
        if(!core.loadInstalledBios(file,error)){
            message("Unable to open BIOS: "+error);return false;
        }
        if(audio)SDL_ClearQueuedAudio(audio);
        return true;
    }
    fs::path statePath() const{
        // Match the existing X11 state names on every desktop OS.
        std::ostringstream tag;
        tag<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(8)
           <<core.cartridge().fingerprint();
        return data/"states"/(tag.str()+
            (core.biosLoaded()?"_bios_":"_hle_")+"slot"+
            std::to_string(stateSlot)+".cgs");
    }
    fs::path legacySdlStatePath()const{
        return data/"states"/(std::to_string(core.cartridge().fingerprint())+
            "_slot"+std::to_string(stateSlot)+".cgs");
    }
    void saveState(){
        if(!core.cartridge().loaded())return;
        std::string error;
        if(!pixelgba::StateCodec::save(core,statePath(),error))
            message("Cannot save state: "+error);
    }
    void loadState(){
        if(!core.cartridge().loaded())return;
        std::string error;
        fs::path file=statePath();
        std::error_code ec;
        if(!fs::exists(file,ec)&&!ec && fs::is_regular_file(legacySdlStatePath(),ec))
            file=legacySdlStatePath(); // Read only: retain original file.
        if(!pixelgba::StateCodec::load(core,file,error))
            message("Cannot load state: "+error);
        if(audio)SDL_ClearQueuedAudio(audio);
    }
    void screenshot(){
        if(!core.cartridge().loaded())return;
        std::error_code ec;
        fs::create_directories(data/"screenshots",ec);
        if(ec){message("Cannot create screenshot directory: "+ec.message());return;}
        const auto file=data/"screenshots"/("frame_"+std::to_string(core.frames())+".png");
        if(!savePng(file,core.framebuffer()))message("Cannot save screenshot");
    }
    void toggleFullscreen(){
        fullscreen=!fullscreen;
        if(SDL_SetWindowFullscreen(window,fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0)!=0){
            fullscreen=!fullscreen;message(SDL_GetError());
        }
    }

    fs::path bindingsFile()const{return data/"config"/"input_sdl2.map";}
    bool saveBindings(){
        std::error_code ec;
        fs::create_directories(bindingsFile().parent_path(),ec);
        if(ec){message("Cannot create input configuration directory: "+ec.message());return false;}
        std::ofstream out(bindingsFile(),std::ios::trunc);
        if(!out){message("Cannot open input configuration for writing");return false;}
        out<<"CipherGBA_SDL_INPUT_V1\n";
        for(unsigned i=0;i<10;i++)
            out<<static_cast<int>(keyMap_[i])<<' '<<static_cast<int>(padMap_[i])<<'\n';
        out.flush();out.close();
        if(!out){message("Input configuration write failed");return false;}
        return true;
    }
    void loadBindings(){
        std::ifstream in(bindingsFile());
        if(!in)return;
        std::string magic;
        if(!(in>>magic)||magic!="CipherGBA_SDL_INPUT_V1")return;
        KeyMap newKeys=defaultKeys_;
        PadMap newPad=defaultPad_;
        for(unsigned i=0;i<10;i++){
            int key=0,pad=0;
            if(!(in>>key>>pad) || key<0 || key>=SDL_NUM_SCANCODES ||
               pad<-1 || pad>=SDL_CONTROLLER_BUTTON_MAX)return;
            newKeys[i]=static_cast<SDL_Scancode>(key);
            newPad[i]=static_cast<SDL_GameControllerButton>(pad);
        }
        keyMap_=newKeys;padMap_=newPad;
    }
    void mappingTitle(){
        std::string title="CipherGBA | Remap ";
        title+=buttonNames_[remapIndex_];
        title+=remap_==RemapMode::Keyboard ?
            " keyboard: press key (Delete=unassign, Esc=cancel)":
            " gamepad: press controller button (Esc=cancel)";
        SDL_SetWindowTitle(window,title.c_str());
    }
    void openInputMenu(bool continuing=false){
        if(!continuing){
            pausedBeforeInput_=paused;
            oldKeyMap_=keyMap_;
            oldPadMap_=padMap_;
        }
        paused=true;remap_=RemapMode::None;
        while(running){
            std::ostringstream description;
            description<<"Current SDL2 input bindings:\n";
            for(unsigned i=0;i<10;i++){
                const char* key=SDL_GetScancodeName(keyMap_[i]);
                const char* pad=SDL_GameControllerGetStringForButton(padMap_[i]);
                description<<buttonNames_[i]<<": "
                    <<((key && *key)?key:"Unassigned")<<" / "
                    <<((pad && *pad)?pad:"Unassigned")<<"\n";
            }
            description<<"\nRemap captures all ten actions in order.\n"
                       <<"Delete unassigns a keyboard action, Escape cancels remapping.\n"
                       <<"Changes are not persisted until Save is selected.";
            const std::string copy=description.str();
            SDL_MessageBoxButtonData buttons[]={{
                SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Remap keyboard"},{
                0,2,"Remap controller"},{
                0,3,"Reset defaults"},{
                0,4,"Save"},{
                SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Return"}};
            SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION,window,
                "CipherGBA - Input configuration",copy.c_str(),5,buttons,nullptr};
            int selection=0;
            if(SDL_ShowMessageBox(&box,&selection)!=0)selection=0;
            if(selection==1 || selection==2){
                if(selection==2 && (!controller||!SDL_GameControllerGetAttached(controller))){
                    message("Connect a controller before remapping gamepad buttons");
                    continue;
                }
                remap_=selection==1?RemapMode::Keyboard:RemapMode::Gamepad;
                remapIndex_=0;mappingTitle();return;
            }
            if(selection==3){keyMap_=defaultKeys_;padMap_=defaultPad_;continue;}
            if(selection==4){
                if(saveBindings()){paused=pausedBeforeInput_;return;}
                continue;
            }
            // Return without saving restores the last persisted bindings.
            keyMap_=oldKeyMap_;
            padMap_=oldPadMap_;
            paused=pausedBeforeInput_;
            return;
        }
    }
    void finishMapping(){
        ++remapIndex_;
        if(remapIndex_>=10){
            remap_=RemapMode::None;
            openInputMenu(true);
        }else mappingTitle();
    }

    void chooseFile(bool bios){
        // A native modal picker pauses emulation while it is open.
        // Use UTF-8 path conversion for non-ASCII file names.
        const std::string selected=ciphergba_host::openFileDialog(bios);
        if(selected.empty())return;
        if(!bios){loadRom(userPath(selected));return;}
        // "Import BIOS" persists a validated copy in the user data directory,
        // so it is available again on the next launch. CLI --bios does not copy.
        std::string error;
        if(!core.importBios(userPath(selected),data/"bios"/"gba_bios.bin",error))
            message("Cannot import BIOS: "+error);
        else if(audio)SDL_ClearQueuedAudio(audio);
    }
    void about(){
        const bool oldPaused=paused;
        paused=true;
        SDL_Window* dialog=SDL_CreateWindow("About CipherGBA v0.1 | HodgkinsStudios",
            SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,640,740,
            SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        if(!dialog){
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,"About CipherGBA",
                "CipherGBA v0.1.0 | HodgkinsStudios\nGame Boy Advance Emulator | C++17",
                window);
            paused=oldPaused;return;
        }
        SDL_SetWindowMinimumSize(dialog,420,490);
        SDL_SetWindowModalFor(dialog,window);
        SDL_Renderer* drawing=SDL_CreateRenderer(dialog,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
        if(!drawing)drawing=SDL_CreateRenderer(dialog,-1,SDL_RENDERER_SOFTWARE);
        SDL_Texture* picture=nullptr;
        if(drawing){
            std::vector<unsigned char> pixels;
            const auto& art=pixelgba::artwork::kAboutArtwork;
            if(pixelgba::artwork::decode(art,pixels)){
                picture=SDL_CreateTexture(drawing,SDL_PIXELFORMAT_RGBA32,
                    SDL_TEXTUREACCESS_STATIC,art.width,art.height);
                if(picture && SDL_UpdateTexture(picture,nullptr,pixels.data(),art.width*4)!=0){
                    SDL_DestroyTexture(picture);picture=nullptr;
                }
            }
        }
        const Uint32 id=SDL_GetWindowID(dialog);
        bool showing=true;
        while(showing&&running){
            SDL_Event event{};
            while(SDL_PollEvent(&event)){
                if(event.type==SDL_QUIT){running=false;showing=false;}
                if(event.type==SDL_WINDOWEVENT && event.window.windowID==id &&
                   event.window.event==SDL_WINDOWEVENT_CLOSE)showing=false;
                if(event.type==SDL_KEYDOWN && event.key.windowID==id &&
                   (event.key.keysym.sym==SDLK_ESCAPE||event.key.keysym.sym==SDLK_RETURN||
                    event.key.keysym.sym==SDLK_BACKSPACE))showing=false;
            }
            if(!showing||!drawing)break;
            int w=0,h=0;SDL_GetRendererOutputSize(drawing,&w,&h);
            SDL_SetRenderDrawColor(drawing,9,10,18,255);SDL_RenderClear(drawing);
            const int size=std::max(1,std::min({w-36,h-205,520}));
            SDL_Rect area{(w-size)/2,12,size,size};
            if(picture)SDL_RenderCopy(drawing,picture,nullptr,&area);
            const int y=12+size+18;
            ciphergba_about::text(drawing,"CIPHERGBA V0.1.0",w/2,y,2);
            ciphergba_about::text(drawing,"HODGKINSSTUDIOS",w/2,y+24,2,{200,157,255,255});
            ciphergba_about::text(drawing,"GAME BOY ADVANCE EMULATOR",w/2,y+48,2);
            ciphergba_about::text(drawing,"C++17 | HLE / ORIGINAL BIOS",w/2,y+72,2);
            ciphergba_about::text(drawing,"6994 / 6998 TESTS PASS",w/2,y+96,2,{160,215,248,255});
            ciphergba_about::text(drawing,"ESC OR CLOSE TO RETURN",w/2,y+128,2,{178,192,213,255});
            SDL_RenderPresent(drawing);
            SDL_Delay(16);
        }
        if(picture)SDL_DestroyTexture(picture);
        if(drawing)SDL_DestroyRenderer(drawing);
        SDL_DestroyWindow(dialog);
        paused=oldPaused;
    }
    void audioChannelsMenu(){
        while(running){
            const auto mask=core.audioChannelMask();
            const char* name[6]={"Square 1","Square 2","Wave","Noise",
                                 "Direct Sound A","Direct Sound B"};
            std::ostringstream info;info<<"Active GBA sound channels:\n";
            for(unsigned i=0;i<6;i++)
                info<<((mask&(1u<<i))?"[ON] ":"[OFF] ")<<name[i]<<"\n";
            SDL_MessageBoxButtonData buttons[]={{
                SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Toggle Square 1"},{
                0,2,"Toggle Square 2"},{
                0,3,"Toggle Wave"},{
                0,4,"Toggle Noise"},{
                0,5,"Toggle Direct Sound A"},{
                0,6,"Toggle Direct Sound B"},{
                0,7,"Enable all channels"},{
                SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Back"}};
            const auto text=info.str();
            SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION,window,
                "CipherGBA - Audio channels",text.c_str(),8,buttons,nullptr};
            int selected=0;
            if(SDL_ShowMessageBox(&box,&selected)!=0)selected=0;
            if(selected==0)return;
            if(selected>=1&&selected<=6)
                core.setAudioChannelMask(mask^(1u<<(selected-1)));
            else if(selected==7)core.setAudioChannelMask(0x3fu);
        }
    }

    void systemMenu(){
        while(running){
            std::string prompt="Current display: ";
            prompt+=fullscreen?"Fullscreen":"Windowed";
            prompt+=stretched?" (stretched)":" (aspect preserving)";
            prompt+="\nEmulation: ";
            prompt+=paused?"Paused":"Running";
            prompt+="\nAudio: ";
            prompt+=muted?"Muted":"Enabled";
            prompt+="\nEmulation speed: "+std::to_string(speedPercent_)+"%";
            prompt+="\nAVI recording: ";
            prompt+=avi_.active()?"ON":"OFF";
            SDL_MessageBoxButtonData buttons[]={{
                SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Pause / Resume"},{
                0,2,"Mute / Unmute"},{
                0,3,"Toggle fullscreen"},{
                0,4,"Toggle stretch"},{
                0,5,"Reset ROM"},{
                0,6,"Toggle AVI recording"},{
                0,7,"Cycle speed 100 / 50 / 25%"},{
                0,8,"Audio channels"},{
                SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Back"}};
            SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION,window,
                "CipherGBA - System",prompt.c_str(),9,buttons,nullptr};
            int select=0;
            if(SDL_ShowMessageBox(&box,&select)!=0)select=0;
            if(select==0)return;
            if(select==1)paused=!paused;
            else if(select==2)muted=!muted;
            else if(select==3)toggleFullscreen();
            else if(select==4)stretched=!stretched;
            else if(select==5){core.reset();if(audio)SDL_ClearQueuedAudio(audio);}
            else if(select==6)toggleAvi();
            else if(select==7) speedPercent_=speedPercent_==100?50:speedPercent_==50?25:100;
            else if(select==8)audioChannelsMenu();
        }
    }
    void fileMenu(){
        while(running){
            SDL_MessageBoxButtonData buttons[]={{
                SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Open ROM..."},{
                0,2,"Import BIOS..."},{
                0,3,"Save state"},{
                0,4,"Load state"},{
                0,5,"Screenshot"},{
                0,6,"Next state slot"},{
                0,7,"Quit CipherGBA"},{
                SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Back"}};
            SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION,window,
                "CipherGBA - File", "ROMs: .gba or .zip | Original BIOS: 16KiB .bin",
                8,buttons,nullptr};
            int selected=0;
            if(SDL_ShowMessageBox(&box,&selected)!=0)selected=0;
            if(selected==0)return;
            if(selected==1){chooseFile(false);return;}
            if(selected==2){chooseFile(true);return;}
            if(selected==3)saveState();
            else if(selected==4)loadState();
            else if(selected==5)screenshot();
            else if(selected==6)stateSlot=stateSlot%4+1;
            else if(selected==7){running=false;return;}
        }
    }

    fs::path cheatsPath()const{
        std::ostringstream tag;
        tag<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(8)
           <<core.cartridge().fingerprint();
        return data/"cheats"/(tag.str()+".txt");
    }
    bool saveCheats(){
        if(!core.cartridge().loaded())return false;
        std::error_code ec;
        fs::create_directories(cheatsPath().parent_path(),ec);
        if(ec){message("Cannot create cheat directory: "+ec.message());return false;}
        std::ofstream out(cheatsPath(),std::ios::trunc);
        if(!out){message("Cannot write cheat file");return false;}
        out<<"CGCHEAT2\n";
        for(const auto& cheat:cheats_)
            out<<unsigned(cheat.enabled)<<' '<<std::quoted(cheat.source)<<'\n';
        out.flush();out.close();
        if(!out){message("Cheat file save failed");return false;}
        return true;
    }
    void loadCheats(){
        if(!core.cartridge().loaded())return;
        std::ifstream in(cheatsPath());
        std::string marker;
        if(!in||!std::getline(in,marker)||marker!="CGCHEAT2")return;
        std::string line;
        while(cheats_.size()<64 && std::getline(in,line)){
            std::istringstream row(line);
            int enabled=0;
            std::string source;
            if(!(row>>enabled>>std::quoted(source))||(enabled!=0&&enabled!=1))continue;
            pixelgba::cheats::Program p;
            std::string error;
            if(pixelgba::cheats::compile(source,p,error)){
                p.enabled=enabled==1;
                cheats_.push_back(std::move(p));
            }
        }
    }
    bool pasteCheat(){
        if(!core.cartridge().loaded()){
            message("Load a ROM before adding cheats");return false;
        }
        if(cheats_.size()>=64){message("Cheat limit: 64 programs");return false;}
        if(!SDL_HasClipboardText()){message("Copy a cheat code to the clipboard first");return false;}
        char* pasted=SDL_GetClipboardText();
        if(!pasted){message("Unable to read clipboard");return false;}
        std::string source(pasted);
        SDL_free(pasted);
        if(source.size()>4096){message("Clipboard cheat exceeds the 4096-character limit");return false;}
        for(char& c:source)if(c=='\n'||c=='\r')c=';';
        pixelgba::cheats::Program p;
        std::string error;
        if(!pixelgba::cheats::compile(source,p,error)){
            message("Invalid cheat code: "+error);return false;
        }
        cheats_.push_back(std::move(p));
        cheatsEnabled_=true;
        return saveCheats();
    }
    void applyCheats(bool deviceButtons){
        core.clearRomCheatPatches();
        if(!cheatsEnabled_||cheats_.empty())return;
        const pixelgba::cheats::Bus bus{
            [this](std::uint32_t address,unsigned width)->std::uint32_t{
                return width==1?core.read8(address):
                    width==2?core.read16(address):core.read32(address);
            },
            [this](std::uint32_t address,std::uint32_t value,unsigned width){
                if(width==1)core.write8(address,std::uint8_t(value));
                else if(width==2)core.write16(address,std::uint16_t(value));
                else core.write32(address,value);
            },
            [deviceButtons]{return deviceButtons;},
            [this](std::uint32_t address,std::uint16_t value){
                core.setRomCheatPatch16(address,value);
            }
        };
        for(const auto& p:cheats_)pixelgba::cheats::execute(p,bus);
    }

    fs::path tasPath()const{
        std::ostringstream tag;
        tag<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(8)
           <<core.cartridge().fingerprint();
        return data/"tas"/(tag.str()+".cgtas");
    }

    bool toggleAvi(){
        if(avi_.active()){
            if(!avi_.close())message("Could not finalize AVI recording");
            return true;
        }
        if(!core.cartridge().loaded()){message("Load ROM before recording");return false;}
        const auto folder=data/"recordings";
        const auto file=folder/("CipherGBA_"+std::to_string(core.cartridge().fingerprint())+
            "_frame"+std::to_string(core.frames())+".avi");
        std::string error;
        if(!avi_.open(file,error)){
            message("Could not start AVI recording: "+error);
            return false;
        }
        return true;
    }

    void tasMenu(){
        if(!core.cartridge().loaded()){message("Load a ROM before using TAS");return;}
        const bool wasPaused=paused;paused=true;
        std::ostringstream title;
        title<<"Recording: "<<(tas_.recording()?"ON":"OFF")
             <<"\nPlayback: "<<(tas_.playback()?"ON":"OFF")
             <<"\nFrames: "<<tas_.count()
             <<"\n\nSaved movies use the CGTAS01 format also used by the X11 frontend.";
        const auto description=title.str();
        SDL_MessageBoxButtonData buttons[]={{
            SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Record from reset"},{
            0,2,"Record from current frame"},{
            0,3,"Stop/save recording"},{
            0,4,"Play saved movie"},{
            0,5,"Stop playback"},{
            SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Return"}};
        SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION,window,
            "CipherGBA - TAS recording",description.c_str(),6,buttons,nullptr};
        int choice=0;
        if(SDL_ShowMessageBox(&box,&choice)!=0)choice=0;
        paused=wasPaused;
        std::string error;
        if(choice==1||choice==2){
            if(tas_.recording()&&!tas_.save(tasPath(),core.cartridge().fingerprint(),error))
                {message(error);return;}
            if(choice==1){
                if(!core.flushSave(error)){message(error);return;}
                core.reset();
            }
            if(audio)SDL_ClearQueuedAudio(audio);
            tas_.begin();
        }else if(choice==3){
            if(!tas_.save(tasPath(),core.cartridge().fingerprint(),error))message(error);
        }else if(choice==4){
            if(tas_.recording()&&!tas_.save(tasPath(),core.cartridge().fingerprint(),error))
                {message(error);return;}
            if(!tas_.load(tasPath(),core.cartridge().fingerprint(),error))
                {message(error);return;}
            if(!core.flushSave(error)){message(error);tas_.stopPlayback();return;}
            core.reset();
            if(audio)SDL_ClearQueuedAudio(audio);
        }else if(choice==5)tas_.stopPlayback();
    }

    void cheatMenu(){
        if(!core.cartridge().loaded()){
            message("Load a ROM to configure cheats");return;
        }
        int selected=0;
        while(running){
            std::ostringstream information;
            information<<"Cheats "<<(cheatsEnabled_?"enabled":"disabled")
                       <<" | "<<cheats_.size()<<" / 64 programs\n";
            if(cheats_.empty())information<<"No codes added.\n";
            for(std::size_t i=0;i<cheats_.size()&&i<12;i++){
                information<<(int(i)==selected?"> ":"  ")<<(i+1)<<". "
                           <<(cheats_[i].enabled?"[ON] ":"[OFF] ")
                           <<pixelgba::cheats::formatName(cheats_[i].format)<<" ";
                information<<cheats_[i].source.substr(0,36)<<"\n";
            }
            if(cheats_.size()>12)information<<"More codes stored (first twelve shown).\n";
            information<<"\nPaste supports RAW:, CB:, GS:, AR: and multi-line code groups.\n"
                       <<"Copy a code, then select Paste. Saved files are CGCHEAT2,\n"
                       <<"compatible with CipherGBA's existing Linux X11 interface.";
            SDL_MessageBoxButtonData buttons[]={{
                SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Paste cheat"},{
                0,2,"Enable / Disable all"},{
                0,3,"Next code"},{
                0,4,"Toggle selected"},{
                0,5,"Delete selected"},{
                0,6,"Clear all"},{
                SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Return"}};
            const std::string prompt=information.str();
            SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION,window,
                "CipherGBA - Cheat codes",prompt.c_str(),7,buttons,nullptr};
            int action=0;
            if(SDL_ShowMessageBox(&box,&action)!=0)action=0;
            if(action==0)return;
            if(action==1)pasteCheat();
            else if(action==2){
                cheatsEnabled_=!cheatsEnabled_;
                if(!cheatsEnabled_)core.clearRomCheatPatches();
            }
            else if(action==3 && !cheats_.empty())selected=(selected+1)%int(cheats_.size());
            else if(action==4 && !cheats_.empty()){
                cheats_[selected].enabled=!cheats_[selected].enabled;
                saveCheats();
            }
            else if(action==5 && !cheats_.empty()){
                cheats_.erase(cheats_.begin()+selected);
                selected=0;saveCheats();
                core.clearRomCheatPatches();
            }
            else if(action==6){
                cheats_.clear();selected=0;saveCheats();
                core.clearRomCheatPatches();
            }
        }
    }


    // Persistent clickable menus for Windows, macOS, AppImage and Flatpak.
    // Use the dependency-free bitmap font; the logo is reserved for About.
    struct UiEntry {const char* label; int action;};
    std::vector<UiEntry> uiEntries(int tab) const {
        switch(tab){
        case 0: return {{"OPEN ROM",1},{"IMPORT BIOS",2},{"SAVE STATE",3},
                        {"LOAD STATE",4},{"SCREENSHOT",5},{"NEXT STATE SLOT",6},
                        {"RESET ROM",7},{"QUIT",8}};
        case 1: return {{"TOGGLE FULLSCREEN",9},{"WINDOWED",10},
                        {"TOGGLE STRETCH",11},{"PAUSE OR RESUME",12},
                        {"MUTE OR UNMUTE",13},{"CYCLE EMULATION SPEED",14},
                        {"AUDIO CHANNELS",15}};
        case 2: return {{"INPUT MAPPING",16},{"CHEAT CODES",17},
                        {"TAS RECORDING",18}};
        case 3: return {{"ABOUT CIPHERGBA",19}};
        default: return {};
        }
    }
    int uiScale() const {
        int ww=0,wh=0,rw=0,rh=0;
        SDL_GetWindowSize(window,&ww,&wh);
        SDL_GetRendererOutputSize(renderer,&rw,&rh);
        return std::clamp((rw+std::max(1,ww)/2)/std::max(1,ww),1,3);
    }
    int uiBarHeight() const{return 38*uiScale();}
    int uiStatusHeight() const{return 26*uiScale();}
    bool uiChromeVisible() const {
        return !fullscreen || uiMenu_>=0 || (uiMouseY_>=0 && uiMouseY_<uiBarHeight());
    }
    int uiTabAt(int x,int y) const {
        const int scale=uiScale();
        if(y<0 || y>=uiBarHeight())return -1;
        const int left=12*scale, tabWidth=116*scale;
        return x>=left && x<left+4*tabWidth ? (x-left)/tabWidth : -1;
    }
    void uiMapMouse(int& x,int& y) const {
        int ww=0,wh=0,rw=0,rh=0;
        SDL_GetWindowSize(window,&ww,&wh);
        SDL_GetRendererOutputSize(renderer,&rw,&rh);
        x=x*rw/std::max(1,ww);
        y=y*rh/std::max(1,wh);
    }
    int uiPopupX(int tab,int screenWidth) const {
        const int scale=uiScale();
        return std::max(0,std::min((12+116*tab)*scale,screenWidth-300*scale));
    }
    int uiActionAt(int x,int y) const {
        if(uiMenu_<0)return 0;
        int w=0,h=0;SDL_GetRendererOutputSize(renderer,&w,&h);
        const int scale=uiScale(), left=uiPopupX(uiMenu_,w);
        const int rowHeight=30*scale;
        const auto entries=uiEntries(uiMenu_);
        if(x<left || x>=left+300*scale || y<uiBarHeight() ||
           y>=uiBarHeight()+static_cast<int>(entries.size())*rowHeight)return 0;
        return entries[static_cast<std::size_t>((y-uiBarHeight())/rowHeight)].action;
    }
    void uiAction(int command){
        switch(command){
        case 1: chooseFile(false);break;
        case 2: chooseFile(true);break;
        case 3: saveState();break;
        case 4: loadState();break;
        case 5: screenshot();break;
        case 6: stateSlot=stateSlot%4+1;break;
        case 7: if(core.cartridge().loaded()){core.reset();if(audio)SDL_ClearQueuedAudio(audio);}break;
        case 8: running=false;break;
        case 9: toggleFullscreen();break;
        case 10: if(fullscreen)toggleFullscreen();break;
        case 11: stretched=!stretched;break;
        case 12: paused=!paused;break;
        case 13: muted=!muted;break;
        case 14: speedPercent_=speedPercent_==100?50:speedPercent_==50?25:100;break;
        case 15: audioChannelsMenu();break;
        case 16: openInputMenu();break;
        case 17: cheatMenu();break;
        case 18: tasMenu();break;
        case 19: about();break;
        default: break;
        }
    }
    void uiFill(int x,int y,int w,int h,SDL_Color c) {
        SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);
        const SDL_Rect rectangle{x,y,w,h};
        SDL_RenderFillRect(renderer,&rectangle);
    }
    void uiDrawChrome(int w,int h) {
        if(!uiChromeVisible())return;
        const int scale=uiScale(), barHeight=uiBarHeight();
        const int statusHeight=uiStatusHeight();
        uiFill(0,0,w,barHeight,{31,39,54,255});
        uiFill(0,barHeight-2*scale,w,2*scale,{67,91,122,255});
        const char* tabs[]={"FILE","SYSTEM","CONFIG","HELP"};
        for(int i=0;i<4;++i){
            const int x=(12+116*i)*scale;
            if(i==uiMenu_ || uiTabAt(uiMouseX_,uiMouseY_)==i)
                uiFill(x,3*scale,116*scale,31*scale,
                       i==uiMenu_?SDL_Color{68,102,145,255}:SDL_Color{45,61,82,255});
            ciphergba_about::text(renderer,tabs[i],x+58*scale,11*scale,2*scale);
        }
        uiFill(0,h-statusHeight,w,statusHeight,{25,31,43,255});
        uiFill(0,h-statusHeight,w,scale,{67,91,122,255});
        const char* state=core.cartridge().loaded()?(paused?"PAUSED":"RUNNING"):"NO ROM LOADED";
        ciphergba_about::text(renderer,
            std::string("CIPHERGBA | ")+state,
            std::min(w/2,160*scale),h-statusHeight+8*scale,scale,
            {191,205,226,255});
        if(w>=670*scale)
            ciphergba_about::text(renderer,
                "F1 MENU | ESC FULLSCREEN",w-150*scale,
                h-statusHeight+8*scale,scale,{145,171,203,255});
        if(uiMenu_<0)return;
        const auto entries=uiEntries(uiMenu_);
        const int left=uiPopupX(uiMenu_,w), rowHeight=30*scale;
        const int popupHeight=static_cast<int>(entries.size())*rowHeight;
        uiFill(left,barHeight,300*scale,popupHeight+2*scale,{43,54,73,255});
        uiFill(left,barHeight,scale,popupHeight,{91,117,152,255});
        for(std::size_t i=0;i<entries.size();++i){
            const int y=barHeight+static_cast<int>(i)*rowHeight;
            if(uiMouseX_>=left && uiMouseX_<left+300*scale &&
               uiMouseY_>=y && uiMouseY_<y+rowHeight)
                uiFill(left+2*scale,y,296*scale,rowHeight,{75,111,153,255});
            ciphergba_about::text(renderer,entries[i].label,
                left+150*scale,y+9*scale,2*scale,{239,245,255,255});
        }
    }
    void uiDrawNoRom(int w,int top,int bottom) {
        const int scale=uiScale();
        const int middle=top+(bottom-top)/2;
        ciphergba_about::text(renderer,"CIPHERGBA",w/2,
                              middle-65*scale,4*scale,{224,235,255,255});
        ciphergba_about::text(renderer,"GAME BOY ADVANCE EMULATOR",w/2,
                              middle-17*scale,2*scale,{166,185,212,255});
        ciphergba_about::text(renderer,"NO GAME LOADED",w/2,
                              middle+22*scale,2*scale,{228,237,250,255});
        ciphergba_about::text(renderer,"FILE - OPEN ROM  OR  CTRL+O",w/2,
                              middle+55*scale,2*scale,{155,185,227,255});
    }
    void mainMenu(){
        const bool originalPause=paused;
        paused=true;
        SDL_MessageBoxButtonData buttons[]={{
            SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"File / ROM / Saves"},{
            0,2,"System / Display / Audio"},{
            0,3,"Configure input"},{
            0,4,"Cheat codes"},{
            0,5,"TAS input recording"},{
            0,6,"About CipherGBA"},{
            SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Return to game"}};
        SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION,window,
            "CipherGBA - Menu",
            "Portable SDL2 interface\n"
            "F1: Menu | Ctrl+O: ROM | Ctrl+B: BIOS | Ctrl+I: Input\n"
            "Some X11-specific features are not available in SDL2.",
            7,buttons,nullptr};
        int selected=0;
        if(SDL_ShowMessageBox(&box,&selected)!=0)selected=0;
        paused=originalPause;
        if(selected==1)fileMenu();
        else if(selected==2)systemMenu();
        else if(selected==3)openInputMenu();
        else if(selected==4)cheatMenu();
        else if(selected==5)tasMenu();
        else if(selected==6)about();
    }
    void handleEvent(const SDL_Event& event){
        if(event.type==SDL_MOUSEMOTION){
            int x=event.motion.x,y=event.motion.y;uiMapMouse(x,y);
            uiMouseX_=x;uiMouseY_=y;
            if(uiMenu_>=0){
                const int tab=uiTabAt(x,y);
                if(tab>=0)uiMenu_=tab;
            }
            return;
        }
        if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT){
            int x=event.button.x,y=event.button.y;uiMapMouse(x,y);
            uiMouseX_=x;uiMouseY_=y;
            if(uiChromeVisible()){
                const int tab=uiTabAt(x,y);
                if(tab>=0){uiMenu_=uiMenu_==tab?-1:tab;return;}
            }
            if(uiMenu_>=0){
                const int command=uiActionAt(x,y);
                uiMenu_=-1;
                if(command)uiAction(command);
                return;
            }
        }
        if(event.type==SDL_QUIT){running=false;return;}
        if(event.type==SDL_DROPFILE){
            if(event.drop.file){
                const fs::path dropped=userPath(event.drop.file);
                std::error_code ec;
                if(fs::is_directory(dropped,ec)){
                    SDL_MessageBoxButtonData buttons[]={{
                        SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Import existing data"},{
                        SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Cancel"}};
                    SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION,window,
                        "CipherGBA - Import legacy data",
                        "Import compatible saves, BIOS, states, TAS and cheats from this folder?\n"
                        "Original files remain untouched; existing new files are not overwritten.\n"
                        "X11 input mappings are not portable and will not be copied.",
                        2,buttons,nullptr};
                    int choice=0;
                    if(SDL_ShowMessageBox(&box,&choice)==0 && choice==1)
                        importLegacyData(dropped);
                }else loadRom(dropped);
                SDL_free(event.drop.file);
            }
            return;
        }
        if(remap_==RemapMode::Gamepad && event.type==SDL_CONTROLLERBUTTONDOWN){
            if(controller && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))
                  ==event.cbutton.which){
                padMap_[remapIndex_]=static_cast<SDL_GameControllerButton>(event.cbutton.button);
                finishMapping();
            }
            return;
        }
        if(event.type==SDL_CONTROLLERDEVICEADDED && !controller){
            if(SDL_IsGameController(event.cdevice.which))
                controller=SDL_GameControllerOpen(event.cdevice.which);
            return;
        }
        if(event.type==SDL_CONTROLLERDEVICEREMOVED && controller &&
           SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))==event.cdevice.which){
            SDL_GameControllerClose(controller);controller=nullptr;
            for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_IsGameController(i)){
                controller=SDL_GameControllerOpen(i);
                if(controller)break;
            }
            return;
        }
        if(event.type!=SDL_KEYDOWN||event.key.repeat)return;
        const auto key=event.key.keysym.sym;
        if(uiMenu_>=0 && key==SDLK_ESCAPE){uiMenu_=-1;return;}
        if(remap_!=RemapMode::None){
            if(key==SDLK_ESCAPE){remap_=RemapMode::None;openInputMenu(true);return;}
            if(remap_==RemapMode::Keyboard){
                keyMap_[remapIndex_]=key==SDLK_DELETE?SDL_SCANCODE_UNKNOWN:
                    event.key.keysym.scancode;
                finishMapping();
            }
            return;
        }
        if((event.key.keysym.mod & KMOD_CTRL) && key==SDLK_o)chooseFile(false);
        else if((event.key.keysym.mod & KMOD_CTRL) && key==SDLK_b)chooseFile(true);
        else if((event.key.keysym.mod & KMOD_CTRL) && key==SDLK_i)openInputMenu();
        else if(key==SDLK_F1)mainMenu();
        else if(key==SDLK_F3)tasMenu();
        else if(key==SDLK_F12)toggleAvi();
        else if(key==SDLK_ESCAPE||key==SDLK_F11)toggleFullscreen();
        else if(key==SDLK_F7)paused=!paused;
        else if(key==SDLK_F5)saveState();
        else if(key==SDLK_F8)loadState();
        else if(key==SDLK_F9)screenshot();
        else if(key==SDLK_F6)stateSlot=stateSlot%4+1;
        else if(key==SDLK_F4)muted=!muted;
        else if(key==SDLK_F2){core.reset();if(audio)SDL_ClearQueuedAudio(audio);}
        else if(key==SDLK_F10)stretched=!stretched;
    }
    std::array<bool,10> input() const {
        std::array<bool,10> buttons{};
        const auto* keys=SDL_GetKeyboardState(nullptr);
        for(unsigned i=0;i<10;i++){
            if(keyMap_[i]!=SDL_SCANCODE_UNKNOWN)
                buttons[i]=keys[keyMap_[i]]!=0;
        }
        if(controller&&SDL_GameControllerGetAttached(controller)){
            auto btn=[this](SDL_GameControllerButton b){
                return b!=SDL_CONTROLLER_BUTTON_INVALID &&
                       SDL_GameControllerGetButton(controller,b)!=0;
            };
            const auto axis=[this](SDL_GameControllerAxis a){
                return SDL_GameControllerGetAxis(controller,a);
            };
            for(unsigned i=0;i<10;i++)buttons[i]=buttons[i]||btn(padMap_[i]);
            // Left-stick navigation remains available independently of remaps.
            buttons[4]=buttons[4]||axis(SDL_CONTROLLER_AXIS_LEFTX)>11000;
            buttons[5]=buttons[5]||axis(SDL_CONTROLLER_AXIS_LEFTX)<-11000;
            buttons[6]=buttons[6]||axis(SDL_CONTROLLER_AXIS_LEFTY)<-11000;
            buttons[7]=buttons[7]||axis(SDL_CONTROLLER_AXIS_LEFTY)>11000;
        }
        return buttons;
    }
    void runFrame(bool fastForward){
        auto buttons=input();
        std::uint16_t mask=0;
        for(unsigned i=0;i<10;i++)if(buttons[i])mask|=std::uint16_t(1u<<i);
        std::uint16_t finalMask=mask;
        if(!tas_.consume(mask,finalMask)){
            std::string error;
            if(!tas_.save(tasPath(),core.cartridge().fingerprint(),error))
                std::fprintf(stderr,"TAS save: %s\n",error.c_str());
        }
        if(finalMask!=mask)
            for(unsigned i=0;i<10;i++)buttons[i]=(finalMask&(1u<<i))!=0;
        core.setButtons(buttons);
        core.runFrame();
        applyCheats(buttons[8]&&buttons[9]);
        const auto samples=core.takeAudioSamples();
        if(avi_.active() && (!avi_.appendFrame(core.framebuffer()) ||
                             !avi_.appendAudio(samples))){
            avi_.close();
            message("AVI recording stopped: file limit or I/O error");
        }
        if(audio && !muted && !fastForward){
            if(SDL_GetQueuedAudioSize(audio)>32768)SDL_ClearQueuedAudio(audio);
            if(!samples.empty())SDL_QueueAudio(audio,samples.data(),
                static_cast<std::uint32_t>(samples.size()*sizeof(std::int16_t)));
        }else if(audio && fastForward)SDL_ClearQueuedAudio(audio);
    }
    void render(){
        int w=0,h=0;SDL_GetRendererOutputSize(renderer,&w,&h);
        const bool chrome=uiChromeVisible();
        const int top=chrome?uiBarHeight():0;
        const int bottom=h-(chrome?uiStatusHeight():0);
        SDL_SetRenderDrawColor(renderer,10,14,19,255);
        SDL_RenderClear(renderer);
        if(core.cartridge().loaded()){
            const auto& frame=core.framebuffer();
            for(std::size_t i=0;i<rgba.size();i++)rgba[i]=0xff000000u|frame[i];
            SDL_UpdateTexture(texture,nullptr,rgba.data(),kWidth*int(sizeof(std::uint32_t)));
            SDL_Rect rect{0,top,w,std::max(1,bottom-top)};
            if(!stretched){
                const float factor=std::min(float(w)/kWidth,float(bottom-top)/kHeight);
                const float scale=factor>=1.0f?std::floor(factor):factor;
                rect.w=std::max(1,int(kWidth*scale));
                rect.h=std::max(1,int(kHeight*scale));
                rect.x=(w-rect.w)/2;
                rect.y=top+(bottom-top-rect.h)/2;
            }
            SDL_RenderCopy(renderer,texture,nullptr,&rect);
        }else{
            uiDrawNoRom(w,top,bottom);
        }
        if(chrome)uiDrawChrome(w,h);
        SDL_RenderPresent(renderer);
    }
    int loop(unsigned smokeFrameLimit=0){
        using Clock=std::chrono::steady_clock;
        auto next=Clock::now();
        auto statusAt=next;
        unsigned count=0;
        unsigned totalFrames=0;
        while(running){
            SDL_Event e{};
            while(SDL_PollEvent(&e))handleEvent(e);
            if(!running)break;
            const bool fast=SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB]!=0;
            const auto now=Clock::now();
            if(!paused && core.cartridge().loaded() && (fast || now>=next)){
                const int runs=fast?4:1;
                for(int i=0;i<runs;i++){
                    runFrame(fast);
                    ++totalFrames;
                    if(smokeFrameLimit && totalFrames>=smokeFrameLimit)return 0;
                }
                count+=static_cast<unsigned>(runs);
                if(fast){next=Clock::now();}else{
                    const auto framePeriod=std::chrono::duration<double>(
                        100.0/(kFramesPerSecond*speedPercent_));
                    next+=std::chrono::duration_cast<Clock::duration>(framePeriod);
                    if(next+std::chrono::milliseconds(100)<Clock::now())next=Clock::now();
                }
            }
            render();
            const auto update=Clock::now();
            if(update-statusAt>=std::chrono::seconds(1)){
                const double interval=std::chrono::duration<double>(update-statusAt).count();
                std::ostringstream title;title<<"CipherGBA v0.1";
                if(!romName.empty())title<<" - "<<romName;
                else title<<" | F1 Menu / Ctrl+O Open ROM";
                title<<" | "<<std::fixed<<std::setprecision(1)<<count/interval<<" FPS";
                if(speedPercent_!=100)title<<" | "<<speedPercent_<<"% speed";
                if(paused)title<<" | PAUSED";
                if(avi_.active())title<<" | AVI REC "<<avi_.frames();
                if(tas_.recording())title<<" | TAS REC "<<tas_.count();
                else if(tas_.playback())title<<" | TAS PLAY "<<tas_.cursor();
                title<<" | State "<<stateSlot;
                if(remap_!=RemapMode::None) {
                    title<<" | REMAP "<<buttonNames_[remapIndex_]<<" (Esc cancels)";
                }
                SDL_SetWindowTitle(window,title.str().c_str());
                statusAt=update;count=0;
            }
            if(!fast && next>update){
                const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(next-update);
                if(left.count()>1)SDL_Delay(static_cast<Uint32>(std::min<long long>(left.count()-1,4)));
                else SDL_Delay(1);
            }else if(paused || !core.cartridge().loaded())SDL_Delay(12);
        }
        return 0;
    }
};
void help(){
    std::puts("CipherGBA v0.1 (SDL2 frontend)");
    std::puts("Usage: CipherGBA [--rom PATH] [--bios PATH] [--import-data DIR] [--verify-rom PATH] [--smoke-frames N] [--help] [--version]");
    std::puts("F1: Main menu. Ctrl+O: Open ROM; Ctrl+B: Import BIOS; Ctrl+I: Input remap.");
    std::puts("Controls: Z=A X=B Backspace=Select Enter=Start Arrows=D-pad A=L S=R");
    std::puts("F2 Reset, F3 TAS, F4 Mute, F5 Save, F6 Slot, F7 Pause, F8 Load, F9 PNG");
    std::puts("F10 Stretch, F11/Escape Fullscreen, F12 AVI record, Tab Fast-forward.");
    std::puts("User save data uses the OS-specific SDL preferences directory.");
    std::puts("--import-data DIR copies existing saves/states/TAS/cheats/BIOS without overwriting.");
    std::puts("You may also drag the old CipherGBA_Data folder onto the SDL2 window.");
}
}
int main(int argc,char** argv){
    std::string rom,bios,verify,oldData;
    unsigned smokeFrames=0;
    for(int i=1;i<argc;i++){
        const std::string arg=argv[i];
        if(arg=="--version"){std::puts("CipherGBA v0.1.0");return 0;}
        if(arg=="--help"){help();return 0;}
        if(arg=="--smoke-frames" && i+1<argc){
            const std::string value=argv[++i];
            char* end=nullptr;
            const auto parsed=std::strtol(value.c_str(),&end,10);
            if(!end||*end!='\0'||parsed<1||parsed>600){
                std::fprintf(stderr,"--smoke-frames must be 1..600\n");return 2;
            }
            smokeFrames=static_cast<unsigned>(parsed);
        }else if((arg=="--rom"||arg=="--bios"||arg=="--verify-rom"||arg=="--import-data")&&i+1<argc){
            const std::string value=argv[++i];
            if(arg=="--rom")rom=value;
            else if(arg=="--bios")bios=value;
            else if(arg=="--verify-rom")verify=value;
            else oldData=value;
        }else{
            std::fprintf(stderr,"Unknown or incomplete argument: %s\n",arg.c_str());
            return 2;
        }
    }
    if(!verify.empty()){
        pixelgba::Cartridge cartridge;
        std::string error;
        if(!cartridge.load(userPath(verify),error)){
            std::fprintf(stderr,"CipherGBA ROM check failed: %s\n",error.c_str());
            return 1;
        }
        std::printf("ROM OK: %s CRC32=%08X bytes=%zu\n",
                    cartridge.title().c_str(),cartridge.fingerprint(),cartridge.size());
        return 0;
    }
    if(smokeFrames && rom.empty()){
        std::fprintf(stderr,"--smoke-frames requires --rom PATH\n");return 2;
    }
    SDL_SetMainReady();
    Application app;
    if(!app.init())return 1;
    if(!oldData.empty()&&!app.importLegacyData(userPath(oldData)))return 1;
    if(!bios.empty()&&!app.loadBios(userPath(bios)))return 1;
    if(!rom.empty()&&!app.loadRom(userPath(rom)))return 1;
    // Startup draws the visible File/System/Config/Help toolbar itself.
    // About artwork never replaces the emulator frontend.
    return app.loop(smokeFrames);
}
