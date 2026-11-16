// HodgkinsStudios CipherGBA: minimal X11 frontend.
// File: Load ROM, Load BIOS, Reset ROM, Exit.
// Config: Fullscreen, Windowed, Input Config.
#include "pixelgba/Core.hpp"
#include "pixelgba/StateFiles.hpp"
#include "pixelgba/Bindings.hpp"
#include "pixelgba/Paths.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <optional>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <zlib.h>
#include "FrontendExtras.hpp"
#include "HostPacing.hpp"
#include "CheatCodes.hpp"
#include "BrandArtwork.hpp"

namespace fs=std::filesystem;
namespace pixelgba {
namespace {
using Clock=std::chrono::steady_clock;
// Xlib error callbacks are process-global. The frontend initializes MIT-SHM
// synchronously on its only UI thread and restores the prior handler at once.
bool shmAttachError=false;
int shmAttachErrorHandler(Display*,XErrorEvent*){shmAttachError=true;return 0;}
// This is the documented XShmCompletionEvent wire layout.  Keep Xext
// dynamically loaded: systems with Xlib but no Xext development headers
// can still build CipherGBA.
struct ShmCompletionEvent {
    int type;unsigned long serial;Bool send_event;Display* display;
    Drawable drawable;int major_code,minor_code;unsigned long shmseg,offset;
};
Bool isShmCompletion(Display*,XEvent* event,XPointer arg){
    return event->type==*reinterpret_cast<const int*>(arg);
}
constexpr int Bar=38, InitialW=1050, InitialH=720, Controls=10;
constexpr int MenuRowH=32, StatusH=30;
constexpr double FrameDuration=1232.0*228.0/16777216.0;
const char* const inputNames[Controls]={"A","B","Select","Start","Right","Left","Up","Down","R","L"};
std::string keyName(KeySym k){ const char* s=XKeysymToString(k);return s?s:"Unassigned"; }
std::string padName(const PadMapping& p){return (p.type==PadMapping::Type::Button?"Button ":"Axis ")+std::to_string(p.index)+(p.type==PadMapping::Type::Axis?(p.direction>0?" +":" -"):"");}
// GTK chooser runs in a child process so the emulator owns only its X11 event loop.
int gtkDialog(int kind,const fs::path& current){
    void* gtk=dlopen("libgtk-3.so.0",RTLD_NOW|RTLD_LOCAL);
    if(!gtk)return 1;
    using Init=int(*)(int*,char***);
    using New=void*(*)(const char*,void*,int,const char*,...);
    using Run=int(*)(void*);
    using Destroy=void(*)(void*);
    using GetFilename=char*(*)(void*);
    using SetFolder=int(*)(void*,const char*);
    using FilterNew=void*(*)();
    using FilterName=void(*)(void*,const char*);
    using FilterPattern=void(*)(void*,const char*);
    using AddFilter=void(*)(void*,void*);
    auto init=(Init)dlsym(gtk,"gtk_init_check");
    auto create=(New)dlsym(gtk,"gtk_file_chooser_dialog_new");
    auto run=(Run)dlsym(gtk,"gtk_dialog_run");
    auto destroy=(Destroy)dlsym(gtk,"gtk_widget_destroy");
    auto getFilename=(GetFilename)dlsym(gtk,"gtk_file_chooser_get_filename");
    auto setFolder=(SetFolder)dlsym(gtk,"gtk_file_chooser_set_current_folder");
    auto filterNew=(FilterNew)dlsym(gtk,"gtk_file_filter_new");
    auto filterName=(FilterName)dlsym(gtk,"gtk_file_filter_set_name");
    auto filterPattern=(FilterPattern)dlsym(gtk,"gtk_file_filter_add_pattern");
    auto addFilter=(AddFilter)dlsym(gtk,"gtk_file_chooser_add_filter");
    void* glib=dlopen("libglib-2.0.so.0",RTLD_NOW|RTLD_LOCAL);
    using Free=void(*)(void*);
    auto gfree=glib?(Free)dlsym(glib,"g_free"):nullptr;
    if(!init||!create||!run||!destroy||!getFilename||!setFolder||!filterNew||!filterName||!filterPattern||!addFilter||!gfree||!init(nullptr,nullptr))return 2;
    void* dlg=create(kind==1?"CipherGBA - Load BIOS":kind==2?"CipherGBA - Select Recording Directory":"CipherGBA - Load ROM",nullptr,kind==2?2:0,"_Cancel",-6,"_Open",-3,(const char*)nullptr);
    if(!dlg)return 3;
    if(!current.empty()){const std::string folder=current.string();setFolder(dlg,folder.c_str());}
    void* filter=filterNew();filterName(filter,kind==1?"GBA BIOS (.bin)":"GBA ROM (.gba, .zip)");filterPattern(filter,kind==1?"*.bin":"*.gba");
    if(kind==0)filterPattern(filter,"*.zip");
    if(kind!=2)addFilter(dlg,filter);
    if(kind!=2){void* all=filterNew();filterName(all,"All files");filterPattern(all,"*");addFilter(dlg,all);}
    const int result=run(dlg);
    if(result==-3){char* name=getFilename(dlg);if(name){std::puts(name);gfree(name);destroy(dlg);return 0;}}
    destroy(dlg);return 4;
}
std::string selectFile(int kind,const fs::path& cwd){
    int fd[2];if(pipe(fd)!=0)return {};
    const pid_t pid=fork();
    if(pid==0){close(fd[0]);dup2(fd[1],STDOUT_FILENO);close(fd[1]);const int result=gtkDialog(kind,cwd);std::fflush(stdout);_exit(result);}
    close(fd[1]);if(pid<0){close(fd[0]);return {};}
    std::string output;char buf[1024];ssize_t n;
    while((n=read(fd[0],buf,sizeof(buf)))>0){if(output.size()>8192)break;output.append(buf,std::size_t(n));}
    close(fd[0]);int status=0;waitpid(pid,&status,0);
    if(!WIFEXITED(status)||WEXITSTATUS(status)!=0)return {};
    const auto nl=output.find_first_of("\r\n");if(nl!=std::string::npos)output.resize(nl);
    return output;
}
} // namespace

class Joystick {
public:
    ~Joystick(){if(fd_>=0)close(fd_);}
    void poll(const std::function<void(PadMapping)>& onPress){
        if(fd_<0){if(Clock::now()-probe_<std::chrono::seconds(2))return;
            probe_=Clock::now();
            for(int i=0;i<8;++i){std::string p="/dev/input/js"+std::to_string(i);fd_=open(p.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(fd_>=0){name_=p;break;}}
        }
        if(fd_<0)return;
        js_event ev{};
        while(true){const auto n=read(fd_,&ev,sizeof(ev));if(n!=sizeof(ev)){
                if(n==0||(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)){close(fd_);fd_=-1;name_="Not connected";}
                break;
            }
            const bool initial=(ev.type&JS_EVENT_INIT)!=0;
            switch(ev.type&~JS_EVENT_INIT){
                case JS_EVENT_BUTTON:
                    if(ev.number<buttons_.size())buttons_[ev.number]=ev.value!=0;
                    if(!initial&&ev.value)onPress({PadMapping::Type::Button,ev.number,1});
                    break;
                case JS_EVENT_AXIS:
                    if(ev.number<axes_.size())axes_[ev.number]=ev.value;
                    if(!initial&&std::abs(int(ev.value))>23000)onPress({PadMapping::Type::Axis,ev.number,ev.value>0?1:-1});
                    break;
                default: break;
            }
        }
    }
    bool down(const PadMapping& p)const{
        if(p.index<0||fd_<0)return false;
        if(p.type==PadMapping::Type::Button)return p.index<int(buttons_.size())&&buttons_[p.index];
        return p.index<int(axes_.size())&&(p.direction>0?axes_[p.index]>16000:axes_[p.index]<-16000);
    }
    const std::string& name()const{return name_;}
private:
    int fd_=-1;Clock::time_point probe_{};
    std::array<bool,32> buttons_{};std::array<int,16> axes_{};
    std::string name_="Not connected";
};

// CipherNES-inspired frontend: appearance and menu organization adapted for GBA.
// All optional features below are host-side unless explicitly marked hardware-facing.
class App {
    enum Cmd {
        LoadRom=1,LoadBios,ResetRom,Quit,FullScreen,Windowed,InputConfig,
        FullScreenStretch,Mute,Speed100,Speed50,Speed25,Screenshot,
        SaveState,LoadState,Slot1,Slot2,Slot3,Slot4,RecordVideo,StopVideo,Pause,
        TasRecord,TasStop,TasPlayback,TasPlaybackStop,TasReset,
        CheatsConfig,CheatsEnabled,Info,RecordToast,RecordTimer,CustomVideoDir,
        Audio1,Audio2,Audio3,Audio4,Audio5,Audio6,ResetToHle
    };
    enum Panel { Game, Controller, Cheats, About };
    struct Item {std::string text;int cmd=0,submenu=0;bool separator=false;};
    struct Menu {std::string title;std::vector<Item> items;};
    struct TasFrame {std::uint16_t buttons=0;};
    Paths paths_;GbaCore core_;Bindings bindings_{},editing_{};Joystick joystick_;
    host::SdlAudio audio_;host::AviRecorder avi_;
    Display* dpy_=nullptr;Window win_=0;GC gc_=0;Pixmap back_=0;XFontStruct* font_=nullptr;
    // Reusable XImage avoids allocating/destroying a ~2.5MB image on every frame.
    XImage* scaledImage_=nullptr;
    // X11 MIT-SHM is optional; dynamic libXext lookup keeps public builds
    // compatible with machines that lack the extension/development headers.
    // The structure matches XShmSegmentInfo on X11 LP64 and ILP32 hosts.
    struct ShmInfo {unsigned long shmseg=0;int shmid=-1;char* shmaddr=nullptr;Bool readOnly=False;};
    void* xext_=nullptr;
    struct ShmBuffer {XImage* image=nullptr;ShmInfo info{};bool attached=false,busy=false;};
    std::array<ShmBuffer,2> shmBuffers_{};
    bool shmAttached_=false,shmChecked_=false,shmAsync_=false;
    int shmCompletionType_=-1,shmNext_=0;
    std::uint64_t shmAsyncPuts_=0,shmSyncPuts_=0,shmWaits_=0;
    using ShmQuery=Bool(*)(Display*);
    using ShmEventBase=int(*)(Display*);
    using ShmCreate=XImage*(*)(Display*,Visual*,unsigned,int,char*,ShmInfo*,unsigned,unsigned);
    using ShmAttach=Status(*)(Display*,ShmInfo*);
    using ShmDetach=Status(*)(Display*,ShmInfo*);
    using ShmPut=Status(*)(Display*,Drawable,GC,XImage*,int,int,int,int,unsigned,unsigned,Bool);
    ShmCreate shmCreate_=nullptr;ShmAttach shmAttach_=nullptr;
    ShmDetach shmDetach_=nullptr;ShmPut shmPut_=nullptr;
    ShmEventBase shmEventBase_=nullptr;
    std::vector<unsigned> stretchXMap_;
    // The compositor need not rescale/re-upload identical finished frames.
    // Every GBA frame is still emulated, mixed and recorded normally.
    std::array<std::uint32_t,GbaCore::Width*GbaCore::Height> previousPixels_{};
    bool havePreviousPixels_=false;
    // Separate content-only frame updates from menu/status invalidations.
    // GBA pixels change ~60 times/s, but the menu/status normally do not.
    bool frameUpdated_=false;
    int gameX_=0,gameY_=0,gameW_=0,gameH_=0;
    unsigned fpsFrames_=0;
    double actualFps_=0.0;
    double avgCoreMs_=0.0,avgPresentMs_=0.0;
    double coreMsSum_=0.0,presentMsSum_=0.0;
    unsigned presentCount_=0;
    // All diagnostics refer to host-side scheduling, not emulated GBA time.
    std::uint64_t lateFrameCount_=0, audioQueueSamples_=0;
    double lateMsMax_=0.0, audioQueueMsSum_=0.0, audioQueueMsMax_=0.0;
    const bool legacyHostPacing_=std::getenv("CIPHERGBA_LEGACY_PACING") &&
        std::strcmp(std::getenv("CIPHERGBA_LEGACY_PACING"),"1")==0;
    const bool perfLogEnabled_=std::getenv("CIPHERGBA_PERF_LOG") &&
        std::strcmp(std::getenv("CIPHERGBA_PERF_LOG"),"1")==0;
    Clock::time_point fpsEpoch_{};
    Atom wmDelete_{},clipboard_{},utf8_{},pasteProperty_{};
    int w_=InitialW,h_=InitialH,menu_=-1,submenu_=-1,hover_=-1,subHover_=-1,mapping_=-1;
    int mx_=0,my_=0,slot_=1,speed_=100,selectedCheat_=-1,cheatScroll_=0;
    Panel panel_=Game;
    bool running_=true,dirty_=true,mapPad_=false,fullscreen_=false,stretched_=false;
    bool paused_=false,muted_=false,cheatsOn_=false,recordToast_=true,recordTimer_=false;
    bool tasRecording_=false,tasPlayback_=false,tasReset_=true;
    bool cheatEditing_=false,showMenuInFullscreen_=false;
    std::string cheatText_,message_,toast_;
    Clock::time_point toastTime_{},lastFrame_{};std::set<KeySym> pressed_;
    std::vector<Menu> menus_;

