// Created by Jacob Hodgkins.
// CipherGBA shared library adapter: no dependency on any desktop frontend.
#include "ciphergba_core.h"
#include "pixelgba/Core.hpp"
#include <array>
#include <algorithm>
#include <memory>
#include <new>
#include <string>
#include <exception>
#include <utility>

struct CIPHERGBA_Handle {
    pixelgba::GbaCore core;
    std::string error, title;
};
namespace {
template<class Fn>
int checked(CIPHERGBA_Handle* h,Fn&& operation){
    if(!h)return 0;
    try {
        h->error.clear();
        return operation()?1:0;
    } catch(const std::exception& e){h->error=e.what();return 0;}
    catch(...){h->error="Unknown emulator error";return 0;}
}
}
extern "C" {
int ciphergba_core_api_version(void){return CIPHERGBA_CORE_API_VERSION;}
const char* ciphergba_core_version(void){return "CipherGBA_Core 1.0.0";}
CIPHERGBA_Handle* ciphergba_create(void){try{return new CIPHERGBA_Handle;}catch(...){return nullptr;}}
void ciphergba_destroy(CIPHERGBA_Handle* h){delete h;}
const char* ciphergba_last_error(const CIPHERGBA_Handle* h){return h?h->error.c_str():"Invalid emulator handle";}
int ciphergba_set_save_directory(CIPHERGBA_Handle* h,const char* path){
    return checked(h,[&]{if(!path){h->error="Missing directory";return false;}
        return h->core.configureSaveDirectory(path,h->error);});
}
int ciphergba_load_rom(CIPHERGBA_Handle* h,const char* path){
    return checked(h,[&]{if(!path){h->error="Missing ROM path";return false;}
        if(!h->core.loadCartridge(path,h->error))return false;
        h->title=h->core.cartridge().title();return true;});
}
int ciphergba_load_bios(CIPHERGBA_Handle* h,const char* path){
    return checked(h,[&]{if(!path){h->error="Missing BIOS path";return false;}
        return h->core.loadInstalledBios(path,h->error);});
}
int ciphergba_import_bios(CIPHERGBA_Handle* h,const char* input,const char* output){
    return checked(h,[&]{if(!input||!output){h->error="Missing BIOS path";return false;}
        return h->core.importBios(input,output,h->error);});
}
int ciphergba_rom_loaded(const CIPHERGBA_Handle* h){return h&&h->core.cartridge().loaded()?1:0;}
int ciphergba_bios_loaded(const CIPHERGBA_Handle* h){return h&&h->core.biosLoaded()?1:0;}
const char* ciphergba_rom_title(CIPHERGBA_Handle* h){return h?h->title.c_str():"";}
uint32_t ciphergba_rom_crc32(const CIPHERGBA_Handle* h){return h?h->core.cartridge().fingerprint():0;}
uint8_t ciphergba_read8(const CIPHERGBA_Handle* h,uint32_t addr){return h?h->core.read8(addr):0xff;}
uint16_t ciphergba_read16(const CIPHERGBA_Handle* h,uint32_t addr){return h?h->core.read16(addr):0xffff;}
void ciphergba_reset(CIPHERGBA_Handle* h){if(h){h->core.reset();h->error.clear();}}
void ciphergba_set_buttons(CIPHERGBA_Handle* h,uint16_t buttons){
    if(!h)return;
    std::array<bool,10> input{};
    for(unsigned i=0;i<10;++i)input[i]=(buttons&(1u<<i))!=0;
    h->core.setButtons(input);
}
int ciphergba_run_frame(CIPHERGBA_Handle* h){return checked(h,[&]{
    if(!h->core.cartridge().loaded()){h->error="No ROM loaded";return false;}
    return h->core.runFrame();});
}
uint64_t ciphergba_frame_count(const CIPHERGBA_Handle* h){return h?h->core.frames():0;}
const uint32_t* ciphergba_get_framebuffer(const CIPHERGBA_Handle* h){return h?h->core.framebuffer().data():nullptr;}
int ciphergba_flush_save(CIPHERGBA_Handle* h){return checked(h,[&]{return h->core.flushSave(h->error);});}

}