    std::vector<TasFrame> tas_;
    std::size_t tasCursor_=0;
    std::vector<cheats::Program> cheats_;
    bool cheatDeviceButton_=false;
    fs::path recordingsDir_;
    std::uint64_t recordingFrames_=0;
    std::string romLabel_="NO ROM";
    static Item action(std::string text,int cmd){return {std::move(text),cmd,0,false};}
    static Item branch(std::string text,int sub){return {std::move(text),0,sub,false};}
    static Item separator(){return {"",0,0,true};}
    void buildMenus(){
        menus_={
            {"File",{action("Load ROM...",LoadRom),action("Load BIOS...",LoadBios),action("Reset ROM",ResetRom),separator(),action("Quit",Quit)}},
            {"Input",{branch("GBA Controller",2)}},
            {"Configure",{action("Controller Input",InputConfig),action("Record With Toast",RecordToast),
                action("Record With Timer",RecordTimer),action("Custom AVI Output Location...",CustomVideoDir),branch("Emulator Speed",4)}},
            {"System",{action("Full Screen",FullScreen),action("Full Screen (Stretched)",FullScreenStretch),
                action("Windowed",Windowed),action("Pause / Resume",Pause),separator(),action("Mute",Mute),branch("APU",5),
                action("Save State",SaveState),action("Load State",LoadState),branch("Save State Slot",6),
                action("Screenshot",Screenshot),separator(),action("Record AVI",RecordVideo),action("Stop Recording",StopVideo)}},
            {"TAS",{action("Record Inputs",TasRecord),action("Stop Recording Inputs",TasStop),
                action("View Recorded Inputs",TasPlayback),action("Stop Viewing Inputs",TasPlaybackStop),
                action("Reset Before Input Recording",TasReset)}},
            {"Cheats",{action("Configure GBA Cheat Codes",CheatsConfig),action("Enable Cheats",CheatsEnabled)}},
            {"Help",{action("Info",Info)}}
        };
    }
    std::vector<Item> submenuItems(int id)const{
        switch(id){
            case 2:return {action("Controller Input",InputConfig)};
            case 4:return {action("100%",Speed100),action("50%",Speed50),action("25%",Speed25)};
            case 5:return {action("PSG Square 1",Audio1),action("PSG Square 2",Audio2),action("PSG Wave",Audio3),
                action("PSG Noise",Audio4),action("Direct Sound A",Audio5),action("Direct Sound B",Audio6)};
            case 6:return {action("1",Slot1),action("2",Slot2),action("3",Slot3),action("4",Slot4)};
        }
        return {};
    }
    void notice(const std::string& text){toast_=text;toastTime_=Clock::now()+std::chrono::seconds(3);dirty_=true;}
    std::string gameTag()const{
        std::ostringstream out;out<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(8)<<core_.cartridge().fingerprint();return out.str();
    }
    fs::path tasFile()const{return paths_.dataDirectory/"tas"/(gameTag()+".cgtas");}
    fs::path cheatsFile()const{return paths_.dataDirectory/"cheats"/(gameTag()+".txt");}
    void clearTransient(){tasRecording_=tasPlayback_=false;tasCursor_=0;tas_.clear();
        cheats_.clear();cheatScroll_=0;cheatsOn_=false;avi_.stop();recordingFrames_=0;audio_.reset();pressed_.clear();}
    void saveCheats(){
        if(!core_.cartridge().loaded())return;
        std::error_code ec;fs::create_directories(cheatsFile().parent_path(),ec);
        std::ofstream f(cheatsFile(),std::ios::trunc);
        f<<"CGCHEAT2\n";
        for(const auto& code:cheats_)
            f<<unsigned(code.enabled)<<' '<<std::quoted(code.source)<<'\n';
    }
    void loadCheats(){
        cheats_.clear();cheatScroll_=0;if(!core_.cartridge().loaded())return;
        std::ifstream f(cheatsFile());if(!f)return;
        std::string line;if(!std::getline(f,line))return;
        if(line=="CGCHEAT2"){
            while(std::getline(f,line)&&cheats_.size()<64){
                std::istringstream row(line);bool enabled=false;std::string source;
                if(!(row>>enabled>>std::quoted(source)))continue;
                cheats::Program program;std::string error;
                if(cheats::compile(source,program,error)){
                    program.enabled=enabled;cheats_.push_back(std::move(program));
                }
            }
            return;
        }
        // Import existing raw-cheat files, upgrade when next saved.
        f.clear();f.seekg(0);
        std::uint32_t address=0,value=0;unsigned width=0,enabled=0;
        while(f>>std::hex>>address>>value>>std::dec>>width>>enabled){
            std::ostringstream source;source<<"RAW:"<<std::hex<<std::uppercase<<std::setfill('0')
                <<std::setw(8)<<address<<":"<<std::setw(8)<<value<<":"<<std::dec<<width;
            cheats::Program program;std::string error;
            if(cheats::compile(source.str(),program,error)){
                program.enabled=enabled!=0;cheats_.push_back(std::move(program));
            }
            if(cheats_.size()>=64)break;
        }
    }
    void applyCheats(){
        core_.clearRomCheatPatches();
        if(!cheatsOn_)return;
        const cheats::Bus bus{
            [this](std::uint32_t a,unsigned width)->std::uint32_t{
                if(width==1)return core_.read8(a);
                if(width==2)return core_.read16(a);
                return core_.read32(a);
            },
            [this](std::uint32_t a,std::uint32_t v,unsigned width){
                if(width==1)core_.write8(a,std::uint8_t(v));
                else if(width==2)core_.write16(a,std::uint16_t(v));
                else core_.write32(a,v);
            },
            [this]{return cheatDeviceButton_;},
            [this](std::uint32_t a,std::uint16_t value){core_.setRomCheatPatch16(a,value);}
        };
        for(const auto& program:cheats_)cheats::execute(program,bus);
    }
    fs::path stateFile()const{return paths_.dataDirectory/"states"/
        (gameTag()+(core_.biosLoaded()?"_bios_":"_hle_")+"slot"+std::to_string(slot_)+".cgs");}
    void captureSlot(){
        std::string error;
        if(!core_.cartridge().loaded()){notice("LOAD A ROM FIRST");return;}
        if(StateCodec::save(core_,stateFile(),error))notice("SAVED STATE SLOT "+std::to_string(slot_));
        else{std::fprintf(stderr,"Save State: %s\n",error.c_str());notice("SAVE STATE FAILED: "+error);}
    }
    void restoreSlot(){
        if(!core_.cartridge().loaded()){notice("LOAD A ROM FIRST");return;}
        std::string error;
        if(StateCodec::load(core_,stateFile(),error)){
            audio_.reset();dirty_=true;notice("LOADED STATE SLOT "+std::to_string(slot_));
        }else{std::fprintf(stderr,"Load State: %s\n",error.c_str());notice("LOAD STATE FAILED: "+error);}
    }
    void startTas(){if(!core_.cartridge().loaded()){notice("LOAD A ROM FIRST");return;}
        if(tasReset_){std::string e;core_.flushSave(e);core_.reset();}
        tas_.clear();tasCursor_=0;tasPlayback_=false;tasRecording_=true;notice("TAS RECORDING");}
    void stopTas(){if(!tasRecording_)return;tasRecording_=false;
        std::error_code ec;fs::create_directories(tasFile().parent_path(),ec);
        std::ofstream f(tasFile(),std::ios::binary);
        const char tag[8]={'C','G','T','A','S','0','1','\0'};f.write(tag,8);
        const std::uint32_t crc=core_.cartridge().fingerprint(),count=std::uint32_t(tas_.size());
        f.write(reinterpret_cast<const char*>(&crc),4);f.write(reinterpret_cast<const char*>(&count),4);
        for(const auto& t:tas_)f.write(reinterpret_cast<const char*>(&t.buttons),2);
        notice(f.good()?"TAS SAVED ("+std::to_string(count)+" FRAMES)":"TAS SAVE FAILED");
    }
    void startTasPlayback(){if(!core_.cartridge().loaded()){notice("LOAD A ROM FIRST");return;}
        if(tasRecording_)stopTas();
        std::ifstream f(tasFile(),std::ios::binary);
        char tag[8]{};std::uint32_t crc=0,count=0;f.read(tag,8);f.read(reinterpret_cast<char*>(&crc),4);f.read(reinterpret_cast<char*>(&count),4);
        if(!f||std::memcmp(tag,"CGTAS01",7)!=0||crc!=core_.cartridge().fingerprint()||count>10000000u){notice("NO VALID TAS FOR THIS ROM");return;}
        std::vector<TasFrame> frames(count);
        for(auto& frame:frames)f.read(reinterpret_cast<char*>(&frame.buttons),2);
        if(!f){notice("TAS FILE INCOMPLETE");return;}
        tas_=std::move(frames);tasCursor_=0;tasPlayback_=true;core_.reset();audio_.reset();
        notice("TAS PLAYBACK");
    }
    bool writeScreenshot(){if(!core_.cartridge().loaded()){notice("LOAD A ROM FIRST");return false;}
        std::error_code ec;const auto dir=paths_.dataDirectory/"screenshots";fs::create_directories(dir,ec);
        const auto file=dir/(gameTag()+"_frame"+std::to_string(core_.frames())+".png");
        const bool ok=host::savePng(file,core_.framebuffer().data(),GbaCore::Width,GbaCore::Height);
        notice(ok?"SCREENSHOT SAVED":"SCREENSHOT FAILED");return ok;
    }
    void startAvi(){if(!core_.cartridge().loaded()){notice("LOAD A ROM FIRST");return;}
        std::error_code ec;fs::create_directories(recordingsDir_,ec);
        const auto out=recordingsDir_/(gameTag()+"_"+std::to_string(std::time(nullptr))+".avi");
        if(avi_.start(out,recordToast_,recordTimer_)){recordingFrames_=0;if(recordToast_)notice("AVI RECORDING STARTED");}
        else notice("FAILED TO START FFMPEG");
    }
    void stopAvi(){if(!avi_.active())return;avi_.stop();if(recordToast_)notice("AVI SAVED");}
    int textWidth(const std::string& s)const{return font_?XTextWidth(font_,s.c_str(),int(s.size())):int(s.size())*9;}
    int rootWidth(unsigned idx)const{return textWidth(menus_[idx].title)+36;}
    int rootX(unsigned idx)const{int x=0;for(unsigned i=0;i<idx;++i)x+=rootWidth(i);return x;}
    int dropdownWidth(int which)const{int n=260;for(const auto& i:menus_[which].items)n=std::max(n,textWidth(i.text)+73);return n;}
    bool itemChecked(int cmd)const{
        if(cmd==Mute)return muted_;
        if(cmd==Pause)return paused_;
        if(cmd==FullScreen)return fullscreen_&&!stretched_;
        if(cmd==FullScreenStretch)return fullscreen_&&stretched_;
        if(cmd==Windowed)return !fullscreen_;
        if(cmd==Speed100||cmd==Speed50||cmd==Speed25)return speed_==(cmd==Speed100?100:cmd==Speed50?50:25);
        if(cmd>=Slot1&&cmd<=Slot4)return slot_==cmd-Slot1+1;
        if(cmd>=Audio1&&cmd<=Audio6)return core_.audioChannelMask()&(1u<<(cmd-Audio1));
        if(cmd==CheatsEnabled)return cheatsOn_;
        if(cmd==TasReset)return tasReset_;
        if(cmd==RecordToast)return recordToast_;
        if(cmd==RecordTimer)return recordTimer_;
        return false;
    }
public:
    App():paths_(Paths::discover()){
        buildMenus();std::string err;bindings_=Bindings::defaults();editing_=bindings_;
        std::error_code ec;for(const char* d:{"bios","saves","config","states","tas","cheats","screenshots","recordings"})
            fs::create_directories(paths_.dataDirectory/d,ec);
        recordingsDir_=paths_.dataDirectory/"recordings";
        core_.configureSaveDirectory(paths_.dataDirectory/"saves",err);
        if(fs::exists(paths_.inputConfig))Bindings::load(paths_.inputConfig,bindings_,err);
        editing_=bindings_;
        if(fs::exists(paths_.biosFile)&&!core_.loadInstalledBios(paths_.biosFile,err))notice("BIOS: "+err);
    }
    ~App(){avi_.stop();if(dpy_){destroyScaledImage();if(back_)XFreePixmap(dpy_,back_);if(gc_)XFreeGC(dpy_,gc_);
        if(font_)XFreeFont(dpy_,font_);
        if(win_)XDestroyWindow(dpy_,win_);
        XCloseDisplay(dpy_);}if(xext_)dlclose(xext_);}
    int run(int argc,char** argv){
        dpy_=XOpenDisplay(nullptr);if(!dpy_){std::fprintf(stderr,"CipherGBA: X11/XWayland required.\n");return 1;}
        const int screen=DefaultScreen(dpy_);
        if(DefaultDepth(dpy_,screen)<24){std::fprintf(stderr,"CipherGBA: 24-bit TrueColor required.\n");return 1;}
        win_=XCreateSimpleWindow(dpy_,RootWindow(dpy_,screen),55,45,w_,h_,0,0,0x0a0e16);
        XStoreName(dpy_,win_,"CipherGBA - Ubuntu");
        setWindowIcon();
        XSelectInput(dpy_,win_,ExposureMask|StructureNotifyMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|PointerMotionMask|FocusChangeMask);
        XSetWindowBackgroundPixmap(dpy_,win_,None);
        wmDelete_=XInternAtom(dpy_,"WM_DELETE_WINDOW",False);XSetWMProtocols(dpy_,win_,&wmDelete_,1);
        clipboard_=XInternAtom(dpy_,"CLIPBOARD",False);
        utf8_=XInternAtom(dpy_,"UTF8_STRING",False);
        pasteProperty_=XInternAtom(dpy_,"CIPHERGBA_CHEAT_PASTE",False);
        gc_=XCreateGC(dpy_,win_,0,nullptr);XSetGraphicsExposures(dpy_,gc_,False);
        font_=XLoadQueryFont(dpy_,"-*-helvetica-bold-r-normal--18-*-*-*-*-*-iso8859-1");
        if(!font_)font_=XLoadQueryFont(dpy_,"9x15");
        if(font_)XSetFont(dpy_,gc_,font_->fid);
        XSizeHints sh{};sh.flags=PMinSize;sh.min_width=730;sh.min_height=540;XSetWMNormalHints(dpy_,win_,&sh);
        XkbSetDetectableAutoRepeat(dpy_,True,nullptr);XMapWindow(dpy_,win_);resize(w_,h_);
        for(int i=1;i<argc;++i){const std::string a=argv[i];
            if(a=="--rom"&&i+1<argc)loadRom(argv[++i]);
            else if(a=="--bios"&&i+1<argc)loadBios(argv[++i]);
        }
        auto nextFrame=Clock::now();lastFrame_=nextFrame;fpsEpoch_=nextFrame;
        while(running_){
            while(XPending(dpy_))handleEvent();
            joystick_.poll([&](PadMapping p){if(mapping_>=0&&mapPad_){editing_.pad[mapping_]=p;mapping_=-1;dirty_=true;}});
            if(core_.cartridge().loaded()&&panel_==Game&&!paused_){
                const auto now=Clock::now();if(now>=nextFrame){
                        if(perfLogEnabled_){
                            const double lateMs=std::chrono::duration<double,std::milli>(now-nextFrame).count();
                            if(lateMs>0.25){++lateFrameCount_;lateMsMax_=std::max(lateMsMax_,lateMs);}
                        }
                        std::array<bool,10> buttons{};
                        for(int k=0;k<Controls;k++)buttons[k]=pressed_.count(bindings_.keyboard[k])||joystick_.down(bindings_.pad[k]);
                        if(tasPlayback_){if(tasCursor_<tas_.size()){
                            const auto mask=tas_[tasCursor_++].buttons;
                            for(int i=0;i<10;i++)buttons[i]=(mask&(1u<<i))!=0;
                        }else{tasPlayback_=false;notice("TAS PLAYBACK COMPLETE");}}
                        if(tasRecording_){std::uint16_t bits=0;for(unsigned i=0;i<10;i++)if(buttons[i])bits|=1u<<i;
                            tas_.push_back({bits});if(tas_.size()>10000000){stopTas();notice("TAS FRAME LIMIT");}}
                        cheatDeviceButton_=buttons[8]&&buttons[9];
                        core_.setButtons(buttons);
                        const auto coreStart=Clock::now();
                        core_.runFrame();
                        coreMsSum_+=std::chrono::duration<double,std::milli>(Clock::now()-coreStart).count();
                        applyCheats();
                        // Only redraw when a pixel actually changed. This is
                        // presentation work only: no GBA frame is skipped.
                        const auto& pixels=core_.framebuffer();
                        const std::size_t pixelBytes=pixels.size()*sizeof(pixels[0]);
                        if(!havePreviousPixels_ ||
                           std::memcmp(previousPixels_.data(),pixels.data(),pixelBytes)!=0){
                            std::memcpy(previousPixels_.data(),pixels.data(),pixelBytes);
                            havePreviousPixels_=true;
                            frameUpdated_=true;
                        }
                        ++fpsFrames_;
                        const auto fpsNow=Clock::now();
                        const double elapsed=std::chrono::duration<double>(fpsNow-fpsEpoch_).count();
                        if(elapsed>=1.0){
                            actualFps_=fpsFrames_/elapsed;
                            avgCoreMs_=coreMsSum_/fpsFrames_;
                            avgPresentMs_=presentCount_?presentMsSum_/presentCount_:0.0;
                            if(perfLogEnabled_)std::fprintf(stderr,
                                "CipherGBA PERF fps=%.1f core_ms=%.2f x11_ms=%.2f shm=%d shm_async=%d shm_put_async=%llu shm_put_sync=%llu shm_wait=%llu pacing=%s late_frames=%llu late_max_ms=%.3f audio_queue_avg_ms=%.2f audio_queue_max_ms=%.2f\n",
                                actualFps_,avgCoreMs_,avgPresentMs_,int(shmAttached_),int(shmAsync_),
                                static_cast<unsigned long long>(shmAsyncPuts_),
                                static_cast<unsigned long long>(shmSyncPuts_),
                                static_cast<unsigned long long>(shmWaits_),
                                legacyHostPacing_?"legacy":"deadline",
                                static_cast<unsigned long long>(lateFrameCount_),lateMsMax_,
                                audioQueueSamples_?audioQueueMsSum_/double(audioQueueSamples_):0.0,audioQueueMsMax_);
                            coreMsSum_=0.0;presentMsSum_=0.0;presentCount_=0;
                            lateFrameCount_=0;lateMsMax_=0.0;
                            audioQueueMsSum_=0.0;audioQueueMsMax_=0.0;audioQueueSamples_=0;
                            fpsFrames_=0;fpsEpoch_=fpsNow;dirty_=true;
                        }
                        auto samples=core_.takeAudioSamples();
                        if(avi_.active())avi_.appendAudio(samples.data(),samples.size());
                        audio_.push(samples,!muted_&&speed_==100);
                        if(perfLogEnabled_ && !muted_ && speed_==100 && audio_.available()){
                            const double queuedMs=audio_.queuedMilliseconds();
                            audioQueueMsSum_+=queuedMs;
                            audioQueueMsMax_=std::max(audioQueueMsMax_,queuedMs);
                            ++audioQueueSamples_;
                        }
                        if(avi_.active()){avi_.append(core_.framebuffer().data());++recordingFrames_;}
                        const double factor=double(speed_)/100.0;
                        nextFrame+=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(FrameDuration/factor));
                        if(nextFrame<now-std::chrono::milliseconds(80))nextFrame=now;
                }
            }else nextFrame=Clock::now();
            if(dirty_ || frameUpdated_ ||
               (toastTime_>Clock::now() && Clock::now()-lastFrame_>std::chrono::milliseconds(100))){
                const auto paintStart=Clock::now();
                // If *only* the game framebuffer changed, repaint/copy only
                // its rectangle. Preserve the existing X11 back pixmap, menu,
                // status strip, and modal overlays. Never skip a guest frame.
                if(frameUpdated_ && !dirty_ && panel_==Game && menu_<0 &&
                   toastTime_<=Clock::now()){
                    renderFrame();
                    if(gameW_>0 && gameH_>0)
                        XCopyArea(dpy_,back_,win_,gc_,gameX_,gameY_,
                                  unsigned(gameW_),unsigned(gameH_),gameX_,gameY_);
                    XFlush(dpy_);
                }else paint();
                frameUpdated_=false;dirty_=false;lastFrame_=Clock::now();
                presentMsSum_+=std::chrono::duration<double,std::milli>(lastFrame_-paintStart).count();
                ++presentCount_;
            }
            // Do not burn 2 ms of every frame when CPU emulation is already
            // behind wall time. UI events are pumped at the top of every loop.
            // Retain a short sleep while ahead to avoid busy-waiting on laptops.
            const auto loopEnd=Clock::now();
            if(core_.cartridge().loaded() && panel_==Game && !paused_){
                if(loopEnd>=nextFrame)
                    std::this_thread::yield();
                else if(legacyHostPacing_)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                else
                    // Sleep toward the exact frame deadline, but wake at least
                    // once every 2ms to keep X11 input/menu handling responsive.
                    // The guest scheduler, cycle counts and audio remain unchanged.
                    std::this_thread::sleep_until(host::pacingWakeDeadline<Clock>(loopEnd,nextFrame));
            }else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        stopTas();avi_.stop();saveCheats();std::string err;if(!core_.flushSave(err))std::fprintf(stderr,"Save failure: %s\n",err.c_str());return 0;
    }
private:
    // Every asynchronous MIT-SHM upload is tied to one of two independent
    // segments.  A segment is *never* overwritten until the server posts
    // the matching completion event.  XSync is reserved for resize/teardown,
    // failed requests, and the conservative single-buffer fallback.
    void acknowledgeShmCompletion(const XEvent& event){
        ShmCompletionEvent done{};
        std::memcpy(&done,&event,sizeof(done)); // avoid C++ strict-aliasing UB
        for(auto& slot:shmBuffers_)
            if(slot.attached && slot.info.shmseg==done.shmseg){slot.busy=false;break;}
    }
    int availableShmBuffer(){
        for(int i=0;i<2;i++){
            const int which=(shmNext_+i)%2;
            if(shmBuffers_[which].attached && !shmBuffers_[which].busy){
                shmNext_=(which+1)%2;return which;
            }
        }
        // Both buffers have outstanding requests.  Wait *only* for a SHM
        // completion, leaving unrelated user input queued for handleEvent().
        ++shmWaits_;XFlush(dpy_);
        XEvent event{};
        XIfEvent(dpy_,&event,isShmCompletion,reinterpret_cast<XPointer>(&shmCompletionType_));
        acknowledgeShmCompletion(event);
        return availableShmBuffer();
    }
    void destroyScaledImage(){
        if(shmAttached_){
            // All server references to both buffers finish before detaching.
            XSync(dpy_,False);
            if(shmCompletionType_>=0){
                XEvent event{};
                while(XCheckIfEvent(dpy_,&event,isShmCompletion,
                                    reinterpret_cast<XPointer>(&shmCompletionType_))){}
            }
            for(auto& slot:shmBuffers_){
                if(!slot.attached)continue;
                shmDetach_(dpy_,&slot.info);
                XSync(dpy_,False);
                slot.image->data=nullptr; // never let XDestroyImage free shmat memory
                XDestroyImage(slot.image);
                shmdt(slot.info.shmaddr);
                slot={};
            }
            shmAttached_=false;shmAsync_=false;shmNext_=0;
        }else if(scaledImage_)XDestroyImage(scaledImage_);
        scaledImage_=nullptr;
    }
    bool attachShmImage(ShmBuffer& slot,Visual* visual,unsigned depth,int w,int h){
        XImage* image=shmCreate_(dpy_,visual,depth,ZPixmap,nullptr,&slot.info,
                                 unsigned(w),unsigned(h));
        if(!image)return false;
        if(image->bits_per_pixel!=32){image->data=nullptr;XDestroyImage(image);return false;}
        const std::size_t bytes=std::size_t(image->bytes_per_line)*std::size_t(h);
        slot.info.shmid=shmget(IPC_PRIVATE,bytes,IPC_CREAT|0600);
        if(slot.info.shmid<0){image->data=nullptr;XDestroyImage(image);return false;}
        void* addr=shmat(slot.info.shmid,nullptr,0);
        if(addr==(void*)-1){
            shmctl(slot.info.shmid,IPC_RMID,nullptr);
            image->data=nullptr;XDestroyImage(image);return false;
        }
        slot.info.shmaddr=static_cast<char*>(addr);
        slot.info.readOnly=False;
        image->data=slot.info.shmaddr;
        shmAttachError=false;
        const auto oldHandler=XSetErrorHandler(shmAttachErrorHandler);
        const bool attached=shmAttach_(dpy_,&slot.info)!=0;
        XSync(dpy_,False); // catch asynchronous BadAccess during initialization
        XSetErrorHandler(oldHandler);
        if(attached && !shmAttachError){
            shmctl(slot.info.shmid,IPC_RMID,nullptr);
            slot.image=image;slot.attached=true;return true;
        }
        if(attached){shmDetach_(dpy_,&slot.info);XSync(dpy_,False);}
        image->data=nullptr;XDestroyImage(image);shmdt(addr);
        shmctl(slot.info.shmid,IPC_RMID,nullptr);slot={};
        return false;
    }
    bool createScaledImage(int destW,int destH){
        destroyScaledImage();
        const unsigned depth=unsigned(DefaultDepth(dpy_,DefaultScreen(dpy_)));
        auto* visual=DefaultVisual(dpy_,DefaultScreen(dpy_));
        if(!shmChecked_){
            shmChecked_=true;
            // Never request shared memory from SSH-forwarded/remote X servers.
            const char* display=std::getenv("DISPLAY");
            if(display && display[0]==':' && !std::getenv("CIPHERGBA_DISABLE_XSHM")){
                xext_=dlopen("libXext.so.6",RTLD_NOW|RTLD_LOCAL);
                if(xext_){
                    const auto query=reinterpret_cast<ShmQuery>(dlsym(xext_,"XShmQueryExtension"));
                    shmCreate_=reinterpret_cast<ShmCreate>(dlsym(xext_,"XShmCreateImage"));
                    shmAttach_=reinterpret_cast<ShmAttach>(dlsym(xext_,"XShmAttach"));
                    shmDetach_=reinterpret_cast<ShmDetach>(dlsym(xext_,"XShmDetach"));
                    shmPut_=reinterpret_cast<ShmPut>(dlsym(xext_,"XShmPutImage"));
                    shmEventBase_=reinterpret_cast<ShmEventBase>(dlsym(xext_,"XShmGetEventBase"));
                    if(!query||!shmCreate_||!shmAttach_||!shmDetach_||!shmPut_||!query(dpy_))
                        shmCreate_=nullptr;
                    if(shmEventBase_)shmCompletionType_=shmEventBase_(dpy_);
                }
            }
        }
        if(shmCreate_ && attachShmImage(shmBuffers_[0],visual,depth,destW,destH)){
            shmAttached_=true;scaledImage_=shmBuffers_[0].image;
            // If the second segment can't attach, retain the proven single-
            // buffer synchronous MIT-SHM implementation with no new hazards.
            if(shmCompletionType_>=0 && !std::getenv("CIPHERGBA_DISABLE_XSHM_ASYNC") &&
               attachShmImage(shmBuffers_[1],visual,depth,destW,destH))
                shmAsync_=true;
            return true;
        }
        shmCreate_=nullptr; // transparent XPutImage fallback after SHM failure
        auto* pixels=static_cast<char*>(std::malloc(std::size_t(destW)*std::size_t(destH)*4u));
        if(!pixels)return false;
        scaledImage_=XCreateImage(dpy_,visual,depth,ZPixmap,0,pixels,
                                 unsigned(destW),unsigned(destH),32,0);
        if(!scaledImage_){std::free(pixels);return false;}
        return true;
    }
    // Brand images are embedded as compressed RGBA data. This keeps the About
    // artwork and WM icon functional even when the executable is moved away
    // from the source tree or launched through a desktop shortcut.
    void setWindowIcon(){
        std::vector<unsigned long> argb;
        for(const auto& icon : {artwork::kIcon64,artwork::kIcon32}){
            std::vector<unsigned char> pixels;
            if(!artwork::decode(icon,pixels))continue;
            argb.push_back(static_cast<unsigned long>(icon.width));
            argb.push_back(static_cast<unsigned long>(icon.height));
            for(std::size_t i=0;i<pixels.size();i+=4){
                const unsigned long a=pixels[i+3],r=pixels[i],g=pixels[i+1],b=pixels[i+2];
                argb.push_back((a<<24)|(r<<16)|(g<<8)|b);
            }
        }
        if(!argb.empty()){
            const Atom property=XInternAtom(dpy_,"_NET_WM_ICON",False);
            XChangeProperty(dpy_,win_,property,XA_CARDINAL,32,PropModeReplace,
                reinterpret_cast<const unsigned char*>(argb.data()),int(argb.size()));
        }
    }
    void drawAboutBrand(int left,int top,int desiredWidth){
        std::vector<unsigned char> source;
        if(!artwork::decode(artwork::kAboutArtwork,source)){
            label(left,top+40,"CipherGBA",0x69c5ff);return;
        }
        const int width=std::max(1,desiredWidth);
        const int height=std::max(1,width*artwork::kAboutArtwork.height/artwork::kAboutArtwork.width);
        // About-panel background (see renderPanel). Alpha is composited once
        // into the upload image, so no XRender or extra runtime library is needed.
        const auto visual=DefaultVisual(dpy_,DefaultScreen(dpy_));
        const unsigned depth=unsigned(DefaultDepth(dpy_,DefaultScreen(dpy_)));
        auto* pixels=static_cast<char*>(std::malloc(std::size_t(width)*height*4u));
        if(!pixels)return;
        XImage* img=XCreateImage(dpy_,visual,depth,ZPixmap,0,pixels,
                                  unsigned(width),unsigned(height),32,0);
        if(!img){std::free(pixels);return;}
        for(int y=0;y<height;y++){
            const std::size_t sy=std::size_t(y)*artwork::kAboutArtwork.height/height;
            for(int x=0;x<width;x++){
                const std::size_t sx=std::size_t(x)*artwork::kAboutArtwork.width/width;
                const std::size_t pos=(sy*artwork::kAboutArtwork.width+sx)*4u;
                const unsigned a=source[pos+3],inv=255-a;
                const unsigned r=(source[pos]*a+9*inv+127)/255;
                const unsigned g=(source[pos+1]*a+15*inv+127)/255;
                const unsigned b=(source[pos+2]*a+24*inv+127)/255;
                XPutPixel(img,x,y,(r<<16)|(g<<8)|b);
            }
        }
        XPutImage(dpy_,back_,gc_,img,0,0,left,top,unsigned(width),unsigned(height));
        XDestroyImage(img);
    }
    void color(unsigned long c){XSetForeground(dpy_,gc_,c);}
    void rect(int x,int y,int w,int h,unsigned long c){if(w<1||h<1)return;color(c);XFillRectangle(dpy_,back_,gc_,x,y,unsigned(w),unsigned(h));}
    void stroke(int x,int y,int w,int h,unsigned long c){if(w<1||h<1)return;color(c);XDrawRectangle(dpy_,back_,gc_,x,y,unsigned(w-1),unsigned(h-1));}
    void label(int x,int base,const std::string& text,unsigned long c){color(c);XDrawString(dpy_,back_,gc_,x,base,text.c_str(),int(text.size()));}
    void button(int x,int y,int w,int h,const std::string& text,bool active=false){rect(x,y,w,h,active?0x285580:0x23384b);stroke(x,y,w,h,0x557da1);label(x+12,y+h/2+6,text,0xe9f2ff);}
    void resize(int w,int h){w_=std::max(1,w);h_=std::max(1,h);if(back_)XFreePixmap(dpy_,back_);
        back_=XCreatePixmap(dpy_,win_,unsigned(w_),unsigned(h_),unsigned(DefaultDepth(dpy_,DefaultScreen(dpy_))));dirty_=true;}
    bool barVisible()const{return !fullscreen_||showMenuInFullscreen_||menu_>=0||my_<Bar;}
    void renderFrame(){
        if(!core_.cartridge().loaded())return;
        constexpr int srcW=GbaCore::Width,srcH=GbaCore::Height;
        const bool bar=barVisible();const int top=bar?Bar:0;
        const int contentH=h_-top-(fullscreen_?0:StatusH);
        const double desired=std::min(double(w_)/srcW,double(contentH)/srcH);
        const int scale=std::max(1,int(std::floor(desired)));
        const int destW=stretched_&&fullscreen_?w_:srcW*scale;
        const int destH=stretched_&&fullscreen_?contentH:srcH*scale;
        if(destW>w_||destH>contentH||destW<1||destH<1)return;
        const int x=(w_-destW)/2,y=top+(contentH-destH)/2;
        gameX_=x;gameY_=y;gameW_=destW;gameH_=destH;
        if(!scaledImage_ || scaledImage_->width!=destW || scaledImage_->height!=destH){
            if(!createScaledImage(destW,destH))return;
            // Pixel mapping is a function of the window size, not frame data.
            stretchXMap_.resize(std::size_t(destW));
            for(int col=0;col<destW;col++)
                stretchXMap_[std::size_t(col)]=unsigned(std::size_t(col)*srcW/destW);
        }
        const int shmWriteSlot=shmAttached_ && shmAsync_?availableShmBuffer():0;
        if(shmAttached_)scaledImage_=shmBuffers_[shmWriteSlot].image;
        auto* dst=reinterpret_cast<std::uint32_t*>(scaledImage_->data);
        const auto& src=core_.framebuffer();
        const int integerScale=destW%srcW==0 && destH%srcH==0 &&
            destW/srcW==destH/srcH?destW/srcW:0;
        if(integerScale){
            // Default no-stretch view: replicate pixels and completed scanlines
            // without hundreds of thousands of integer divisions each frame.
            for(int sy=0;sy<srcH;sy++){
                auto* row=dst+std::size_t(sy)*integerScale*destW;
                const auto* source=src.data()+std::size_t(sy)*srcW;
                for(int sx=0;sx<srcW;sx++)
                    std::fill_n(row+std::size_t(sx)*integerScale,integerScale,source[sx]);
                for(int k=1;k<integerScale;k++)
                    std::memcpy(row+std::size_t(k)*destW,row,std::size_t(destW)*4u);
            }
        }else{
            // Fullscreen stretched mode retains the exact original nearest-
            // neighbor source mapping. Duplicate identical source scanlines.
            int previousSourceRow=-1;
            for(int row=0;row<destH;row++){
                const int sy=int(std::size_t(row)*srcH/destH);
                auto* output=dst+std::size_t(row)*destW;
                if(sy==previousSourceRow){
                    std::memcpy(output,output-destW,std::size_t(destW)*4u);
                    continue;
                }
                previousSourceRow=sy;
                const auto* source=src.data()+std::size_t(sy)*srcW;
                for(int col=0;col<destW;col++)
                    output[col]=source[stretchXMap_[std::size_t(col)]];
            }
        }
        if(shmAttached_){
            if(shmAsync_){
                if(shmPut_(dpy_,back_,gc_,scaledImage_,0,0,x,y,
                           unsigned(destW),unsigned(destH),True)){
                    shmBuffers_[shmWriteSlot].busy=true;
                    ++shmAsyncPuts_;
                }else{
                    // No request is in flight for this segment on failure.
                    // The old synchronous upload is safe and remains available.
                    shmAsync_=false;
                    shmPut_(dpy_,back_,gc_,scaledImage_,0,0,x,y,
                            unsigned(destW),unsigned(destH),False);
                    XSync(dpy_,False);++shmSyncPuts_;
                }
            }else{
                shmPut_(dpy_,back_,gc_,scaledImage_,0,0,x,y,
                        unsigned(destW),unsigned(destH),False);
                XSync(dpy_,False);++shmSyncPuts_;
            }
        }else XPutImage(dpy_,back_,gc_,scaledImage_,0,0,x,y,unsigned(destW),unsigned(destH));
    }
    void renderMenus(){
        if(!barVisible())return;
        rect(0,0,w_,Bar,0x182330);rect(0,Bar-1,w_,1,0x486988);
        for(unsigned i=0;i<menus_.size();i++){
            const int x=rootX(i),width=rootWidth(i);
            if(menu_==int(i)||(mx_>=x&&mx_<x+width&&my_<Bar))rect(x,0,width,Bar,menu_==int(i)?0x2d4c6d:0x22405e);
            label(x+17,Bar-13,menus_[i].title,0xeef3f8);
        }
        if(menu_<0||menu_>=int(menus_.size()))return;
        const auto& items=menus_[menu_].items;const int x=rootX(menu_),width=dropdownWidth(menu_);
        const int bottom=Bar+int(items.size())*MenuRowH;
        rect(x,Bar,width,bottom-Bar,0x131c27);stroke(x,Bar,width,bottom-Bar,0x486887);
        for(unsigned i=0;i<items.size();i++){
            const int rowY=Bar+int(i)*MenuRowH;const auto& item=items[i];
            if(item.separator){rect(x+13,rowY+MenuRowH/2,width-26,1,0x485b6e);continue;}
            if(int(i)==hover_)rect(x+1,rowY,width-2,MenuRowH,0x265580);
            if(itemChecked(item.cmd))label(x+13,rowY+22,"X",0xffda5b);
            label(x+35,rowY+22,item.text,0xf0f5fa);
            if(item.submenu)label(x+width-21,rowY+22,">",0xb4cfe5);
        }
        if(submenu_>=0&&submenu_<int(items.size())&&items[submenu_].submenu){
            const auto child=submenuItems(items[submenu_].submenu);
            const int sx=std::min(x+width,w_-222),sy=Bar+submenu_*MenuRowH,sw=222;
            rect(sx,sy,sw,int(child.size())*MenuRowH,0x131c27);stroke(sx,sy,sw,int(child.size())*MenuRowH,0x486887);
            for(unsigned i=0;i<child.size();i++){
                const int yy=sy+int(i)*MenuRowH;
                if(int(i)==subHover_)rect(sx+1,yy,sw-2,MenuRowH,0x265580);
                if(itemChecked(child[i].cmd))label(sx+13,yy+22,"X",0xffda5b);
                label(sx+35,yy+22,child[i].text,0xf1f6fb);
            }
        }
    }
    void renderStatus(){
        if(fullscreen_)return;
        rect(0,h_-StatusH,w_,StatusH,0x070d15);rect(0,h_-StatusH,w_,1,0x486988);
        std::string status=romLabel_+"  SLOT "+std::to_string(slot_)+"  "+std::to_string(speed_)+"%";
        if(core_.cartridge().loaded()){
            status+="  "+std::to_string(int(actualFps_+0.5))+" FPS";
            char times[72];
            std::snprintf(times,sizeof(times),"  CORE %.1fms  X11 %.1fms",avgCoreMs_,avgPresentMs_);
            status+=times;
        }
        if(paused_)status+="  PAUSED";
        if(muted_)status+="  MUTED";
        if(cheatsOn_)status+="  CHEATS";
        if(avi_.active())status+="  REC"+(recordTimer_?(" "+std::to_string(recordingFrames_/60u)+"s"):"");
        if(tasRecording_)status+="  TAS REC "+std::to_string(tas_.size());
        if(tasPlayback_)status+="  TAS PLAY "+std::to_string(tasCursor_)+"/"+std::to_string(tas_.size());
        if(!audio_.available())status+="  AUDIO UNAVAILABLE";
        label(13,h_-9,status,0xe1edf7);
    }
    void renderToast(){
        if(toastTime_<=Clock::now())return;
        const int width=std::min(w_-22,textWidth(toast_)+32);const int left=(w_-width)/2,y=h_-(fullscreen_?60:StatusH+65);
        rect(left,y,width,40,0x04080e);stroke(left,y,width,40,0xffcd46);label(left+16,y+27,toast_,0xffec99);
    }
    void renderPanel(){
        if(panel_==Game)return;
        rect(0,Bar,w_,h_-Bar-(fullscreen_?0:StatusH),0x090f18);
        if(panel_==About){
            const int logoW=std::max(1,std::min({w_-46,420,h_-Bar-StatusH-280}));
            const int logoH=logoW*artwork::kAboutArtwork.height/artwork::kAboutArtwork.width;
            drawAboutBrand((w_-logoW)/2,Bar+15,logoW);
            const int baseline=Bar+logoH+54;
            label(28,baseline,"CipherGBA v0.3.0  |  C++17  |  HodgkinsStudios",0xe2edff);
            label(28,baseline+34,"Game Boy Advance Emulator  -  Linux X11/XWayland",0xcbd9e9);
            label(28,baseline+68,"ARM7TDMI  |  Integrated HLE or user-supplied original BIOS",0xcbd9e9);
            label(28,baseline+102,"suite.gba: 6,994 / 6,998 in both BIOS modes; 4 HBlank cases unresolved",0x9ebbd6);
            button(26,h_-StatusH-74,190,42,"RETURN TO GAME");return;
        }
        if(panel_==Controller){
            label(25,Bar+42,"CONTROLLER INPUT",0x67c3ff);
            label(25,Bar+72,"Click a binding, then press a keyboard key or gamepad button / axis.",0xc3d9e9);
            label(25,Bar+105,"GBA",0xa7c6df);label(197,Bar+105,"KEYBOARD",0xa7c6df);label(476,Bar+105,"GAMEPAD",0xa7c6df);
            for(int i=0;i<Controls;i++){
                const int yy=Bar+121+i*39;
                label(25,yy+23,inputNames[i],0xf0f4fa);
                button(176,yy,260,33,keyName(editing_.keyboard[i]),mapping_==i&&!mapPad_);
                button(460,yy,285,33,padName(editing_.pad[i]),mapping_==i&&mapPad_);
            }
            if(mapping_>=0)label(25,h_-StatusH-102,mapPad_?"WAITING FOR GAMEPAD INPUT...":"PRESS A KEY...",0xffd777);
            button(24,h_-StatusH-72,144,41,"SAVE");button(179,h_-StatusH-72,160,41,"DEFAULTS");
            button(350,h_-StatusH-72,154,41,"CANCEL");return;
        }
        label(25,Bar+43,"GBA CHEAT CODES",0x67c3ff);
        label(25,Bar+75,"CB:XXXXXXXX XXXX   GS:XXXXXXXX XXXXXXXX   AR:XXXXXXXX XXXXXXXX",0xc8d9e9);
        label(25,Bar+103,"RAW:address:value:bits | Semicolon separates linked code lines.",0x9eb9d1);
        for(std::size_t i=std::size_t(cheatScroll_);i<cheats_.size()&&i<std::size_t(cheatScroll_+10);i++){
            const auto& c=cheats_[i];const int yy=Bar+122+(int(i)-cheatScroll_)*38;
            rect(20,yy,w_-45,32,int(i)==selectedCheat_?0x284b6b:0x1b2937);
            std::string display="["+std::string(c.enabled?"X":" ")+"] "+c.source;
            if(c.source.find(':')==std::string::npos)display="["+std::string(c.enabled?"X":" ")+"] "+cheats::formatName(c.format)+"  "+c.source;
            if(display.size()>100)display.resize(97),display+="...";
            label(31,yy+23,display,0xe2f1ff);
        }
        if(cheatEditing_){
            const int y=h_-StatusH-155;
            label(30,y,"ENTER RAW / CB / GS / AR CODE; ; JOINS LINES; ENTER TO ADD:",0xffd777);
            rect(25,y+12,std::min(w_-65,620),42,0x223f59);
            const auto visible=cheatText_.size()>68?"..."+cheatText_.substr(cheatText_.size()-68):cheatText_;
            label(35,y+40,visible+"_",0xfafafa);
        }
        button(25,h_-StatusH-73,135,42,"ADD");button(172,h_-StatusH-73,143,42,"REMOVE");
        button(327,h_-StatusH-73,154,42,"SAVE");button(493,h_-StatusH-73,154,42,"RETURN");
    }
    void paint(){
        rect(0,0,w_,h_,0x0a0e16);
        if(panel_==Game)renderFrame();else renderPanel();
        renderStatus();renderToast();renderMenus();
        XCopyArea(dpy_,back_,win_,gc_,0,0,unsigned(w_),unsigned(h_),0,0);XFlush(dpy_);
    }
    void fullscreen(bool on,bool stretched){
        const bool modeChanged=fullscreen_!=on;
        fullscreen_=on;stretched_=stretched;showMenuInFullscreen_=false;menu_=-1;
        if(modeChanged){
            const Atom state=XInternAtom(dpy_,"_NET_WM_STATE",False),fs=XInternAtom(dpy_,"_NET_WM_STATE_FULLSCREEN",False);
            XEvent ev{};ev.type=ClientMessage;ev.xclient.window=win_;ev.xclient.message_type=state;ev.xclient.format=32;
            ev.xclient.data.l[0]=on?1:0;ev.xclient.data.l[1]=long(fs);ev.xclient.data.l[3]=1;
            XSendEvent(dpy_,DefaultRootWindow(dpy_),False,SubstructureNotifyMask|SubstructureRedirectMask,&ev);XFlush(dpy_);
        }
        dirty_=true;
    }
    void loadRom(const fs::path& path){std::string e;avi_.stop();
        if(!core_.loadCartridge(path,e)){notice("LOAD ROM FAILED");std::fprintf(stderr,"Load ROM: %s\n",e.c_str());return;}
        clearTransient();romLabel_=core_.cartridge().title();loadCheats();panel_=Game;notice("LOADED "+romLabel_);
    }
    void loadBios(const fs::path& path){std::string e;if(!core_.flushSave(e)){notice("SAVE FLUSH FAILED");return;}
        if(!core_.importBios(path,paths_.biosFile,e)){notice("INVALID BIOS");std::fprintf(stderr,"Load BIOS: %s\n",e.c_str());return;}
        clearTransient();loadCheats();notice("ORIGINAL BIOS LOADED");
    }
    void resetRom(){if(!core_.cartridge().loaded())return;std::string e;core_.flushSave(e);
        core_.reset();audio_.reset();tasRecording_=tasPlayback_=false;notice("GBA RESET");}
    void execute(int cmd){menu_=-1;submenu_=-1;hover_=subHover_=-1;dirty_=true;
        if(cmd>=Slot1&&cmd<=Slot4){slot_=cmd-Slot1+1;notice("STATE SLOT "+std::to_string(slot_));return;}
        if(cmd>=Audio1&&cmd<=Audio6){core_.setAudioChannelMask(core_.audioChannelMask()^(1u<<(cmd-Audio1)));
            notice("GBA AUDIO CHANNEL TOGGLED");return;}
        switch(cmd){
            case LoadRom:{const auto file=selectFile(0,fs::current_path());if(!file.empty())loadRom(file);break;}
            case LoadBios:{const auto file=selectFile(1,fs::current_path());if(!file.empty())loadBios(file);break;}
            case ResetRom:resetRom();break;
            case Quit:running_=false;break;
            case FullScreen:fullscreen(true,false);break;
            case FullScreenStretch:fullscreen(true,true);break;
            case Windowed:fullscreen(false,false);break;
            case Pause:paused_=!paused_;audio_.reset();notice(paused_?"PAUSED":"RESUMED");break;
            case Mute:muted_=!muted_;audio_.reset();break;
            case Speed100:speed_=100;audio_.reset();break;
            case Speed50:speed_=50;audio_.reset();break;
            case Speed25:speed_=25;audio_.reset();break;
            case InputConfig:panel_=Controller;editing_=bindings_;mapping_=-1;pressed_.clear();break;
            case Screenshot:writeScreenshot();break;
            case SaveState:captureSlot();break;
            case LoadState:restoreSlot();break;
            case RecordVideo:startAvi();break;
            case StopVideo:stopAvi();break;
            case TasRecord:startTas();break;
            case TasStop:stopTas();break;
            case TasPlayback:startTasPlayback();break;
            case TasPlaybackStop:tasPlayback_=false;notice("TAS PLAYBACK STOPPED");break;
            case TasReset:tasReset_=!tasReset_;break;
            case CheatsConfig:panel_=Cheats;loadCheats();selectedCheat_=-1;break;
            case CheatsEnabled:cheatsOn_=!cheatsOn_;break;
            case RecordToast:recordToast_=!recordToast_;break;
            case RecordTimer:recordTimer_=!recordTimer_;break;
            case CustomVideoDir:{const auto dir=selectFile(2,recordingsDir_);
                if(!dir.empty()){recordingsDir_=dir;notice("RECORDING FOLDER UPDATED");}break;}
            case Info:panel_=About;break;
        }
    }
    bool panelClick(int x,int y){
        if(panel_==Controller){
            for(int k=0;k<Controls;k++){const int yy=Bar+121+k*39;
                if(y>=yy&&y<yy+33){if(x>=176&&x<436){mapping_=k;mapPad_=false;}
                    else if(x>=460&&x<745){mapping_=k;mapPad_=true;}
                    dirty_=true;return true;}}
            if(y>=h_-StatusH-72&&y<h_-StatusH-31){
                if(x>=24&&x<168){std::string e;if(editing_.save(paths_.inputConfig,e)){
                    bindings_=editing_;panel_=Game;notice("CONTROLLER INPUT SAVED");}else notice("CANNOT SAVE INPUT CONFIG");mapping_=-1;}
                else if(x>=179&&x<339){editing_=Bindings::defaults();mapping_=-1;}
                else if(x>=350&&x<504){panel_=Game;mapping_=-1;}
            }
            dirty_=true;return true;
        }
        if(panel_==About){if(y>=h_-StatusH-74&&x<230){panel_=Game;dirty_=true;}return true;}
        if(panel_==Cheats){
            if(y>=Bar+122&&y<Bar+122+std::min(10,int(cheats_.size())-cheatScroll_)*38){
                const int which=(y-Bar-122)/38+cheatScroll_;if(which>=0&&which<int(cheats_.size())){
                    selectedCheat_=which;cheats_[which].enabled=!cheats_[which].enabled;saveCheats();dirty_=true;
                }return true;
            }
            if(y>=h_-StatusH-73&&y<h_-StatusH-31){
                if(x>=25&&x<160){cheatEditing_=true;cheatText_.clear();}
                else if(x>=172&&x<315){if(selectedCheat_>=0&&selectedCheat_<int(cheats_.size())){
                    cheats_.erase(cheats_.begin()+selectedCheat_);selectedCheat_=-1;
                    cheatScroll_=std::min(cheatScroll_,std::max(0,int(cheats_.size())-10));saveCheats();}}
                else if(x>=327&&x<481){saveCheats();notice("CHEATS SAVED");}
                else if(x>=493&&x<647){saveCheats();panel_=Game;cheatEditing_=false;}
            }
            dirty_=true;return true;
        }
        return false;
    }
    void click(int x,int y){
        if(panel_!=Game&&y>=Bar){panelClick(x,y);return;}
        if(y<Bar){int chosen=-1;for(unsigned i=0;i<menus_.size();i++)if(x>=rootX(i)&&x<rootX(i)+rootWidth(i)){chosen=int(i);break;}
            if(chosen>=0){if(menu_==chosen){menu_=-1;submenu_=-1;}else{menu_=chosen;submenu_=-1;hover_=-1;}}
            else{menu_=-1;submenu_=-1;}dirty_=true;return;}
        if(menu_>=0){
            const int left=rootX(menu_),width=dropdownWidth(menu_);
            if(submenu_>=0){
                const auto& parent=menus_[menu_].items[submenu_];const auto child=submenuItems(parent.submenu);
                const int sx=std::min(left+width,w_-222),sy=Bar+submenu_*MenuRowH;
                if(x>=sx&&x<sx+222&&y>=sy&&y<sy+int(child.size())*MenuRowH){
                    execute(child[(y-sy)/MenuRowH].cmd);return;
                }
            }
            const auto& list=menus_[menu_].items;
            if(x>=left&&x<left+width&&y>=Bar&&y<Bar+int(list.size())*MenuRowH){
                const auto& item=list[(y-Bar)/MenuRowH];if(item.separator)return;
                if(item.submenu){submenu_=(y-Bar)/MenuRowH;hover_=submenu_;dirty_=true;}
                else execute(item.cmd);
                return;
            }
        }
        menu_=submenu_=-1;dirty_=true;
    }
    void motion(int x,int y){
        mx_=x;my_=y;
        if(fullscreen_){const bool open=y<Bar||menu_>=0;if(open!=showMenuInFullscreen_){showMenuInFullscreen_=open;dirty_=true;}}
        if(menu_>=0){
            if(y<Bar){for(unsigned i=0;i<menus_.size();i++)if(x>=rootX(i)&&x<rootX(i)+rootWidth(i)){
                if(menu_!=int(i)){menu_=int(i);submenu_=-1;hover_=-1;dirty_=true;}break;}
            }else{
                const int left=rootX(menu_),width=dropdownWidth(menu_);
                const auto& list=menus_[menu_].items;
                int row=-1,sub=-1;
                if(x>=left&&x<left+width&&y>=Bar&&y<Bar+int(list.size())*MenuRowH){
                    row=(y-Bar)/MenuRowH;if(list[row].submenu)sub=row;
                }
                if(row!=hover_){hover_=row;dirty_=true;}
                if(sub!=-1&&sub!=submenu_){submenu_=sub;dirty_=true;}
                else if(row>=0&&!list[row].submenu&&submenu_!=-1){submenu_=-1;dirty_=true;}
                if(submenu_>=0){const auto children=submenuItems(list[submenu_].submenu);
                    const int sx=std::min(left+width,w_-222),sy=Bar+submenu_*MenuRowH;
                    const int next=(x>=sx&&x<sx+222&&y>=sy&&y<sy+int(children.size())*MenuRowH)?(y-sy)/MenuRowH:-1;
                    if(next!=subHover_){subHover_=next;dirty_=true;}
                }
            }
        }
    }
    void handleEvent(){XEvent e{};XNextEvent(dpy_,&e);
        if(shmAsync_ && e.type==shmCompletionType_){
            acknowledgeShmCompletion(e);return;
        }
        switch(e.type){
            case Expose:dirty_=true;break;
            case ConfigureNotify:if(e.xconfigure.width!=w_||e.xconfigure.height!=h_)resize(e.xconfigure.width,e.xconfigure.height);break;
            case ClientMessage:if(Atom(e.xclient.data.l[0])==wmDelete_)running_=false;break;
            case FocusOut:pressed_.clear();break;
            case MotionNotify:motion(e.xmotion.x,e.xmotion.y);break;
            case ButtonPress:
                if(e.xbutton.button==Button1)click(e.xbutton.x,e.xbutton.y);
                else if(panel_==Cheats&&(e.xbutton.button==Button4||e.xbutton.button==Button5)){
                    const int direction=e.xbutton.button==Button5?1:-1;
                    cheatScroll_=std::clamp(cheatScroll_+direction,0,std::max(0,int(cheats_.size())-10));
                    dirty_=true;
                }
                break;
            case SelectionNotify:
                if(cheatEditing_&&e.xselection.selection==clipboard_&&e.xselection.property!=None){
                    Atom type=None;int format=0;unsigned long n=0,bytes=0;unsigned char* text=nullptr;
                    const int rc=XGetWindowProperty(dpy_,win_,pasteProperty_,0,4096,True,AnyPropertyType,
                        &type,&format,&n,&bytes,&text);
                    if(rc==Success&&format==8&&text){
                        for(unsigned long i=0;i<n&&cheatText_.size()<1024;i++){
                            const unsigned char c=text[i];
                            if(c=='\n'||c=='\r'){
                                if(!cheatText_.empty()&&cheatText_.back()!=';')cheatText_+=';';
                            }else if(std::isprint(c))cheatText_+=char(c);
                        }
                        dirty_=true;
                    }
                    if(text)XFree(text);
                }
                break;
            case KeyPress:{
                KeySym key=XLookupKeysym(&e.xkey,0);
                if(panel_==Controller){if(mapping_>=0&&!mapPad_){editing_.keyboard[mapping_]=std::uint32_t(key);mapping_=-1;dirty_=true;}
                    else if(key==XK_Escape){panel_=Game;mapping_=-1;dirty_=true;}
                    break;}
                if(panel_==Cheats&&cheatEditing_){
                    if((e.xkey.state&ControlMask)&&(key==XK_v||key==XK_V)){
                        XConvertSelection(dpy_,clipboard_,utf8_,pasteProperty_,win_,CurrentTime);
                        break;
                    }
                    if(key==XK_Return){cheats::Program c;std::string error;
                        if(cheats_.size()<64&&cheats::compile(cheatText_,c,error)){
                            cheats_.push_back(std::move(c));
                            cheatScroll_=std::max(0,int(cheats_.size())-10);
                            saveCheats();notice("GBA CHEAT ADDED");}
                        else notice(error.empty()?"CHEAT LIST FULL":"INVALID CODE: "+error);
                        cheatEditing_=false;cheatText_.clear();}
                    else if(key==XK_Escape){cheatEditing_=false;cheatText_.clear();}
                    else if(key==XK_BackSpace){if(!cheatText_.empty())cheatText_.pop_back();}
                    else{char ch[16]{};KeySym translated;const int count=XLookupString(&e.xkey,ch,sizeof(ch),&translated,nullptr);
                        for(int i=0;i<count;i++)if((std::isprint((unsigned char)ch[i]))&&cheatText_.size()<512)cheatText_+=ch[i];}
                    dirty_=true;break;
                }
                if(panel_!=Game&&key==XK_Escape){panel_=Game;dirty_=true;break;}
                if(key==XK_Escape&&menu_>=0){menu_=submenu_=-1;dirty_=true;break;}
                if(key==XK_F5){captureSlot();break;}
                if(key==XK_F8){restoreSlot();break;}
                if(key==XK_F9){writeScreenshot();break;}
                if(key==XK_F7){execute(Pause);break;}
                pressed_.insert(key);break;
            }
            case KeyRelease:{const KeySym key=XLookupKeysym(&e.xkey,0);
                pressed_.erase(key);
                break;}
        }
    }
};
} // namespace pixelgba
int main(int argc,char** argv){
        for(int i=1;i<argc;++i){
            const std::string arg=argv[i];
            if(arg=="--version"){std::puts("CipherGBA v0.3.0");return 0;}
            if(arg=="--help"){
                std::puts("CipherGBA v0.3 - GBA emulator for Linux");
                std::puts("Usage: CipherGBA [--rom PATH] [--bios PATH] [--help] [--version]");
                std::puts("Without --bios, the integrated HLE BIOS is used unless a saved BIOS is installed.");
                return 0;
            }
        }
    pixelgba::App app;return app.run(argc,argv);
}
