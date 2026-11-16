#include <cstdio>
#include <stdexcept>
// Created by Jacob Hodgkins.
#include "pixelgba/Core.hpp"
#include "pixelgba/GamePakRefillLedger.hpp"
#include "pixelgba/GamePakCycleTimeline.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <zlib.h>
#ifdef PIXELGBA_FAST_REWIND
# ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
# else
#  include <dlfcn.h>
# endif
#endif
namespace pixelgba {
namespace {
#if !defined(CIPHERGBA_FIFO_DELIVERY_PREFETCH) || CIPHERGBA_FIFO_DELIVERY_PREFETCH
constexpr bool fifoDeliveryPrefetch=true;
#else
constexpr bool fifoDeliveryPrefetch=false;
#endif
#ifdef PIXELGBA_FAST_REWIND
// Resolve liblz4 at startup instead of adding mandatory development/runtime
// dependencies to the Code::Blocks or portable CMake build. If the library is
// absent, the original zlib encoder is used without breaking rewind.
struct Lz4Api {
    using Bound=int (*)(int);
    using Compress=int (*)(const char*,char*,int,int);
    using Decompress=int (*)(const char*,char*,int,int);
    Bound bound=nullptr;
    Compress compress=nullptr;
    Decompress decompress=nullptr;
    Lz4Api(){
        if(const char* disabled=std::getenv("PIXELGBA_DISABLE_LZ4"))
            if(disabled[0]=='1')return;
#ifdef _WIN32
        HMODULE library=LoadLibraryA("liblz4.dll");
        if(!library)return;
        bound=reinterpret_cast<Bound>(GetProcAddress(library,"LZ4_compressBound"));
        compress=reinterpret_cast<Compress>(GetProcAddress(library,"LZ4_compress_default"));
        decompress=reinterpret_cast<Decompress>(GetProcAddress(library,"LZ4_decompress_safe"));
        if(!bound||!compress||!decompress){
            bound=nullptr;compress=nullptr;decompress=nullptr;
            FreeLibrary(library);
        }
#else
        void* library=dlopen("liblz4.so.1",RTLD_LAZY|RTLD_LOCAL);
        if(!library)return;
        bound=reinterpret_cast<Bound>(dlsym(library,"LZ4_compressBound"));
        compress=reinterpret_cast<Compress>(dlsym(library,"LZ4_compress_default"));
        decompress=reinterpret_cast<Decompress>(dlsym(library,"LZ4_decompress_safe"));
        if(!bound||!compress||!decompress){
            bound=nullptr;compress=nullptr;decompress=nullptr;
            dlclose(library);
        }
#endif
        // Successful handle is retained for this process's lifetime.
    }
    bool available()const{return bound&&compress&&decompress;}
};
const Lz4Api& lz4(){static const Lz4Api api;return api;}
#endif
std::uint16_t u16(const std::array<std::uint8_t,1024>& a,unsigned off){return std::uint16_t(a[off]|(unsigned(a[off+1])<<8));}
std::uint32_t u32(const std::array<std::uint8_t,1024>& a,unsigned off){return std::uint32_t(u16(a,off))|(std::uint32_t(u16(a,off+2))<<16);}
void raw16(std::array<std::uint8_t,1024>& a,unsigned off,std::uint16_t v){a[off]=std::uint8_t(v);a[off+1]=std::uint8_t(v>>8);}
// GBA headers are not guaranteed trustworthy. Encode unusual game codes to
// prevent a malformed ROM from choosing a save outside CipherGBA_Data/saves.
std::string cartridgeSaveName(const Cartridge& cart){
    static constexpr char hex[]="0123456789ABCDEF";
    std::string code=cart.gameCode();
    if(code.empty())code="UNKNOWN_"+std::to_string(cart.size());
    std::string safe;
    for(unsigned char c:code){
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')
            safe.push_back(static_cast<char>(c));
        else{safe.push_back('_');safe.push_back(hex[c>>4]);safe.push_back(hex[c&15]);}
    }
    return safe+".sav";
}
}

// A snapshot contains every mutable hardware subsystem, the complete GBA RAM/IO,
// all PPU pixels, EEPROM/Flash protocol state, scheduler, and active BIOS-HLE state.
// Little-endian CPU state is copied by value; only plain byte arrays go through zlib.
// The fixed order is internal-only: rewind states are not disk save states.
bool GbaCore::captureState(State& out) const { return captureStateInternal(out,false); }
bool GbaCore::captureRewindState(State& out) const { return captureStateInternal(out,true); }
bool GbaCore::captureStateInternal(State& out,bool quickRewind) const {
    if(!cart_.loaded())return false;
    State next;
    next.cpu=units_.cpu;next.apu=units_.apu;next.dma=units_.dma;
    next.timers=units_.timers;next.interrupts=units_.interrupts;
    next.save=units_.save;next.scheduler=units_.scheduler;
    next.affine=units_.ppu.affineReference_;
    next.keyInput=keyInput_;next.keyIrqLevel=keyIrqLevel_;
    next.line=line_;next.lineCycles=lineCycles_;next.renderedX=renderedX_;
    next.frames=frames_;next.haltWrites=haltWrites_;
    next.lastSaveFlushFrame=lastSaveFlushFrame_;next.dmaBursts=dmaBursts_;
    next.hleIrqActive=hleIrqActive_;next.hleIrqReturn=hleIrqReturn_;
    next.hleIrqStatus=hleIrqStatus_;next.hleSavedRegisters=hleSavedRegisters_;
    next.hleWaitMask=hleWaitMask_;next.unsupportedHleSwis=unsupportedHleSwis_;
    next.hleIrqDispatches=hleIrqDispatches_;
    next.biosPrefetchLatch=biosPrefetchLatch_;next.biosExecutionActive=biosExecutionActive_;
    next.pipePc=pipePc_;next.pipeCurrent=pipeCurrent_;next.pipeNext=pipeNext_;
    next.pipeThumb=pipeThumb_;next.pipeValid=pipeValid_;
    next.dmaBusLatch=dmaBusLatch_;next.dmaOpenBusValid=dmaOpenBusValid_;next.dmaOpenBusValue=dmaOpenBusValue_;
    next.serialCyclesRemaining=serialCyclesRemaining_;
    next.timer0IrqAssertCycle=timer0IrqAssertCycle_;
    next.timer0IrqAsserted=timer0IrqAsserted_;
    next.dmaPendingCpuCycles=dmaPendingCpuCycles_;next.dmaReadyCycle=dmaReadyCycle_;
    next.hblankDmaPendingMask=hblankDmaPendingMask_;
    next.hleHaltSwiPending=hleHaltSwiPending_;
    next.dmaGamePakClaimPending=dmaGamePakClaimPending_;
    next.gamePakPrefetch=gamePakPrefetch_.state();
    next.conservativeNextFetch=conservativeNextFetch_;
    next.cartridgeDataReturn=cartridgeDataReturn_;
    next.timerStartDelayMask=timerStartDelayMask_;
    next.timerHasTickedMask=timerHasTickedMask_;
    next.physicalOpcodeFetchIssued=physicalOpcodeFetchIssued_;
    next.gamePakDataBusOccupied=gamePakDataBusOccupied_;
    next.cpuPhysicalRomDataRead=cpuPhysicalRomDataRead_;
    next.cpuLastGamePakReadWidth=cpuLastGamePakReadWidth_;
    next.hleFirmwareRomReturnTarget=hleFirmwareRomReturnTarget_;
    next.hleFirmwareRomReturnWidth=hleFirmwareRomReturnWidth_;
    constexpr std::size_t size=256*1024+32*1024+1024+1024+96*1024+1024+Ppu::Width*Ppu::Height*4;
    // A snapshot is taken every frame. Keep a per-thread staging buffer to
    // avoid repeatedly allocating and zero-filling >500 KiB of RAM.
    thread_local std::vector<std::uint8_t> raw(size);
    std::size_t offset=0;
    const auto append=[&](const void* src,std::size_t count){
        std::memcpy(raw.data()+offset,src,count);offset+=count;
    };
    append(ewram_.data(),ewram_.size());append(iwram_.data(),iwram_.size());
    append(io_.data(),io_.size());append(palette_.data(),palette_.size());
    append(vram_.data(),vram_.size());append(oam_.data(),oam_.size());
    append(units_.ppu.framebuffer_.data(),sizeof(units_.ppu.framebuffer_));
#ifdef PIXELGBA_FAST_REWIND
    if(quickRewind && lz4().available()){
        // In-memory-only rewind frame. LZ4 costs much less CPU than zlib at
        // 60 snapshots/second. Disk save states deliberately remain zlib so
        // older slot files retain their existing encoding and ABI.
        constexpr std::size_t header=8;
        next.packedMemory.resize(header+static_cast<std::size_t>(lz4().bound(static_cast<int>(raw.size()))));
        auto* outBytes=next.packedMemory.data();
        std::memcpy(outBytes,"PGL4",4);
        const std::uint32_t checksum=static_cast<std::uint32_t>(crc32(0,raw.data(),static_cast<uInt>(raw.size())));
        for(unsigned i=0;i<4;i++)outBytes[4+i]=static_cast<std::uint8_t>(checksum>>(8*i));
        const int written=lz4().compress(reinterpret_cast<const char*>(raw.data()),
            reinterpret_cast<char*>(outBytes+header),static_cast<int>(raw.size()),
            static_cast<int>(next.packedMemory.size()-header));
        if(written<=0)return false;
        next.packedMemory.resize(header+static_cast<std::size_t>(written));
    }else
#endif
    {
        // Without a runtime LZ4 installation, rewind used to allocate a new
        // deflate stream and its work buffers on every video frame. Use a
        // retained thread-local stream with the standard fast strategy instead. This
        // is still ordinary zlib data, so the existing uncompress() rewind
        // decoder and *all* on-disk state files stay backward compatible.
        // Disk-state capture deliberately keeps the previous compress2 path.
        bool packed=false;
        if(quickRewind){
            struct ReusableDeflater{
                z_stream stream{};
                bool ready=false;
                ReusableDeflater(){ready=deflateInit2(&stream,Z_BEST_SPEED,Z_DEFLATED,
                                                  MAX_WBITS,8,Z_DEFAULT_STRATEGY)==Z_OK;}
                ~ReusableDeflater(){if(ready)deflateEnd(&stream);}
            };
            thread_local ReusableDeflater codec;
            if(codec.ready && raw.size()<=std::numeric_limits<uInt>::max()){
                next.packedMemory.resize(compressBound(static_cast<uLong>(raw.size())));
                z_stream& z=codec.stream;
                if(deflateReset(&z)==Z_OK){
                    z.next_in=raw.data();z.avail_in=static_cast<uInt>(raw.size());
                    z.next_out=next.packedMemory.data();
                    z.avail_out=static_cast<uInt>(next.packedMemory.size());
                    if(deflate(&z,Z_FINISH)==Z_STREAM_END){
                        next.packedMemory.resize(z.total_out);
                        packed=true;
                    }
                }
            }
        }
        if(!packed){
            uLongf compressedSize=compressBound(static_cast<uLong>(raw.size()));
            next.packedMemory.resize(compressedSize);
            if(compress2(next.packedMemory.data(),&compressedSize,raw.data(),
                         static_cast<uLong>(raw.size()),Z_BEST_SPEED)!=Z_OK)return false;
            next.packedMemory.resize(compressedSize);
        }
    }
    out=std::move(next);
    return true;
}
bool GbaCore::restoreState(const State& in){
    if(!cart_.loaded())return false;
    // Validate the external cartridge scheduler before committing any CPU, RAM,
    // timer or DMA state. Rewind and disk snapshots must fail atomically.
    GamePakPrefetch checkedPrefetch;
    if(!checkedPrefetch.restore(in.gamePakPrefetch))return false;
    if((in.timerStartDelayMask|in.timerHasTickedMask)&0xf0u)return false;
    if(in.timerStartDelayMask&in.timerHasTickedMask)return false;
    if(in.dmaGamePakClaimPending>1u || (in.dmaGamePakClaimPending && !in.dmaPendingCpuCycles))return false;
    // A pending HBlank request is possible only during the two-clock
    // 1006..1008 grant window on visible lines, for channels whose latched
    // DMA mode is still HBlank. Reject corrupt rewind states atomically.
    if(in.hblankDmaPendingMask&0xf0u)return false;
    if(in.hblankDmaPendingMask){
        if(in.line>=160u||in.lineCycles<1006u||in.lineCycles>=1008u)return false;
        for(unsigned ch=0;ch<4u;ch++)if(in.hblankDmaPendingMask&(1u<<ch)){
            const auto ctl=in.dma.channels[ch].control;
            if(!(ctl&0x8000u)||((ctl>>12u)&3u)!=2u)return false;
        }
    }
    for(unsigned i=0;i<4;i++)if(in.timerHasTickedMask&(1u<<i)){
        const auto ctrl=in.timers.timers[i].control;
        if((ctrl&0x83u)!=0x80u || (i && (ctrl&4u)))return false;
    }
    for(unsigned i=0;i<4;i++)if(in.timerStartDelayMask&(1u<<i)){
        if((in.timers.timers[i].control&0x83u)!=0x80u)return false;
    }
    constexpr std::size_t size=256*1024+32*1024+1024+1024+96*1024+1024+Ppu::Width*Ppu::Height*4;
    // A snapshot is taken every frame. Keep a per-thread staging buffer to
    // avoid repeatedly allocating and zero-filling >500 KiB of RAM.
    thread_local std::vector<std::uint8_t> raw(size);
    // The two codecs are self-identifying. Existing disk states contain
    // their original zlib payload and continue to load unchanged.
#ifdef PIXELGBA_FAST_REWIND
    if(in.packedMemory.size()>=8 && std::memcmp(in.packedMemory.data(),"PGL4",4)==0){
        if(in.packedMemory.size()-8>static_cast<std::size_t>(std::numeric_limits<int>::max()))return false;
        if(!lz4().available())return false; // Can't decode an LZ4 save-state without the codec.
        const int decoded=lz4().decompress(
            reinterpret_cast<const char*>(in.packedMemory.data()+8),
            reinterpret_cast<char*>(raw.data()),
            static_cast<int>(in.packedMemory.size()-8),static_cast<int>(raw.size()));
        if(decoded!=static_cast<int>(raw.size()))return false;
        std::uint32_t checksum=0;
        for(unsigned i=0;i<4;i++)checksum|=std::uint32_t(in.packedMemory[4+i])<<(8*i);
        if(checksum!=static_cast<std::uint32_t>(crc32(0,raw.data(),static_cast<uInt>(raw.size()))))return false;
    }else
#endif
    {
        uLongf resultSize=raw.size();
        if(uncompress(raw.data(),&resultSize,in.packedMemory.data(),
                      static_cast<uLong>(in.packedMemory.size()))!=Z_OK||resultSize!=size)return false;
    }
    // Commit state only after decompression succeeds, preserving the current
    // running game if a compressed frame is damaged or incomplete.
    units_.cpu=in.cpu;units_.apu=in.apu;units_.apu.invalidateMixCache();units_.dma=in.dma;
    units_.timers=in.timers;units_.interrupts=in.interrupts;
    units_.save=in.save;units_.scheduler=in.scheduler;
    units_.ppu.affineReference_=in.affine;
    std::size_t offset=0;
    const auto extract=[&](void* dest,std::size_t count){
        std::memcpy(dest,raw.data()+offset,count);offset+=count;
    };
    extract(ewram_.data(),ewram_.size());extract(iwram_.data(),iwram_.size());
    extract(io_.data(),io_.size());extract(palette_.data(),palette_.size());
    extract(vram_.data(),vram_.size());extract(oam_.data(),oam_.size());
    extract(units_.ppu.framebuffer_.data(),sizeof(units_.ppu.framebuffer_));
    keyInput_=in.keyInput;keyIrqLevel_=in.keyIrqLevel;
    line_=in.line;lineCycles_=in.lineCycles;renderedX_=in.renderedX;
    frames_=in.frames;haltWrites_=in.haltWrites;
    lastSaveFlushFrame_=in.lastSaveFlushFrame;dmaBursts_=in.dmaBursts;
    hleIrqActive_=in.hleIrqActive;hleIrqReturn_=in.hleIrqReturn;
    hleIrqStatus_=in.hleIrqStatus;hleSavedRegisters_=in.hleSavedRegisters;
    hleWaitMask_=in.hleWaitMask;unsupportedHleSwis_=in.unsupportedHleSwis;
    hleIrqDispatches_=in.hleIrqDispatches;
    biosPrefetchLatch_=in.biosPrefetchLatch;biosExecutionActive_=in.biosExecutionActive;
    pipePc_=in.pipePc;pipeCurrent_=in.pipeCurrent;pipeNext_=in.pipeNext;
    pipeThumb_=in.pipeThumb;pipeValid_=in.pipeValid;
    dmaBusLatch_=in.dmaBusLatch;dmaOpenBusValid_=in.dmaOpenBusValid;dmaOpenBusValue_=in.dmaOpenBusValue;
    serialCyclesRemaining_=in.serialCyclesRemaining;
    timer0IrqAssertCycle_=in.timer0IrqAssertCycle;
    timer0IrqAsserted_=in.timer0IrqAsserted;
    dmaPendingCpuCycles_=in.dmaPendingCpuCycles;dmaReadyCycle_=in.dmaReadyCycle;dmaStepStallCycles_=0;
    hblankDmaPendingMask_=in.hblankDmaPendingMask;
    hleHaltSwiPending_=in.hleHaltSwiPending;
    dmaGamePakClaimPending_=in.dmaGamePakClaimPending;
    // Pending cartridge-data and DMA return phases are part of the timing
    // scheduler: dropping them makes rewind/save-state replay non-deterministic.
    gamePakPrefetch_=checkedPrefetch;
    conservativeNextFetch_=in.conservativeNextFetch;
    cartridgeDataReturn_=in.cartridgeDataReturn;
    timerStartDelayMask_=in.timerStartDelayMask;
    timerHasTickedMask_=in.timerHasTickedMask;
    cpuInstructionTimingActive_=false;cpuInstructionCyclesSpent_=0;cpuEarlyRomFetchClocks_=0;
    return true;
}

GbaCore::GbaCore(){reset();}
GbaCore::~GbaCore(){std::string ignored;flushSave(ignored);}
bool GbaCore::configureSaveDirectory(const std::filesystem::path& path, std::string& err){
    if(path.empty()){err="Cartridge save directory cannot be empty.";return false;}
    if(!savePath_.empty() && !flushSave(err))return false;
    // If a game is already running, validate its save file before changing paths.
    // A failure must never silently discard its in-memory save data.
    std::filesystem::path newSavePath;
    if(cart_.loaded()){
        newSavePath=path/cartridgeSaveName(cart_);
        if(newSavePath!=savePath_){
            std::error_code ec;
            const bool found=std::filesystem::exists(newSavePath,ec);
            if(ec){err="Cannot inspect cartridge save file: "+ec.message();return false;}
            if(found){
                if(units_.save.dirty()) {
                    err="New save directory contains an existing save and the current cartridge has unsaved changes.";
                    return false;
                }
                SaveMemory incoming=units_.save;
                if(incoming.type()==SaveMemory::Type::EEPROM512){
                    const auto size=std::filesystem::file_size(newSavePath,ec);
                    if(ec){err="Cannot inspect EEPROM save: "+ec.message();return false;}
                    if(size==8192)incoming.setEepromAddressBits(14);
                }
                if(!incoming.load(newSavePath,err))return false;
                units_.save=std::move(incoming);
            }
        }
    }
    saveDirectory_=path;
    savePath_=std::move(newSavePath);
    lastSaveError_.clear();err.clear();
    return true;
}
bool GbaCore::flushSave(std::string& err){
    if(!units_.save.dirty()){err.clear();lastSaveError_.clear();return true;}
    if(savePath_.empty()){
        err="No cartridge save directory is configured.";
        lastSaveError_=err;
        return false;
    }
    // Never lose the original 8 KiB file when upgrading an accidentally
    // over-sized 512-byte EEPROM save from older emulator versions.
    if(units_.save.type()==SaveMemory::Type::EEPROM512){
        std::error_code ec;
        if(std::filesystem::exists(savePath_,ec) && !ec &&
           std::filesystem::file_size(savePath_,ec)==8192 && !ec){
            const auto backup=std::filesystem::path(savePath_.string()+".original-8k.bak");
            if(!std::filesystem::exists(backup,ec) && !ec){
                std::filesystem::copy_file(savePath_,backup,std::filesystem::copy_options::none,ec);
                if(ec){err="Could not preserve original EEPROM file before migration: "+ec.message();
                    lastSaveError_=err;return false;}
            }
            if(ec){err="Could not inspect EEPROM backup: "+ec.message();lastSaveError_=err;return false;}
        }
        if(ec){err="Could not inspect EEPROM save for migration: "+ec.message();lastSaveError_=err;return false;}
    }
    if(!units_.save.save(savePath_,err)){
        lastSaveError_="Could not save " + savePath_.string() + ": " + err;
        err=lastSaveError_;
        return false;
    }
    lastSaveError_.clear();err.clear();return true;
}
bool GbaCore::loadCartridge(const std::filesystem::path& path,std::string& err){
    // Stage everything before switching away from the existing game. A broken
    // ROM or mismatched .sav must not discard a valid, already-loaded game.
    Cartridge nextCart;
    if(!nextCart.load(path,err))return false;
    SaveMemory nextSave;
    const auto hasMarker=[&](const char* marker){return nextCart.containsMarker(marker);};
    if(hasMarker("FLASH1M_V"))nextSave.configure(SaveMemory::Type::Flash128);
    else if(hasMarker("FLASH512_V")||hasMarker("FLASH_V"))nextSave.configure(SaveMemory::Type::Flash64);
    else if(hasMarker("EEPROM_V"))nextSave.configure(SaveMemory::Type::EEPROM512);
    // Commit any outgoing save before reading the incoming one. In particular,
    // reloading the *same* ROM must not restore an old on-disk copy and silently
    // discard progress made since the previous autosave.
    if(!flushSave(err))return false;
    std::filesystem::path nextSavePath;
    if(!saveDirectory_.empty()){
        nextSavePath=saveDirectory_/cartridgeSaveName(nextCart);
        std::error_code ec;
        const bool found=std::filesystem::exists(nextSavePath,ec);
        if(ec){err="Cannot inspect save file: "+ec.message();return false;}
        if(found){
            if(nextSave.type()==SaveMemory::Type::EEPROM512){
                const auto size=std::filesystem::file_size(nextSavePath,ec);
                if(ec){err="Cannot inspect EEPROM save: "+ec.message();return false;}
                if(size==8192)nextSave.setEepromAddressBits(14);
            }
            if(!nextSave.load(nextSavePath,err)){
                err="Could not load save " + nextSavePath.string() + ": " + err +
                    ". Existing save was left untouched.";
                return false;
            }
        }
    }
    cart_=std::move(nextCart);
    units_.save=std::move(nextSave);
    savePath_=std::move(nextSavePath);
    lastSaveError_.clear();
    reset();err.clear();return true;
}
bool GbaCore::importBios(const std::filesystem::path& path,const std::filesystem::path& target,std::string& err){if(!bios_.importCopy(path,target,err))return false;reset();return true;}
bool GbaCore::loadInstalledBios(const std::filesystem::path& path,std::string& err){if(!bios_.load(path,err))return false;reset();return true;}
unsigned GbaCore::vramIndex(std::uint32_t addr){unsigned off=addr&0x1ffffu;return off>=0x18000u?0x10000u+(off&0x7fffu):off;}
void GbaCore::unloadBios(){bios_.clear();reset();}
void GbaCore::reset(){
    units_.reset(bios_.loaded());ewram_.fill(0);iwram_.fill(0);io_.fill(0);palette_.fill(0);vram_.fill(0);oam_.fill(0);
    line_=lineCycles_=renderedX_=0;frames_=haltWrites_=lastSaveFlushFrame_=0;dmaBursts_={};dmaBusLatch_={};dmaOpenBusValid_=false;dmaOpenBusValue_=0;physicalOpcodeFetchIssued_=false;serialCyclesRemaining_=0;keyInput_=0x03ff;keyIrqLevel_=false;
    cpuInstructionTimingActive_=false;cpuInstructionCyclesSpent_=0;cpuPhysicalRomDataRead_=false;cpuEarlyRomFetchClocks_=0;
    timerStartDelayMask_=0;timerHasTickedMask_=0;gamePakPrefetch_.reset();gamePakDataBusOccupied_=false;conservativeNextFetch_=false;cartridgeDataReturn_=false;
    timer0IrqAsserted_=false;timer0IrqAssertCycle_=0;
    dmaPendingCpuCycles_=dmaReadyCycle_=0;dmaStepStallCycles_=0;dmaGamePakClaimPending_=0;hblankDmaPendingMask_=0;
    hleIrqActive_=false;hleHaltSwiPending_=false;hleIrqReturn_=hleIrqStatus_=0;
    hleSavedRegisters_={};hleWaitMask_=0;unsupportedHleSwis_=hleIrqDispatches_=0;
    // In BIOS-free mode, HLE boot ends at the same visible protected BIOS
    // prefetch state as firmware startup. This is a bus compatibility value,
    // not a copy of firmware or a ROM-specific test shortcut.
    biosPrefetchLatch_=bios_.loaded()?0u:0xE129F000u;
    biosExecutionActive_=false;
    pipePc_=pipeCurrent_=pipeNext_=0;pipeThumb_=pipeValid_=false;
    raw16(io_,0x130,keyInput_);
    raw16(io_,0x88,0x0200); // SOUNDBIAS
    // On reset the BG affine matrices default to identity.
    raw16(io_,0x20,0x100);raw16(io_,0x26,0x100);
    raw16(io_,0x30,0x100);raw16(io_,0x36,0x100);
    if(!bios_.loaded()){io_[0x300]=1;} // POSTFLG when no BIOS
    updateDisplayStatus();
}
void GbaCore::setButtons(const std::array<bool,10>& pressed){
    keyInput_=0x3ff;
    for(unsigned i=0;i<10;i++)if(pressed[i])keyInput_&=std::uint16_t(~(1u<<i));
    raw16(io_,0x130,keyInput_);
    const std::uint16_t keycnt=u16(io_,0x132),mask=keycnt&0x3ffu;
    const bool match=(keycnt&0x8000u)&&mask&&((keycnt&0x4000u)?((~keyInput_&mask)==mask):((~keyInput_&mask)!=0));
    // Evaluate the keypad IRQ level and its transition independently of the
    // IME mask. KEYCNT changes are handled by the same helper below.
    if(match && !keyIrqLevel_)units_.interrupts.request(1u<<12);
    keyIrqLevel_=match;
}
std::uint32_t GbaCore::fetchCpuOpcode(std::uint32_t pc,bool thumb){
    // An internal-bus instruction fetch displaces a previous DMA transfer's
    // open-bus word. Game Pak fetches use the external bus and do not do so.
    const unsigned region=pc>>24u;
    if(region<=7u && region!=1u)dmaOpenBusValid_=false;
    // The ARM7TDMI fetches two opcodes ahead of the executing instruction.
    // Keep those prefetched values until the PC or instruction state branches.
    // Crucial for executing self-modifying code from IWRAM/VRAM: overwriting
    // the next two instruction words must not change the queued opcodes.
    const unsigned size=thumb?2u:4u;
    const auto fetch=[&](std::uint32_t address)->std::uint32_t{
        // Opcode fetches are side-effect-free on ordinary internal RAM and
        // cartridge ROM. Bypass the *CPU data read* decoder for those regions
        // while retaining the exact prefetched bytes and bus timing, which
        // are already handled by cpuInstructionFetchCycles. The unusual BIOS,
        // I/O, EEPROM and unmapped fetch paths still use the normal decoder.
        const unsigned region=address>>24u;
        if(thumb){
            address&=~1u;
            if(region==3u){
                const auto* b=iwram_.data()+(address&0x7fffu);
                return unsigned(b[0])|(unsigned(b[1])<<8u);
            }
            if(region==2u){
                const auto* b=ewram_.data()+(address&0x3ffffu);
                return unsigned(b[0])|(unsigned(b[1])<<8u);
            }
            if(region>=8u&&region<=12u)return cart_.read16(address&0x1ffffffu);
            return read16(address);
        }
        address&=~3u;
        if(region==3u||region==2u){
            const auto* b=(region==3u?iwram_.data():ewram_.data())+
                (region==3u?(address&0x7fffu):(address&0x3ffffu));
            return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8u)|
                (std::uint32_t(b[2])<<16u)|(std::uint32_t(b[3])<<24u);
        }
        if(region>=8u&&region<=12u)return cart_.read32(address&0x1ffffffu);
        return read32(address);
    };
    const bool sequential=pipeValid_ && pipePc_==pc && pipeThumb_==thumb;
    const std::uint32_t current=sequential?pipeCurrent_:fetch(pc);
    const std::uint32_t next=sequential?pipeNext_:fetch(pc+size);
    pipeCurrent_=next;
    pipeNext_=fetch(pc+size*2u);
    pipePc_=pc+size;
    pipeThumb_=thumb;pipeValid_=true;
    return current;
}
void GbaCore::observeInstructionFetch(std::uint32_t pc,bool thumb){
    biosExecutionActive_=bios_.loaded() && pc<Bios::Size;
    if(!biosExecutionActive_)return;
 // BIOS read-protection latch holds its last value.
    const std::uint32_t ahead=pc+(thumb?4u:8u);
    if(ahead>=Bios::Size)return;
    if(thumb){
        // BIOS Thumb pipeline supplies two upcoming halfwords, not an ARM word.
        const auto lo=std::uint32_t(bios_.read(ahead)) |
                      (std::uint32_t(bios_.read(ahead+1u))<<8u);
        const auto hi=std::uint32_t(bios_.read(ahead+2u)) |
                      (std::uint32_t(bios_.read(ahead+3u))<<8u);
        biosPrefetchLatch_=lo|(hi<<16u);
    }else{
        biosPrefetchLatch_=0;
        for(unsigned byte=0;byte<4;byte++)
            biosPrefetchLatch_|=std::uint32_t(bios_.read(ahead+byte))<<(8u*byte);
    }
}
// ARM7TDMI I/O readback is not a byte array: many video/DMA addresses are
// write-only, and unused 32-bit I/O slots return the current prefetch bus.
// The offset is a *halfword* offset within 0x04000000.
bool GbaCore::ioWriteOnly(unsigned o) const {
    if(o>=0x10u&&o<=0x46u)return true; // BG scroll/affine/window coordinates
    if(o==0x4cu||o==0x54u)return true; // MOSAIC and BLDY
    if(o>=0xa0u&&o<=0xb6u)return true; // audio FIFO / DMA addresses
    if(o>=0xbcu&&o<=0xc2u)return true;
    if(o>=0xc8u&&o<=0xceu)return true;
    if(o>=0xd4u&&o<=0xdau)return true;
    if((o>=0xe0u&&o<=0xfeu)||(o>=0x8cu&&o<=0x8eu))return true;
    if((o>=0x56u&&o<=0x5eu)||(o>=0xa8u&&o<=0xaeu))return true;
    if(o==0x4eu)return true; // both halves of MOSAIC/unused word are unreadable
    return o==0x100cu; // outside documented I/O map
}
bool GbaCore::ioZeroRead(unsigned o) const {
    if(o==0x66u||o==0x6au||o==0x6eu||o==0x76u||o==0x7au||o==0x7eu||
       o==0x86u||o==0x8au||o==0x136u||o==0x142u||o==0x15au||
       o==0x206u||o==0x20au||o==0x302u)return true;
    return o==0xb8u||o==0xc4u||o==0xd0u||o==0xdcu; // DMA word counts are write-only, read as zero
}
bool GbaCore::ioOpenBus(unsigned o) const {
    return ioWriteOnly(o);
}
std::uint16_t GbaCore::readIo16(unsigned o) const {
    if(ioZeroRead(o))return 0;
    if(ioOpenBus(o)){
        // This is the ARM7TDMI's prefetched instruction bus, not the last
        // software value written to a write-only display/audio register.
        // The CPU fetch phase keeps pipeNext_ at PC+8 (ARM) / PC+4 (Thumb).
        const std::uint32_t bus=pipeValid_?(pipeThumb_?(pipeNext_*0x00010001u):pipeNext_):0;
        return static_cast<std::uint16_t>((bus >> ((o&2u)*8u))&0xffffu);
    }
    if(o>=io_.size())return static_cast<std::uint16_t>((pipeValid_?pipeNext_:0u)>>((o&2u)*8u));
    // Diagnostic-only observation of the control halfword in a 32-bit
    // timer poll. Keeping the counter and control latches distinct in the
    // trace exposes whether the CPU observed the stop edge before or after
    // IRQ return. This does not advance hardware or change readback values.
    

    if(o==0x84u)return std::uint16_t((io_[0x84u]&0x80u)|units_.apu.status());
    if(o==0x200u)return units_.interrupts.interruptEnable;
    if(o==0x202u)return units_.interrupts.interruptFlags;
    if(o==0x208u)return units_.interrupts.masterEnable?1u:0u;
    if(o>=0x100u&&o<=0x10eu&&(o&3u)==0u){
        const unsigned ti=(o-0x100u)/4u;
        // The CPU samples timer data at the preceding I/O bus-latch edge.
        // /1 counters can have ticked during the aggregate opcode-fetch phase;
        // the read needs the counter from one peripheral clock earlier.
        // A just-enabled timer has not necessarily ticked yet, even after its
        // enable-latch clock was consumed. Track that distinction separately.
        const auto& timer=units_.timers.timers[ti];
        std::uint16_t count=timer.counter;
        if(cpuInstructionTimingActive_ && (timerHasTickedMask_&(1u<<ti)) &&
           (timer.control&0x83u)==0x80u && !(ti && (timer.control&4u))){
            count=std::uint16_t(timer.counter==timer.reload ? 0xffffu : timer.counter-1u);
        }
        

        return count;
    }
    if(o>=0x90u&&o<=0x9eu){
        return std::uint16_t(units_.apu.readWave(o,io_))|
               (std::uint16_t(units_.apu.readWave(o+1u,io_))<<8u);
    }
    const std::uint16_t value=u16(io_,o);
    // GBA serial-port registers have mode-dependent readback. Without a
    // connected peripheral, the receive latches remain zero; transmission
    // data is readable only in the serial formats exposing that register.
    const std::uint16_t serialMode=u16(io_,0x128u)&0x3000u;
    const std::uint16_t serialRcnt=u16(io_,0x134u)&0xc000u;
    if(o>=0x120u&&o<=0x126u){
        if(serialRcnt || o>=0x124u || serialMode!=0x1000u)return 0;
        return value;
    }
    if(o==0x128u)return value&((serialMode==0x3000u)?0x7fafu:0x7f8fu);
    if(o==0x12au)return serialMode==0x3000u?0:value;
    if(o==0x134u){
        if(serialRcnt==0xc000u)return (value&0xc000u)|0x01fcu;
        if(serialRcnt==0x8000u)return (value&0x8000u)|0x01ffu;
        return (serialMode==0x0000u||serialMode==0x1000u)?
            (value&0x01f5u):(value&0x01ffu);
    }
    if(o==0x140u)return value&0x0040u;
    if(o==0x150u||o==0x152u||o==0x154u||o==0x156u||o==0x15au)return 0;
    // The hardware masks unused and write-only bits on readback. This is
    // distinct from the stored raw bits used by video/audio implementation.
    switch(o){
        case 0x08u:case 0x0au:return value&0xdfffu;
        case 0x48u:case 0x4au:return value&0x3f3fu;
        case 0x50u:return value&0x3fffu;
        case 0x52u:return value&0x1f1fu;
        case 0x60u:return value&0x007fu;
        case 0x62u:case 0x68u:return value&0xffc0u;
        case 0x64u:case 0x6cu:case 0x74u:return value&0x4000u;
        case 0x70u:return value&0x00e0u;
        case 0x72u:return value&0xe000u;
        case 0x78u:return value&0xff00u;
        case 0x7cu:return value&0x40ffu;
        case 0x80u:return value&0xff77u;
        case 0x82u:return value&0x770fu;
        case 0xbau:case 0xc6u:case 0xd2u:return value&0xf7e0u;
        case 0xdeu:return value&0xffe0u;
        default:return value;
    }
}
std::uint8_t GbaCore::read8(std::uint32_t addr) const {
    switch(addr>>24){
        case 0:
            if(addr>=Bios::Size){
                const std::uint32_t bus=pipeValid_?(pipeThumb_?
                    (pipeNext_*0x00010001u):pipeNext_):0u;
                return std::uint8_t(bus>>(8u*(addr&3u)));
            }
            // Real GBA firmware is inaccessible when the CPU executes elsewhere.
            // Read byte lanes from the sticky *prefetched instruction*, not the
            // requested BIOS address. Do not store original firmware in states.
            return (bios_.loaded() && biosExecutionActive_)?bios_.read(addr):
                std::uint8_t(biosPrefetchLatch_>>(8u*(addr&3u)));
        case 2:return ewram_[addr&0x3ffffu];
        case 3:return iwram_[addr&0x7fffu];
        case 4:{const unsigned o=addr&0xffffffu;
            return static_cast<std::uint8_t>(readIo16(o&~1u)>>(8u*(o&1u)));
        }
        case 5:return palette_[addr&0x3ffu];
        case 6:return vram_[vramIndex(addr)];
        case 7:return oam_[addr&0x3ffu];
        case 8:case 9:case 10:case 11:case 12:return cart_.read(addr&0x1ffffffu);
        case 13:return units_.save.type()==SaveMemory::Type::EEPROM512||units_.save.type()==SaveMemory::Type::EEPROM8K ? 1 : cart_.read(addr&0x1ffffffu);
        case 14:case 15:return units_.save.read(addr);
        default:{
            // Empty/unmapped address space exposes the CPU prefetch data bus.
            // Thumb instructions appear twice on the 32-bit external bus.
            const std::uint32_t bus=dmaOpenBusValid_?dmaOpenBusValue_: (pipeValid_?(pipeThumb_?
                (pipeNext_*0x00010001u):pipeNext_):0u);
            return std::uint8_t(bus>>(8u*(addr&3u)));
        }
    }
}
// Fast little-endian read paths keep CPU instruction fetches out of the
// bytewise bus decoder. The I/O path remains bytewise: register reads have
// side effects and may have different values on adjacent byte accesses.
std::uint16_t GbaCore::read16(std::uint32_t a) const {
    const unsigned region=a>>24u;
    if(region==4u)return readIo16((a&0xffffffu)&~1u);
    // Cartridge backup is an 8-bit bus mapped at both 0x0E and 0x0F.
    // Every byte lane of 16-/32-bit CPU reads samples the same SRAM/Flash byte.
    if(region==14u||region==15u){
        const unsigned v=units_.save.read(a);
        return std::uint16_t(v*0x0101u);
    }
    if(region==13u && (units_.save.type()==SaveMemory::Type::EEPROM512 ||
                       units_.save.type()==SaveMemory::Type::EEPROM8K))
        return const_cast<SaveMemory&>(units_.save).readEeprom();
    if((a&0xffffffu)!=0xffffffu){
        switch(region){
            case 2:{const unsigned i=a&0x3ffffu;
                if(i<0x3ffffu)return std::uint16_t(unsigned(ewram_[i])|(unsigned(ewram_[i+1])<<8u));
                break;}
            case 3:{const unsigned i=a&0x7fffu;
                if(i<0x7fffu)return std::uint16_t(unsigned(iwram_[i])|(unsigned(iwram_[i+1])<<8u));
                break;}
            case 5:{const unsigned i=a&0x3ffu;
                if(i<0x3ffu)return std::uint16_t(unsigned(palette_[i])|(unsigned(palette_[i+1])<<8u));
                break;}
            case 6:{const unsigned i=vramIndex(a);
                if(i+1<vram_.size() && vramIndex(a+1)==i+1)
                    return std::uint16_t(unsigned(vram_[i])|(unsigned(vram_[i+1])<<8u));
                break;}
            case 7:{const unsigned i=a&0x3ffu;
                if(i<0x3ffu)return std::uint16_t(unsigned(oam_[i])|(unsigned(oam_[i+1])<<8u));
                break;}
            case 8:case 9:case 10:case 11:case 12:case 13:
                return cart_.read16(a&0x1ffffffu);
            default:break;
        }
    }
    return std::uint16_t(unsigned(read8(a))|(unsigned(read8(a+1u))<<8u));
}
std::uint32_t GbaCore::read32(std::uint32_t a) const {
    const unsigned region=a>>24u;
    if(region==14u||region==15u){
        const std::uint32_t v=units_.save.read(a);
        return v*0x01010101u;
    }
    if((a&0xffffffu)<=0xfffffcu){
        switch(region){
            case 2:{const unsigned i=a&0x3ffffu;
                if(i<=0x3fffcu){const auto* b=ewram_.data()+i;
                    return unsigned(b[0])|(unsigned(b[1])<<8u)|(unsigned(b[2])<<16u)|(unsigned(b[3])<<24u);}break;}
            case 3:{const unsigned i=a&0x7fffu;
                if(i<=0x7ffcu){const auto* b=iwram_.data()+i;
                    return unsigned(b[0])|(unsigned(b[1])<<8u)|(unsigned(b[2])<<16u)|(unsigned(b[3])<<24u);}break;}
            case 5:{const unsigned i=a&0x3ffu;
                if(i<=0x3fcu){const auto* b=palette_.data()+i;
                    return unsigned(b[0])|(unsigned(b[1])<<8u)|(unsigned(b[2])<<16u)|(unsigned(b[3])<<24u);}break;}
            case 6:{const unsigned i=vramIndex(a);
                if(i+3<vram_.size() && vramIndex(a+3)==i+3){const auto* b=vram_.data()+i;
                    return unsigned(b[0])|(unsigned(b[1])<<8u)|(unsigned(b[2])<<16u)|(unsigned(b[3])<<24u);}break;}
            case 7:{const unsigned i=a&0x3ffu;
                if(i<=0x3fcu){const auto* b=oam_.data()+i;
                    return unsigned(b[0])|(unsigned(b[1])<<8u)|(unsigned(b[2])<<16u)|(unsigned(b[3])<<24u);}break;}
            case 8:case 9:case 10:case 11:case 12:
                return cart_.read32(a&0x1ffffffu);
            case 13:
                if(units_.save.type()!=SaveMemory::Type::EEPROM512 &&
                   units_.save.type()!=SaveMemory::Type::EEPROM8K)
                    return cart_.read32(a&0x1ffffffu);
                break;
            default:break;
        }
    }
    return std::uint32_t(read16(a))|(std::uint32_t(read16(a+2u))<<16u);
}
void GbaCore::flushVisiblePixels(unsigned targetX){
    if(line_>=160u)return;
    targetX=std::min(targetX,Width);
    if(targetX<=renderedX_)return;
    units_.ppu.renderSpan(line_,renderedX_,targetX,io_,palette_,vram_,oam_);
    renderedX_=targetX;
}
void GbaCore::write8(std::uint32_t addr,std::uint8_t value){
    switch(addr>>24){
        case 2:ewram_[addr&0x3ffffu]=value;return;
        case 3:iwram_[addr&0x7fffu]=value;return;
        case 4:{
            const unsigned o=addr&0xffffffu;
            if(o>=io_.size())return;
            if(o<=0x55u && lineCycles_<960u)flushVisiblePixels(lineCycles_/4u);
            if((o&0x3feu)==0x130u||o==6||o==7)return;
            if(o==0x202||o==0x203){units_.interrupts.interruptFlags&=std::uint16_t(~(unsigned(value)<<(8*(o-0x202))));return;}
            if(o==0x200||o==0x201){unsigned shift=8*(o-0x200);units_.interrupts.interruptEnable=std::uint16_t((units_.interrupts.interruptEnable&~(0xffu<<shift))|(unsigned(value)<<shift));}
            if(o==0x208)units_.interrupts.masterEnable=(value&1)!=0;
            


            if(o==0x301){
                ++haltWrites_;
                

                if(value&0x80u){units_.cpu.halted=true;} // STOP low-power approximation
                else units_.cpu.halted=true;
                return;
            }
            for(unsigned i=0;i<4;i++){
                auto& t=units_.timers.timers[i];
                unsigned lo=0x100+4*i;
                if(o==lo||o==lo+1){
                    const unsigned shift=(o-lo)*8;
                    t.reload=std::uint16_t((t.reload&~(0xffu<<shift))|(unsigned(value)<<shift));
                }
                if(o==lo+2){const bool wasRunning=t.control&0x80u;
                    if(!(value&0x80u)){
                        timerStartDelayMask_&=std::uint8_t(~(1u<<i));
                        timerHasTickedMask_&=std::uint8_t(~(1u<<i));
                    }
                    t.control=value;
                    if((value&0x80u)&&!wasRunning){
                        timerHasTickedMask_&=std::uint8_t(~(1u<<i));
                        t.counter=t.reload;
                        // Only an unbuffered ARM cartridge fetch covers the
                        // /1 timer's enable-latch clock in the aggregate model.
                        // Thumb delivery and FIFO-served ARM delivery retain
                        // the peripheral start edge; the old ROM-only test
                        // incorrectly suppressed it in both situations.
                        const bool romExecution=cpuInstructionTimingActive_ &&
                            (units_.cpu.r[15]>>24u)>=8u &&
                            (units_.cpu.r[15]>>24u)<=13u;
#if !defined(CIPHERGBA_TIMER_ENABLE_FETCH_OVERLAP) || CIPHERGBA_TIMER_ENABLE_FETCH_OVERLAP
                        const bool enableClockCovered=romExecution &&
                            !units_.cpu.thumb() && !gamePakPrefetchEnabled();
#else
                        const bool enableClockCovered=romExecution;
#endif
                        if((value&3u)==0u && !(i && (value&4u)) && !enableClockCovered)
                            timerStartDelayMask_|=std::uint8_t(1u<<i);
                        // Timer0 prescaled taps reference the shared clock.
                        // A CPU-issued control write is sampled at a different
                        // bus phase than a direct external/core write. Preserve
                        // the direct-register /64 tap regression (-4), while
                        // charging one later CPU data-latch phase (-3) to guest
                        // writes. /256 and /1024 keep the global-clock tap.
                        // This empirical CPU-latch alignment needs physical
                        // hardware verification before wider generalization.
                        const unsigned divider=(value&3u)==1u?64u:(value&3u)==2u?256u:1024u;
                        t.phase=(i==0u && (value&3u)>=1u)?
                            unsigned((units_.scheduler.cycles()+((value&3u)==1u?divider-(cpuInstructionTimingActive_?0u:4u):0u))%divider):0u;
                        // Passive witness of the exact modeled enable tap.
                        // The sample clock is absolute; no per-test phase shift.
                        

                    }
                }
                

            }
            if(o==4)value=std::uint8_t((value&0x38u)|(io_[4]&7u)); // DISPSTAT bit 7 reserved
            if(o==0x84){
                value&=0x80u; // PSG status bits are hardware read-only.
                if(!(value&0x80u)){
                    // Master-off clears the PSG register bank, not Wave RAM,
                    // SOUNDCNT_H, or SOUNDBIAS. Software may rewrite these
                    // control registers after re-enabling the mixer.
                    std::fill(io_.begin()+0x60,io_.begin()+0x82,0);
                }
            }
            // PSG register bank is not writable when the sound master is off.
            // FIFO/DMA control and SOUNDBIAS are independent exceptions.
            if(o>=0x60&&o<=0x81 && !(io_[0x84]&0x80u))return;
            // Several sound controls contain write-only trigger/reset bits.
            // Never echo those bits on CPU reads: read/modify/write code must
            // not accidentally re-trigger notes, especially during music.
            std::uint8_t stored=value;
            switch(o){
                case 0x60: stored&=0x7f;break;
                case 0x65:case 0x6d:case 0x75:stored&=0x47;break;
                case 0x7d:stored&=0x40;break;
                case 0x70:case 0x73:stored&=0xe0;break;
                case 0x80:stored&=0x77;break;
                case 0x82:stored&=0x0f;break;
                case 0x83:stored&=0x77;break;
                case 0x89:stored&=0xc3;break;
                default:break;
            }
            const std::uint16_t previousSioControl=(o==0x128u||o==0x129u)?u16(io_,0x128u):0u;
            io_[o]=stored;
            if(o==0x128u||o==0x129u){
                const std::uint16_t control=u16(io_,0x128u);
                const std::uint16_t mode=control&0x3000u;
                // In normal serial mode the console is the clock master only
                // if SIOCNT bit 0 selects the internal serial clock. Normal
                // 256 kHz: 64 system clocks/bit; 2 MHz: 8 clocks/bit.
                // Multi-player/external-clock transfers need a link peer and
                // must not complete spontaneously.
                if(!(control&0x80u))serialCyclesRemaining_=0;
                else if(!(previousSioControl&0x80u) && (control&1u) &&
                        (mode==0u || mode==0x1000u) && !(u16(io_,0x134u)&0x8000u)){
                    const unsigned bits=(control&0x1000u)?32u:8u;
                    // Direct writes commit at the exposed bus boundary; CPU
                    // writes still owe their on-bus START synchronization.
                    // Preserve the hardware-clock test's 1-cycle START latch.
                    serialCyclesRemaining_=(cpuInstructionTimingActive_?2u:1u)+bits*((control&2u)?8u:64u);
                }
            }
            if(o==4||o==5)updateDisplayStatus(); // VCount match may change mid-line
            if(o==0x132||o==0x133){
                const unsigned keycnt=u16(io_,0x132),mask=keycnt&0x03ffu;
                const bool matched=(keycnt&0x8000u)&&mask&&
                    ((keycnt&0x4000u)?((~keyInput_&mask)==mask):((~keyInput_&mask)!=0));
                if(matched&&!keyIrqLevel_)units_.interrupts.request(1u<<12);
                keyIrqLevel_=matched;
            }
            if(o>=0x28&&o<=0x3f)units_.ppu.affineRegisterWritten(o,io_);
            if(o>=0x60&&o<=0xa7)units_.apu.write(o,value,io_);
            if(o==0x83)io_[o]&=std::uint8_t(~0x88u); // FIFO reset are write-only pulse bits
            for(unsigned i=0;i<4;i++)if(o==0xbb+12*i){
                auto& d=units_.dma.channels[i];
                const std::uint16_t next=u16(io_,0xba+12*i);
                const bool wasEnabled=(d.control&0x8000u)!=0;
                d.control=next;
                // A channel disabled or switched away from HBlank during the
                // 1006..1008 grant window cannot consume a stale HBlank grant.
                // Enabling a channel after 1006 does not retroactively request DMA.
                if(!(next&0x8000u)||((next>>12)&3u)!=2u)
                    hblankDmaPendingMask_ &= std::uint8_t(~(1u<<i));
                // Hardware latches SAD/DAD/count at DMA enable's 0->1 edge.
                // Writes while enabled affect control but not active pointers.
                if((next&0x8000u)&&!wasEnabled){
                    d.source=u32(io_,0xb0+12*i)&(i?0x0fffffffu:0x07ffffffu);
                    d.destination=u32(io_,0xb4+12*i)&(i==3?0x0fffffffu:0x07ffffffu);
                    d.initialDestination=d.destination;
                    d.count=u16(io_,0xb8+12*i);
                    if(!((next>>12)&3u))runDma(i,0);
                }
            }
            return;
        }
        case 5:{if(lineCycles_<960u)flushVisiblePixels(lineCycles_/4u);unsigned off=addr&0x3feu;palette_[off]=value;palette_[off+1]=value;return;}
        case 6:{
            if(lineCycles_<960u)flushVisiblePixels(lineCycles_/4u);
            const unsigned off=vramIndex(addr&~1u);
            // Byte stores are ignored in OBJ VRAM (the 16 KiB OBJ region
            // begins at different addresses in tile and bitmap video modes).
            const unsigned spriteBase=((u16(io_,0)&7u)>=3)?0x14000u:0x10000u;
            if(off>=spriteBase)return;
            vram_[off]=value;vram_[off+1]=value;return;
        }
        case 7:return; // GBA OAM ignores byte writes.
        case 14:case 15:units_.save.write(addr,value);return;
        default:return;
    }
}
void GbaCore::write16(std::uint32_t a,std::uint16_t v){
    // The 8-bit backup bus only accepts the byte in the addressed lane.
    // CPU STRH must pass its original address to preserve odd byte lanes.
    if((a>>24u)==14u||(a>>24u)==15u){
        units_.save.write(a,std::uint8_t(v>>(8u*(a&1u))));return;
    }
    a&=~1u;
    // CPU and DMA commonly write halfwords to internal RAM. Avoid two
    // complete bus decodes for each ordinary aligned RAM store. Mirrored
    // addresses still use the same low-address masks as the byte decoder.
    const unsigned region=a>>24u;
    if(region==2u){
        const unsigned i=a&0x3ffffu;
        ewram_[i]=std::uint8_t(v);ewram_[i+1u]=std::uint8_t(v>>8u);
        return;
    }
    if(region==3u){
        const unsigned i=a&0x7fffu;
        iwram_[i]=std::uint8_t(v);iwram_[i+1u]=std::uint8_t(v>>8u);
        return;
    }
    if((a>>24)==13 && (units_.save.type()==SaveMemory::Type::EEPROM512||units_.save.type()==SaveMemory::Type::EEPROM8K)){
        units_.save.writeEeprom(v);return;
    }
    if(((a>>24)==5u||(a>>24)==6u||(a>>24)==7u) && lineCycles_<960u)
        flushVisiblePixels(lineCycles_/4u);
    if((a>>24)==5){unsigned off=a&0x3feu;palette_[off]=std::uint8_t(v);palette_[off+1]=std::uint8_t(v>>8);return;}
    if((a>>24)==6){unsigned off=vramIndex(a);vram_[off]=std::uint8_t(v);vram_[off+1]=std::uint8_t(v>>8);return;}
    if((a>>24)==7){unsigned off=a&0x3feu;oam_[off]=std::uint8_t(v);oam_[off+1]=std::uint8_t(v>>8);return;}
    write8(a,std::uint8_t(v));write8(a+1,std::uint8_t(v>>8));
}
void GbaCore::write32(std::uint32_t a,std::uint32_t v){
    // The overwhelming majority of GBA STR/STM stores target aligned
    // EWRAM/IWRAM. Handle both before entering the video/I/O/backup bus
    // decoder. memcpy of a fixed four-byte POD is defined for unaligned
    // host pointers; on little-endian targets it compiles to one store.
    const unsigned region=a>>24u;
    if((a&3u)==0u){
        std::uint8_t* b=nullptr;
        if(region==2u)b=ewram_.data()+(a&0x3ffffu);
        else if(region==3u)b=iwram_.data()+(a&0x7fffu);
        if(b){
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            std::memcpy(b,&v,sizeof(v));
#else
            b[0]=std::uint8_t(v);b[1]=std::uint8_t(v>>8u);
            b[2]=std::uint8_t(v>>16u);b[3]=std::uint8_t(v>>24u);
#endif
            return;
        }
    }
    if(region==14u||region==15u){
        units_.save.write(a,std::uint8_t(v>>(8u*(a&3u))));return;
    }
    if((a&3u)==0u){
        std::uint8_t* b=nullptr;
        if(region==5u){
            if(lineCycles_<960u)flushVisiblePixels(lineCycles_/4u);
            b=palette_.data()+(a&0x3fcu);
        }else if(region==6u){
            if(lineCycles_<960u)flushVisiblePixels(lineCycles_/4u);
            b=vram_.data()+vramIndex(a);
        }else if(region==7u){
            if(lineCycles_<960u)flushVisiblePixels(lineCycles_/4u);
            b=oam_.data()+(a&0x3fcu);
        }
        if(b){
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            std::memcpy(b,&v,sizeof(v));
#else
            b[0]=std::uint8_t(v);b[1]=std::uint8_t(v>>8u);
            b[2]=std::uint8_t(v>>16u);b[3]=std::uint8_t(v>>24u);
#endif
            return;
        }
    }
    write16(a,std::uint16_t(v));write16(a+2u,std::uint16_t(v>>16u));
}

void GbaCore::runDma(unsigned ch,unsigned timing){
    auto& d=units_.dma.channels[ch];
    if(!(d.control&0x8000u)||((d.control>>12)&3u)!=timing)return;
    std::uint32_t src=d.source,dst=d.destination;
    if(GamePakPrefetch::isRom(src) || GamePakPrefetch::isRom(dst)){
        // An already buffered or soon-ready instruction may reach decode
        // before the immediate-DMA arbitration edge. Preserve that stream
        // until actual ownership transfers; misses use the conservative path.
#if !defined(CIPHERGBA_DEFERRED_DMA_FIFO_CLAIM) || CIPHERGBA_DEFERRED_DMA_FIFO_CLAIM
        const unsigned ready=gamePakPrefetch_.clocksUntilAvailable(
            units_.cpu.thumb()?1u:2u,u16(io_,0x204u));
        const bool deferClaim=timing==0u && gamePakPrefetchEnabled() && ready<3u;
#else
        constexpr bool deferClaim=false;
#endif
        if(deferClaim){
            dmaGamePakClaimPending_=true;
        }else{
            gamePakPrefetch_.externalBusAccess();
            gamePakDataBusOccupied_=true;
            conservativeNextFetch_=true;
        }
    }
    

    if(timing==3){ // DMA1/2: fixed destination, 4 x 32-bit Sound FIFO words per request
        if(ch!=1&&ch!=2)return;
        if((dst&~3u)!=0x040000a0u && (dst&~3u)!=0x040000a4u)return;
        const unsigned srcMode=(d.control>>7)&3;
        src&=~3u;
        

        

        for(unsigned i=0;i<4;i++){

            const std::uint32_t value=(src>>24u)==0u && !biosExecutionActive_
                ?dmaBusLatch_[ch]:read32(src);
            if(!((src>>24u)==0u && !biosExecutionActive_))dmaBusLatch_[ch]=value;

            write32(dst,value);
            if(srcMode==0)src+=4;else if(srcMode==1)src-=4;
        }
        d.source=src;
        

        if(d.control&0x4000)units_.interrupts.request(std::uint16_t(1u<<(8+ch)));
        // Sound FIFO DMA remains armed until DMA is disabled.
        ++dmaBursts_[ch];return;
    }
    const bool word=d.control&0x400u;
    const unsigned bytes=word?4:2;
    unsigned count=d.count?d.count:(ch==3?0x10000u:0x4000u);
    std::uint64_t dmaTransferCycles=0;
    // A DMA burst claims the cartridge bus as one stream: the *first*
    // Game Pak transaction is nonsequential, not independently the first
    // source and first destination. A 128 KiB boundary still forces N.
    bool dmaStreamHadGamePakAccess=false;
    

    // Infer EEPROM address width ONLY from a command streamed *to* EEPROM.
    // A DMA3 read *from* EEPROM is normally 68 halfwords (4 dummy + 64 data);
    // the previous code mistook this readback for a 14-bit command, silently
    // promoting 512-byte saves (including Super Mario Advance) to 8 KiB.
    // Other counts (including 68) must never mutate the save geometry.
    if(ch==3 && !word && (dst>>24)==13 &&
       (units_.save.type()==SaveMemory::Type::EEPROM512||units_.save.type()==SaveMemory::Type::EEPROM8K)){
        if(count==9 || count==73)units_.save.setEepromAddressBits(6);
        else if(count==17 || count==81)units_.save.setEepromAddressBits(14);
    }
    const unsigned srcMode=(d.control>>7)&3,dstMode=(d.control>>5)&3;
    src&=~(bytes-1);dst&=~(bytes-1);
#ifdef CIPHERGBA_DMA_PROFILE
    struct DmaCounter {
        std::uint64_t totals[16][16]{};
        std::uint64_t bursts[16][16]{};
        ~DmaCounter(){for(unsigned x=0;x<16;x++)for(unsigned y=0;y<16;y++)if(totals[x][y]) std::fprintf(stderr,"[dma] src=%u dst=%u transfers=%llu bursts=%llu\n",x,y,(unsigned long long)totals[x][y],(unsigned long long)bursts[x][y]);}
    };
    static DmaCounter counters;
    if((src>>24)<16 && (dst>>24)<16){counters.totals[src>>24][dst>>24]+=count;counters.bursts[src>>24][dst>>24]++;}
#endif
    // The Game Pak cartridge read bus is an auto-incrementing stream. For
    // DMA1-3, a fixed/decrementing source address does not rewind the ROM
    // data stream: the cartridge itself advances to the next halfword/word.
    // DMA0 has no usable Game Pak source bus, so retain its prior behavior.
    std::uint32_t gamePakReadAddress=src;
    // A whole internal-RAM -> video-RAM DMA burst can be copied directly
    // when the destination is not currently raster-visible, both addresses
    // stay inside their physical banks, and both DMA strides increment.
    // Each read/write still occupies its original bus clocks; the final
    // transfer drives the same DMA/open-bus latch. All ROM, IO, mirroring,
    // raster-active, fixed/decrement and cross-bank transfers use the
    // unchanged per-beat path below.
    bool completedDirectBurst=false;
    const unsigned sourceRegion=src>>24u,destRegion=dst>>24u;
    if(count>=8u && srcMode==0u && dstMode==0u &&
       (sourceRegion==2u||sourceRegion==3u) &&
       (destRegion==5u||destRegion==6u||destRegion==7u) &&
       (line_>=160u || lineCycles_>=960u)){
        const std::size_t length=std::size_t(count)*bytes;
        const std::size_t sourceIndex=sourceRegion==2u ? (src&0x3ffffu) : (src&0x7fffu);
        const std::size_t sourceSize=sourceRegion==2u ? ewram_.size() : iwram_.size();
        const std::size_t destIndex=destRegion==6u?vramIndex(dst):
            (dst&0x3ffu);
        const std::size_t destSize=destRegion==6u?vram_.size():
            (destRegion==5u?palette_.size():oam_.size());
        const bool continuousVideo=(destRegion!=6u ||
            vramIndex(dst+std::uint32_t(length-1u))==destIndex+length-1u);
        if(length<=sourceSize-sourceIndex && length<=destSize-destIndex &&
           continuousVideo){
            const std::uint8_t* sourceBytes=(sourceRegion==2u?
                ewram_.data():iwram_.data())+sourceIndex;
            std::uint8_t* destBytes=(destRegion==5u?palette_.data():
                destRegion==6u?vram_.data():oam_.data())+destIndex;
            std::memcpy(destBytes,sourceBytes,length);
            const auto* last=sourceBytes+length-bytes;
            const std::uint32_t lastValue=word?
                (std::uint32_t(last[0])|(std::uint32_t(last[1])<<8u)|
                 (std::uint32_t(last[2])<<16u)|(std::uint32_t(last[3])<<24u)):
                (std::uint32_t(last[0])|(std::uint32_t(last[1])<<8u));
            dmaBusLatch_[ch]=word?lastValue:lastValue*0x00010001u;
            dmaOpenBusValid_=true;
            dmaOpenBusValue_=dmaBusLatch_[ch];
            if(timing<=2u){
                dmaTransferCycles=std::uint64_t(count)*
                    (cpuAccessCycles(src,bytes,false,false)+
                     cpuAccessCycles(dst,bytes,false,false));
            }
            src+=std::uint32_t(length);
            dst+=std::uint32_t(length);
            completedDirectBurst=true;
        }
    }
    if(!completedDirectBurst)for(unsigned i=0;i<count;i++){
        // BIOS is unreadable to DMA when CPU executes outside firmware.
        // DMA0 additionally has no Game Pak source bus. In both cases the
        // transferred value is the last word left on this DMA's bus, not
        // unrestricted firmware data or a fabricated 0xFFFF read.
        const unsigned region=src>>24u;
        const bool inaccessible=(region==0u ||
                                 region==1u || region>15u);
        const bool gamePakStream=ch!=0 && region>=8u && region<=12u && srcMode!=0u;
        const std::uint32_t readAddress=gamePakStream?gamePakReadAddress:src;
        // Immediate DMA occupies both the source and destination buses.
        // Include the initial nonsequential Game Pak access, then sequential
        // accesses within a burst. Defer elapsed time until CPU instruction
        // completion to avoid recursively advancing a live PPU/DMA event.
        // The CPU is suspended during HBlank/VBlank DMA just as during
        // immediate DMA. Previously blanking bursts copied the data but
        // consumed *zero* CPU clocks, drifting the CPU polling phase against
        // the 1,232-clock scanline over consecutive HBlanks.
        if(timing==0u || timing==1u || timing==2u){
            const auto access=[&](std::uint32_t address)->unsigned {
                const unsigned region=address>>24u;
                if(region==0u||region==1u||region>15u)return 1u;
                // Following DMA transfers retain the actual active Game Pak beat(s)
                // even with instruction prefetch enabled. The first grant is
                // already included in the DMA start/arbitration clocks.
                const bool rom=(region>=8u && region<=13u);
                const bool seqAccess=(i!=0u) || dmaStreamHadGamePakAccess;
                const unsigned clocks=cpuAccessCycles(address,bytes,seqAccess,false);
                if(region>=8u && region<=15u)
                    dmaStreamHadGamePakAccess=true;
                return clocks+(rom && gamePakPrefetchEnabled() && seqAccess
                    ? (bytes==4u ? 2u : 1u) : 0u);
            };
            dmaTransferCycles+=access(readAddress)+access(dst);
        }

        const std::uint32_t value=inaccessible? (word?dmaBusLatch_[ch]:
            static_cast<std::uint16_t>(dmaBusLatch_[ch])) :
            (word?read32(readAddress):read16(readAddress));
        if(gamePakStream)gamePakReadAddress+=bytes;
        if(!inaccessible)dmaBusLatch_[ch]=word?value:(value*0x00010001u);
        dmaOpenBusValid_=true;dmaOpenBusValue_=dmaBusLatch_[ch];
        // The interpreter commits the transfer at the event
        // clock, before its deferred physical stall is retired. This pinpoints
        // a potential CPU data read/DMA overlap without changing behavior.

        if(word)write32(dst,value);else write16(dst,std::uint16_t(value));
        if(srcMode==0)src+=bytes;else if(srcMode==1)src-=bytes;
        if(dstMode==0||dstMode==3)dst+=bytes;else if(dstMode==1)dst-=bytes;
    }
    if(timing==0u){
        const bool srcRom=((d.source>>24u)>=8u && (d.source>>24u)<=13u);
        const bool dstRom=((d.destination>>24u)>=8u && (d.destination>>24u)<=13u);
        // A DMA burst involving the external Game Pak bus retains the
        // established cartridge handoff cost. Internal RAM-only bursts can
        // return control after the shorter internal-bus handoff. Non-prefetched
        // ARM instruction delivery from ROM also needs its external refill
        // phase, even when the DMA data remained entirely on internal buses.
        // The ROM-to-ROM arbitration path keeps its already validated cost.
        const bool externalArmNoPrefetch = GamePakPrefetch::isRom(units_.cpu.r[15]) &&
            !units_.cpu.thumb() && !gamePakPrefetchEnabled();
        // ARM executes two 16-bit instruction-delivery beats. When DMA
        // immediately hands the Game Pak bus back to non-prefetched ARM ROM
        // code, the pipeline restart needs the longer return path. With
        // prefetch enabled, simultaneous ROM source+destination DMA adds
        // one ARM-state reconnection clock, without charging Thumb's narrower
        // fetch path. The actual DMA data cost remains transfer-based.
        const unsigned handoffCycles=(srcRom&&dstRom)
            ? (externalArmNoPrefetch ? 4u : 2u)
            : (externalArmNoPrefetch || (count==1u && (srcRom||dstRom) && gamePakPrefetchEnabled()))?4u:2u;
        // A single 16-bit cartridge WRITE can share its last return clock
        // with the CPU handoff when the nonsequential Game Pak transaction
        // spans at least five bus clocks. Faster ROM N phases cannot overlap
        // this stage. Distinguish direction, bus latency and transfer width,
        // not particular games, PC values or accuracy-suite configurations.
        // This phase hypothesis is regression-tested, not pin-level certified.
        const unsigned romWriteSingleReturnOverlap=(count==1u && !word &&
            dstRom && gamePakPrefetchEnabled() && !externalArmNoPrefetch &&
            GamePakPrefetch::busCycles(d.destination,false,u16(io_,0x204u))>=5u)
            ? 1u : 0u;
        const unsigned armPrefetchReturn=(srcRom&&dstRom &&
            !units_.cpu.thumb() && gamePakPrefetchEnabled())?1u:0u;
        // A one-beat, 16-bit cartridge READ ending on an internal bus can
        // release its read-return stage one clock before the wider ROM write
        // and word-transfer paths. Keep the non-prefetched ARM ROM refill
        // independent: that port still needs its full external return.
        // This is a phase-model hypothesis pending external pin-level
        // validation; it is deliberately based on the physical transfer
        // direction/width, never the program or its expected test result.
        const unsigned readHalfwordReturnOverlap=(count==1u && !word &&
            srcRom && !dstRom && gamePakPrefetchEnabled() &&
            !externalArmNoPrefetch) ? 1u : 0u;
        // A multi-beat word transfer reads two Game Pak halfwords per DMA
        // unit. The final read-return stage consumes a separate ARM port
        // reconnect clock with prefetch enabled; Thumb's halfword-width port
        // remains independent. This is a phase-model hypothesis, not a
        // hardware bus ordering; guard against cross-width regressions.
        const unsigned wordBurstRomSourceReturn=(word && count>1u &&
            srcRom && !dstRom && gamePakPrefetchEnabled() &&
            !units_.cpu.thumb())?1u:0u;
        // A slow Game Pak read can retire its final bus-return stage during
        // the CPU handoff even after the earlier one-clock halfword overlap.
        // Word reads use a two-halfword data port, but still have a final
        // halfword-return clock. Faster N reads retain that return latency.
        const unsigned slowRomSourceReturnOverlap=(count==1u && srcRom &&
            !dstRom && gamePakPrefetchEnabled() &&
            GamePakPrefetch::busCycles(d.source,false,u16(io_,0x204u))>=5u)
            ? 1u : 0u;
        // A single word WRITE into ROM occupies two Game Pak halfwords.
        // The return overlaps Thumb's narrower CPU instruction port, but
        // not ARM's two-halfword reconnect. The nonsequential phase must
        // be long enough to overlap this last edge.
        const unsigned slowWordWriteReturnOverlap=(count==1u && word &&
            dstRom && !srcRom && units_.cpu.thumb() &&
            gamePakPrefetchEnabled() &&
            GamePakPrefetch::busCycles(d.destination,false,u16(io_,0x204u))>=5u)
            ? 1u : 0u;
        const unsigned burst16FastNReturn=(count>1u && !word && srcRom && !dstRom &&
            gamePakPrefetchEnabled() && !units_.cpu.thumb() &&
            GamePakPrefetch::busCycles(d.source,false,u16(io_,0x204u))<=4u)?1u:0u;
        const unsigned burst16SlowNThumbOverlap=(count>1u && !word && srcRom && !dstRom &&
            gamePakPrefetchEnabled() && units_.cpu.thumb() &&
            GamePakPrefetch::busCycles(d.source,false,u16(io_,0x204u))>=5u)?1u:0u;
        const unsigned burst32FastNReturn=(count>1u && word && srcRom && !dstRom &&
            gamePakPrefetchEnabled() &&
            GamePakPrefetch::busCycles(d.source,false,u16(io_,0x204u))<=4u)?1u:0u;
        const unsigned romToRomWordFastNReturn=(word && srcRom && dstRom &&
            gamePakPrefetchEnabled() &&
            GamePakPrefetch::busCycles(d.source,false,u16(io_,0x204u))<=4u)?1u:0u;
        const unsigned romToRomHalfwordSlowNOverlap=(count>1u && !word && srcRom && dstRom &&
            gamePakPrefetchEnabled() &&
            GamePakPrefetch::busCycles(d.source,false,u16(io_,0x204u))>=5u)?1u:0u;
        const bool single16RomWrite=count==1u && !word && !srcRom && dstRom &&
            gamePakPrefetchEnabled();
        const bool fastSeqReturn=single16RomWrite &&
            GamePakPrefetch::busCycles(d.destination+2u,true,u16(io_,0x204u))<=2u;
        const unsigned arm16FastSReturnOverlap=single16RomWrite &&
            !units_.cpu.thumb() && fastSeqReturn ? 2u : 0u;
        const unsigned thumb16SlowSReturnOverlap=single16RomWrite &&
            units_.cpu.thumb() && !fastSeqReturn ? 1u : 0u;
        const unsigned thumbSingleRomReadSlowSOverlap=(count==1u && srcRom && !dstRom &&
            gamePakPrefetchEnabled() && units_.cpu.thumb() &&
            GamePakPrefetch::busCycles(d.source+2u,true,u16(io_,0x204u))>2u)?1u:0u;
        // Cartridge-write streams and the CPU's returning ROM fetch can
        // overlap N and S phases independently. A word stream occupies two
        // halfword deliveries, whereas Thumb receives only one instruction
        // halfword at a time. Account by physical access speed and width.
        unsigned romWriteReturnAdditional=0u,romWriteReturnOverlap=0u;
        if(dstRom && !srcRom && gamePakPrefetchEnabled() &&
            GamePakPrefetch::isRom(units_.cpu.r[15])){
            const auto waitcnt=u16(io_,0x204u);
            const bool fastN=GamePakPrefetch::busCycles(d.destination,false,waitcnt)<=4u;
            const bool fastS=GamePakPrefetch::busCycles(d.destination+2u,true,waitcnt)<=2u;
            if(word && count==1u && !units_.cpu.thumb()){
                romWriteReturnAdditional+=unsigned(fastN);
                if(fastS)romWriteReturnOverlap+=2u;
            }else if(word && count>1u){
                if(units_.cpu.thumb()){
                    if(!fastS)romWriteReturnAdditional+=fastN?2u:1u;
                }else{
                    romWriteReturnAdditional+=unsigned(fastN)+(!fastS?2u:0u);
                }
            }else if(!word && count>1u && !units_.cpu.thumb()){
                romWriteReturnAdditional+=unsigned(fastN)+(!fastS?1u:0u);
                if(fastS)++romWriteReturnOverlap;
            }else if(!word && count>1u && units_.cpu.thumb()){
                if(fastN && !fastS)++romWriteReturnAdditional;
            }
        }
        dmaPendingCpuCycles_+=dmaTransferCycles+handoffCycles+
            armPrefetchReturn-readHalfwordReturnOverlap+wordBurstRomSourceReturn-
            romWriteSingleReturnOverlap-slowRomSourceReturnOverlap-
            slowWordWriteReturnOverlap+burst16FastNReturn-burst16SlowNThumbOverlap+
            burst32FastNReturn+romToRomWordFastNReturn-romToRomHalfwordSlowNOverlap-
            arm16FastSReturnOverlap-thumb16SlowSReturnOverlap-thumbSingleRomReadSlowSOverlap+
            romWriteReturnAdditional-romWriteReturnOverlap;
        // DMA request takes time to win bus arbitration. The CPU can execute
        // instructions whose fetch/read phase finishes before that boundary.
        dmaReadyCycle_=std::max(dmaReadyCycle_,units_.scheduler.cycles()+3u);
    }
    // Blanking DMA begins at the video event edge and suspends the CPU.
    // A two-clock bus handoff is added after its actual source and destination
    // beats. Defer CPU debt rather than advancing a hardware event recursively.
    if(timing==1u || timing==2u){
        // CPU HALT has no outstanding instruction/data bus transaction.
        // HBlank/VBlank DMA completes as time advances through the sleep
        // interval. Its bus occupancy must not be booked as a CPU stall
        // hundreds of scanlines later, when an interrupt wakes the CPU.
        // Preserve normal contention accounting for a running CPU.
        if(!units_.cpu.halted){
            // An HBlank DMA returning to Thumb ROM code must restore
            // the interrupted halfword-fetch sequence. The additional
            // nonoverlapping return clock is not charged to internal code
            // or the independent VBlank path. This is a measured phase
            // model, not a fully serialized per-beat scheduler.
            dmaPendingCpuCycles_+=dmaTransferCycles+
                (timing==2u && GamePakPrefetch::isRom(units_.cpu.r[15]) &&
                 units_.cpu.thumb() ? 3u : 2u);
            dmaReadyCycle_=std::max(dmaReadyCycle_,units_.scheduler.cycles()+2u);
        }
    }
    

    d.source=src;
    d.destination=(dstMode==3)?d.initialDestination:dst;
    ++dmaBursts_[ch];
    if(d.control&0x4000u)units_.interrupts.request(std::uint16_t(1u<<(8+ch)));
    if((d.control&0x200u) && timing!=0u){
        // Repeating DMA reloads the programmed word count for its next
        // trigger, not the already-active count of the current burst.
        d.count=u16(io_,0xb8+12*ch);
    } else {
        d.control&=~0x8000u;
        raw16(io_,0xba+12*ch,std::uint16_t(d.control));
    }
}
void GbaCore::updateDisplayStatus(bool hEvent,bool vEvent){
    const std::uint16_t control=u16(io_,4);
    const bool hblank=lineCycles_>=1006,vblank=line_>=160&&line_<=226;
    const bool vcount=line_==((control>>8)&255u);
    const std::uint16_t old=control&7u;
    raw16(io_,4,std::uint16_t((control&~7u)|(vblank?1u:0u)|(hblank?2u:0u)|(vcount?4u:0u)));
    raw16(io_,6,std::uint16_t(line_));
    if(vEvent&&(control&0x8u))units_.interrupts.request(1u);
    if(hEvent&&(control&0x10u))units_.interrupts.request(2u);
    if(vcount&&!(old&4u)&&(control&0x20u))units_.interrupts.request(4u);
}
void GbaCore::primeBranchTargetPair(std::uint32_t target,unsigned width){
    // The target pair has reached the CPU pipeline. FIFO background work
    // resumes with the following instruction, rather than fetching the
    // just-delivered target for a second time.
    const auto address=target+2u*width;
    if(!GamePakPrefetch::isRom(address))return;
    GamePakPrefetch::State next{};
    next.waitcnt=u16(io_,0x204u);
    next.head=next.next=address;
    next.valid=true;next.sequential=true;
    gamePakPrefetch_.restore(next);
}
// WAITCNT bit 14 is high-byte bit 6. A single byte test replaces a
// repeated 16-bit decode in the CPU cartridge-fetch hot path.
bool GbaCore::gamePakPrefetchEnabled() const {return (io_[0x205u]&0x40u)!=0;}
unsigned GbaCore::cpuInstructionFetchCycles(std::uint32_t address,unsigned bytes,bool sequential){
    if(gamePakPrefetchEnabled() && GamePakPrefetch::isRom(address)){
        const auto fetched=gamePakPrefetch_.instruction(address,bytes,sequential,
            u16(io_,0x204),fifoDeliveryPrefetch);
        const unsigned physical=fetched.cycles;
        physicalOpcodeFetchIssued_=fetched.misses>0;
        if(conservativeNextFetch_){
            conservativeNextFetch_=false;
            const bool dataReturn=cartridgeDataReturn_;
            cartridgeDataReturn_=false;
            physicalOpcodeFetchIssued_=true;
            const unsigned fallback=cpuAccessCycles(address,bytes,sequential,true);
            // Cartridge data transactions also retire the ARM7TDMI pipeline
            // data-return stage before new Thumb instructions are delivered.
            return fallback+(dataReturn && bytes==2u ? 3u : 0u);
        }
        return physical;
    }
    gamePakPrefetch_.externalBusAccess();
    physicalOpcodeFetchIssued_=true;
    conservativeNextFetch_=false;cartridgeDataReturn_=false;
    return cpuAccessCycles(address,bytes,sequential,true);
}
unsigned GbaCore::cpuAccessCycles(std::uint32_t address,unsigned bytes,bool sequential,bool opcodeFetch) const {
    // Cycle counts are for a *single bus transfer*, not the instruction's
    // internal ARM7TDMI cycles. CPU-only access, not DMA contention.
    const unsigned region=address>>24u;
    if(region==2) return bytes==4?6u:3u; // external WRAM: 3 cycles per 16-bit halfword
    if(region==5||region==6) return bytes==4?2u:1u; // 16-bit video bus
    if(region==14 || region==15){
        // Both 0x0E and 0x0F are mirrors of the same 8-bit backup bus.
        // The SRAM WAITCNT selection applies identically to each region.
        static constexpr unsigned wait[4]={4,3,2,8};
        // WAITCNT encodes wait states; the 8-bit cartridge backup bus
        // also occupies one active cycle for a byte transaction.
        return wait[u16(io_,0x204)&3u] + 1u;
    }
    if(region>=8&&region<=13){
        const unsigned waitcnt=u16(io_,0x204);
        const unsigned bank=(region-8u)/2u;
        static constexpr unsigned first[4]={4,3,2,8};
        static constexpr unsigned second[3][2]={{2,1},{4,1},{8,1}};
        // GamePak begins a new nonsequential bus burst every 128 KiB.
        if((address&0x1ffffu)==0)sequential=false;
        const unsigned start=sequential?second[bank][(waitcnt>>(4u+3u*bank))&1u]
                                        :first[(waitcnt>>(2u+3u*bank))&3u];
        // Each occupied halfword on the 16-bit Game Pak bus has an active
        // transfer beat in addition to its configured wait-state interval.
        // Non-prefetched data accesses and Thumb instruction fetches account
        // for that beat. ARM (two-beat) opcode fetches still use the legacy
        // pipeline estimate: adding both beats there regresses known ARM
        // calibration cases until pipeline/fetch arbitration is modeled.
        // Prefetch-enabled instruction fetches likewise need the FIFO model.
        const bool prefetchDisabled=(waitcnt&0x4000u)==0;
        const unsigned activeBeat=(prefetchDisabled && (!opcodeFetch || units_.cpu.thumb()))?1u:0u;
        if(bytes==4) return start+second[bank][(waitcnt>>(4u+3u*bank))&1u]+2u*activeBeat;
        return start+activeBeat;
    }
    return 1u; // IWRAM, BIOS, IO, OAM and other single-cycle buses
}
void GbaCore::advanceTimers(unsigned cycles){
    // Nothing to integrate if all four timers are disabled. This is common
    // during BIOS startup, pause screens, and early game initialization.
    const auto& ts=units_.timers.timers;
    if(!((ts[0].control|ts[1].control|ts[2].control|ts[3].control)&0x80u))return;
    // Common no-overflow Timer0 clock. A cascaded Timer1 only counts Timer0
    // overflows, so neither counter nor IRQ/FIFO/DMA can change in this
    // interval. Avoid the four-timer event machinery unless an overflow or
    // an outstanding Timer0 enable delay genuinely needs the full scheduler.
    if((ts[0].control&0x80u) &&
       !((ts[2].control|ts[3].control)&0x80u) &&
       (!(ts[1].control&0x80u) || (ts[1].control&4u)) &&
       !(timerStartDelayMask_&1u)){
        auto& t=units_.timers.timers[0];
        static constexpr unsigned shifts[4]={0,6,8,10};
        const unsigned shift=shifts[t.control&3u];
        const unsigned phase=t.phase+cycles;
        const unsigned ticks=phase>>shift;
        if(ticks < 0x10000u-t.counter){
            t.phase=phase&((1u<<shift)-1u);
            t.counter=std::uint16_t(t.counter+ticks);
            if(ticks && shift==0u)timerHasTickedMask_|=1u;
            return;
        }
    }
    constexpr unsigned divs[4]={1,64,256,1024};
    unsigned overflows[4]={};
    for(unsigned i=0;i<4;i++){
        auto& t=units_.timers.timers[i];
        if(!(t.control&0x80))continue;
        unsigned increments=0;
        if(i>0&&(t.control&4))increments=overflows[i-1];
        else {const unsigned divisor=divs[t.control&3];
            unsigned timerCycles=cycles;
            if(timerStartDelayMask_&(1u<<i)){
                timerStartDelayMask_&=std::uint8_t(~(1u<<i));
                if(timerCycles) --timerCycles;
            }
            // GBA prescaler periods are powers of two. Use exact bit
            // arithmetic rather than costly runtime integer division.
            static constexpr unsigned shifts[4]={0u,6u,8u,10u};
            const unsigned bits=shifts[t.control&3u];
            const unsigned phase=t.phase+timerCycles;
            increments=phase>>bits;
            t.phase=phase&(divisor-1u);
            if(increments && divisor==1u)timerHasTickedMask_|=std::uint8_t(1u<<i);
        }
        while(increments){
            const unsigned until=0x10000u-t.counter;
            if(increments<until){t.counter=std::uint16_t(t.counter+increments);break;}
            increments-=until;t.counter=t.reload;overflows[i]++;
            if(i<=1){
                units_.apu.timerOverflow(i,io_);
                for(unsigned c=1;c<=2;c++){
                    const auto& dma=units_.dma.channels[c];
                    const unsigned dst=dma.destination&~3u;
                    const unsigned fifo=dst==0x040000a0u?0:dst==0x040000a4u?1:2;
                    if(fifo<2 && ((io_[0x83]>>(fifo?6:2))&1u)==i && units_.apu.fifoNeedsRefill(fifo))
                        runDma(c,3);
                }
            }
            if(t.control&0x40){
                // Trace only the newly latched IRQ edge, not every timer
                // overflow while IF already holds the same interrupt.
                const bool timer0NewEdge=i==0u && !(units_.interrupts.interruptFlags&8u);
                if(timer0NewEdge){
                    timer0IrqAsserted_=true;
                    timer0IrqAssertCycle_=units_.scheduler.cycles();
                }
                units_.interrupts.request(std::uint16_t(1u<<(3+i)));
                

            }
            

        }
    }
}
void GbaCore::advanceSerial(unsigned cycles){
    if(!serialCyclesRemaining_)return;
    if(cycles<serialCyclesRemaining_){serialCyclesRemaining_-=cycles;return;}
    serialCyclesRemaining_=0;
    // Clock is complete: release START, latch idle-high disconnected SI,
    // and request the serial interrupt when enabled. Data receive lines
    // remain unconnected and therefore read as logic-high.
    const std::uint16_t control=u16(io_,0x128u);
    raw16(io_,0x128u,std::uint16_t(control&~0x80u));
    if(control&0x1000u){raw16(io_,0x120u,0xffffu);raw16(io_,0x122u,0xffffu);}
    else raw16(io_,0x12au,0xffu);
    if(control&0x4000u)units_.interrupts.request(1u<<7);
}
void GbaCore::advanceHardware(unsigned n){
    // Most ARM/Thumb instructions advance only a few CPU cycles. Avoid the
    // generic event-splitting loop unless a display/sample/timer boundary
    // actually falls *inside* that instruction. Keep the same event order:
    // time -> APU before timer FIFO latch -> scanline.
    if(n<=32u && n<=units_.apu.cyclesUntilSample()){
        const unsigned edge=lineCycles_<960u?960u:lineCycles_<1006u?1006u:lineCycles_<1008u?1008u:1232u;
        if(n<edge-lineCycles_ && (!serialCyclesRemaining_ || n<serialCyclesRemaining_)){
            bool split=false;
            static constexpr unsigned divisors[4]={1,64,256,1024};
            for(unsigned i=0;i<2;i++){
                const auto& timer=units_.timers.timers[i];
                if(!(timer.control&0x80u)||(i && (timer.control&4u)))continue;
                const unsigned distance=(0x10000u-timer.counter)*divisors[timer.control&3u]-timer.phase+unsigned((timerStartDelayMask_>>i)&1u);
                if(distance<n){split=true;break;}
            }
            if(!split){
                // Exact common timer clocking without a second four-timer
                // dispatch. The test above already proved no Timer0 FIFO/IRQ
                // overflow occurs inside this bus phase. A cascaded Timer1
                // cannot tick in the absence of a Timer0 overflow.
                // Do not use this path while enabling Timer0 (one-cycle
                // latch), or if independently clocked timers 1/2/3 run.
                const auto& ts=units_.timers.timers;
                const bool simpleTimer0=(ts[0].control&0x80u) &&
                    !((ts[2].control|ts[3].control)&0x80u) &&
                    (!(ts[1].control&0x80u) || (ts[1].control&4u)) &&
                    !(timerStartDelayMask_&1u);
                units_.scheduler.advance(n);
                units_.apu.advanceCycles(n,io_);
                if(simpleTimer0){
                    auto& t=units_.timers.timers[0];
                    static constexpr unsigned shifts[4]={0u,6u,8u,10u};
                    const unsigned shift=shifts[t.control&3u];
                    const unsigned phase=t.phase+n;
                    const unsigned ticks=phase>>shift;
                    // The scheduler never entered this path if an overflow
                    // was imminent. Retain fallback for arithmetic edge cases.
                    if(ticks<0x10000u-t.counter){
                        t.phase=phase&((1u<<shift)-1u);
                        t.counter=std::uint16_t(t.counter+ticks);
                        if(ticks && !shift)timerHasTickedMask_|=1u;
                    }else advanceTimers(n);
                }else advanceTimers(n);
                advanceSerial(n);
                lineCycles_+=n;
                return;
            }
        }
    }
    // Event interleaving: timer FIFO consumption must precede audio sampling.
    // Using small CPU-cycle batches is provisional until an exact event scheduler
    // is implemented, but avoids processing an entire frame's audio ahead of IRQs.
    while(n){
        // Bound the next batch by the nearest active non-cascaded timer
        // overflow and the next output sample. A sample must observe the
        // correct PCM FIFO latch at its actual cycle boundary.
        unsigned step=std::min({n,32u,units_.apu.cyclesUntilSample()});
        // A single clock domain must not advance timers past a PPU event
        // before the HBlank/VBlank DMA and DISPSTAT edge are dispatched.
        // Split at the *next* pixel/blank boundary, so events are ordered
        // by time rather than by subsystem update order within a batch.
        const unsigned videoBoundary = lineCycles_<960u ? 960u :
                                       lineCycles_<1006u ? 1006u :
                                       lineCycles_<1008u ? 1008u : 1232u;
        step=std::min(step,videoBoundary-lineCycles_);
        if(serialCyclesRemaining_)step=std::min(step,serialCyclesRemaining_);
        constexpr unsigned divs[4]={1,64,256,1024};
        for(unsigned ti=0;ti<2;ti++){
            const auto& t=units_.timers.timers[ti];
            if(!(t.control&0x80u)||(ti>0&&(t.control&4u)))continue;
            const unsigned divisor=divs[t.control&3u];
            const unsigned left=(0x10000u-t.counter)*divisor-t.phase+unsigned((timerStartDelayMask_>>ti)&1u);
            if(left && left<step)step=left;
        }
        n-=step;
        units_.scheduler.advance(step);
        // Advance the *preceding* time interval with the old FIFO DAC
        // latch. A timer overflow takes effect at the end of this interval,
        // not retroactively at its beginning.
        units_.apu.advanceCycles(step,io_);
        advanceTimers(step);
        advanceSerial(step);
        unsigned pending=step;
        while(pending){
            const unsigned boundary=lineCycles_<960?960:lineCycles_<1006?1006:lineCycles_<1008?1008:1232;
            const unsigned take=std::min(pending,boundary-lineCycles_);
            pending-=take;lineCycles_+=take;
            if(lineCycles_==960){
                if(line_<160){
                    flushVisiblePixels(Width);
                    units_.ppu.advanceAffine(io_);
                }
            }
            if(lineCycles_==1006){
                // Passive hardware-event marker. Keep DISPSTAT/IRQ
                // ordering unchanged and record the scanline at its real edge.
                

                updateDisplayStatus(true,false);
                // Snapshot eligible channels at the *request* edge, not the
                // later grant. CPU writes during the two-clock window must
                // never manufacture a missed HBlank request retroactively.
                hblankDmaPendingMask_=0;
                if(line_<160u)for(unsigned c=0;c<4u;c++){
                    const auto control=units_.dma.channels[c].control;
                    if((control&0x8000u)&&((control>>12u)&3u)==2u)
                        hblankDmaPendingMask_|=std::uint8_t(1u<<c);
                }
                // DISPSTAT/IRQ assert now; grants become eligible at 1008.
            }
            if(lineCycles_==1008){
                // Consume only channels that were already requesting DMA
                // at the earlier HBlank edge (in hardware channel priority).
                const std::uint8_t pending=hblankDmaPendingMask_;
                hblankDmaPendingMask_=0;
                for(unsigned c=0;c<4;c++)if(pending&(1u<<c))runDma(c,2);
            }
            if(lineCycles_==1232){
                hblankDmaPendingMask_=0;
                lineCycles_=0;renderedX_=0;line_++;
                if(line_==160){units_.ppu.latchAffine(io_);updateDisplayStatus(false,true);for(unsigned c=0;c<4;c++)runDma(c,1);}
                if(line_==228){line_=0;frames_++;
                    if(!savePath_.empty()&&units_.save.dirty()&&frames_>=lastSaveFlushFrame_+120){
                        std::string ignored;flushSave(ignored);lastSaveFlushFrame_=frames_;
                    }
                }
                updateDisplayStatus(false,false);
            }
        }
    }
}
void GbaCore::beginCpuInstructionTiming(){
    

    cpuLastGamePakReadWidth_=0;cpuPhysicalRomDataRead_=false;cpuEarlyRomFetchClocks_=0;
    cpuInstructionTimingActive_=true;
    cpuInstructionCyclesSpent_=0;
    gamePakDataBusOccupied_=false;
}
void GbaCore::advanceCpuTime(unsigned cycles){
    if(!dmaPendingCpuCycles_){advanceHardware(cycles);return;}
    const auto now=units_.scheduler.cycles();
    if(now+cycles<dmaReadyCycle_){advanceHardware(cycles);return;}
    const unsigned before=static_cast<unsigned>(dmaReadyCycle_>now?dmaReadyCycle_-now:0u);
    if(before){advanceHardware(before);cycles-=before;}
    const auto debt=static_cast<unsigned>(std::min<std::uint64_t>(dmaPendingCpuCycles_,1000000u));
    dmaPendingCpuCycles_-=debt;
    if(dmaGamePakClaimPending_){
        gamePakPrefetch_.externalBusAccess();gamePakDataBusOccupied_=true;
        conservativeNextFetch_=true;dmaGamePakClaimPending_=false;
        

    }
    // Actual deferred DMA charge point (distinct from the grant descriptors,
    // which are emitted when DMA is requested). Read-only timing evidence.
    

    advanceHardware(debt);
    dmaStepStallCycles_+=debt;
    if(cycles)advanceHardware(cycles);
}
bool GbaCore::finishPhysicalRomDataReturn(){
    if(!cpuPhysicalRomDataRead_ || dmaPendingCpuCycles_)return false;
    cpuPhysicalRomDataRead_=false;cpuEarlyRomFetchClocks_=0;
    const unsigned width=units_.cpu.thumb()?2u:4u;
    const auto address=units_.cpu.r[15]+2u*width;
    if(!gamePakPrefetchEnabled() || !GamePakPrefetch::isRom(address))return false;
    GamePakPrefetch::State next{};
    next.waitcnt=u16(io_,0x204u);next.head=next.next=address;
    next.valid=true;next.sequential=false;
    gamePakPrefetch_.restore(next);
    conservativeNextFetch_=false;cartridgeDataReturn_=false;
    // The load's internal writeback still owns the data-return latch.
    // Begin a fresh N instruction transaction after that interlock, rather
    // than treating the already booked return clock as idle FIFO fill.
    gamePakDataBusOccupied_=true;
    return true;
}
unsigned GbaCore::waitForCpuGamePakClaim(std::uint32_t address){
#if !defined(CIPHERGBA_PREFETCH_ACTIVE_DATA_CLAIM) || CIPHERGBA_PREFETCH_ACTIVE_DATA_CLAIM
    if(!cpuInstructionTimingActive_ || !GamePakPrefetch::isRom(address) || !gamePakPrefetchEnabled())return 0u;
    const auto state=gamePakPrefetch_.state();
    if(!state.inflight || state.remaining!=1u)return 0u;
    gamePakPrefetch_.internalCycles(1u,u16(io_,0x204u));
    cpuTimedBusPhase(1u);
    

    return 1u;
#else
    (void)address;return 0u;
#endif
}
void GbaCore::promoteRomReadFetchBeat(){
    if(!cpuInstructionTimingActive_ || cpuEarlyRomFetchClocks_)return;
    cpuTimedBusPhase(1u);cpuEarlyRomFetchClocks_=1u;
}
void GbaCore::cpuDataBusPhase(unsigned cycles){
#if !defined(CIPHERGBA_PHASED_CPU_DATA_BUS) || CIPHERGBA_PHASED_CPU_DATA_BUS
    if(!cpuInstructionTimingActive_)return;
    // Internal RAM and peripheral data cycles leave the external Game Pak
    // bus free. Preserve that physical prefetch opportunity while moving
    // these clocks out of the instruction's residual retirement budget.
    if(!gamePakDataBusOccupied_){
        gamePakPrefetch_.internalCycles(cycles,u16(io_,0x204u));
        

    }
    cpuTimedBusPhase(cycles);
#else
    (void)cycles;
#endif
}
void GbaCore::cpuTimedBusPhase(unsigned cycles){
    if(!cpuInstructionTimingActive_)return;
    advanceCpuTime(cycles);
    cpuInstructionCyclesSpent_+=cycles;
}
void GbaCore::finishCpuInstructionTiming(unsigned total){
    cpuInstructionTimingActive_=false;
    if(!gamePakDataBusOccupied_ && total>cpuInstructionCyclesSpent_)
        gamePakPrefetch_.internalCycles(total-cpuInstructionCyclesSpent_,u16(io_,0x204u));
    if(total>cpuInstructionCyclesSpent_)advanceCpuTime(total-cpuInstructionCyclesSpent_);
    if(hleFirmwareRomReturnWidth_){
        gamePakPrefetch_.branch();
        primeBranchTargetPair(hleFirmwareRomReturnTarget_,hleFirmwareRomReturnWidth_);
        hleFirmwareRomReturnWidth_=0u;
        hleFirmwareRomReturnTarget_=0u;
    }
    // An impossible negative balance indicates a cost-accounting error.
    // Report it rather than silently dropping emulated cycles.
    if(cpuInstructionCyclesSpent_>total)
        throw std::logic_error("CPU bus phases exceed instruction cycle cost");
}
unsigned GbaCore::step(){
    dmaStepStallCycles_=0;
    unsigned interruptEntryCycles=0;
    if(hleIrqActive_ && units_.cpu.r[15]==0x00000100u)
        finishHleIrq();
    if(!(units_.interrupts.interruptFlags&8u))timer0IrqAsserted_=false;
    // A newly asserted Timer0-only IRQ is sampled after a bounded five-cycle
    // synchronization window. HALT wake and unrelated IRQs are unaffected.
    // The exact window was suite-calibrated and needs real-hardware validation.
    const bool irqSynchronizing=timer0IrqAsserted_ && !units_.cpu.halted &&
         (units_.interrupts.interruptEnable&units_.interrupts.interruptFlags)==8u &&
         (units_.scheduler.cycles()-timer0IrqAssertCycle_)<5u;
    // Observe the precise CPU-boundary IRQ decision without changing the
    // five-clock synchronization policy or synthesizing hardware timings.
    

    if(!irqSynchronizing && units_.interrupts.pending()&&!(units_.cpu.cpsr&0x80u) && (bios_.loaded() || !hleIrqActive_)){
        if(units_.cpu.halted)advanceHardware((units_.interrupts.interruptEnable&units_.interrupts.interruptFlags)==8u?5u:8u); // provisional: IRQ wake return bus phase
        if(!bios_.loaded())beginHleIrq();
        else {
            // IRQ exception entry flushes the prefetched instruction queue
            // and refills the pipeline (three ARM7TDMI bus phases). This
            // latency must elapse before the firmware IRQ handler executes.
            gamePakPrefetch_.branch();
            

            units_.cpu.irq();
            

            advanceHardware(3u);
            interruptEntryCycles=3u;
        }
    }
    if(hleWaitMask_ && !hleIrqActive_){
        const auto flags=read16(0x03007ff8);
        if(flags&hleWaitMask_){
            // Firmware wait and cartridge refill execute within the core HLE BIOS module.
            finishHleWait(flags);
            return 3u+interruptEntryCycles+static_cast<unsigned>(dmaStepStallCycles_);
        }else{
            // The IRQ handler may have woken the CPU for an unrelated IRQ.
            // IntrWait remains suspended until the requested BIOS flag arrives.
            units_.cpu.halted=true;
        }
    }else if(units_.cpu.halted &&
             (units_.interrupts.interruptEnable&units_.interrupts.interruptFlags)){
        // Ordinary HALT wakes on IE&IF, even when IME is off.
        units_.cpu.halted=false;
    }
    // HALT can wake on IE&IF with IME disabled; no IRQ handler runs in
    // that case. Finish the suspended firmware SWI return before running
    // another guest instruction, just as when the IRQ epilogue completes.
    if(!bios_.loaded() && hleHaltSwiPending_ && !hleIrqActive_ && !units_.cpu.halted){
        finishHleHalt();
    }
    if(units_.cpu.halted){
        // HALT consumes bus cycles but executes no instructions. Instead of
        // simulating one empty CPU cycle per step, jump to the next possible
        // wakeup event. Restrict the batch to display edges and the earliest
        // timer overflow (including the source of any cascaded timer). The
        // regular hardware scheduler still advances every PSG/PCM event.
        const unsigned edge=lineCycles_<960u?960u:lineCycles_<1006u?1006u:lineCycles_<1008u?1008u:1232u;
        unsigned sleepCycles=edge-lineCycles_;
        static constexpr unsigned divisors[4]={1,64,256,1024};
        for(unsigned i=0;i<4;i++){
            const auto& t=units_.timers.timers[i];
            if(!(t.control&0x80u)||(i && (t.control&4u)))continue;
            const unsigned until=(0x10000u-t.counter)*divisors[t.control&3u]-t.phase+unsigned((timerStartDelayMask_>>i)&1u);
            if(until && until<sleepCycles)sleepCycles=until;
        }
        // A serial-clock completion can wake HALT just like a timer or video
        // interrupt. Never jump beyond this event or the IRQ is delivered a
        // scanline late, breaking both link timing and serial BIOS waits.
        if(serialCyclesRemaining_ && serialCyclesRemaining_<sleepCycles)
            sleepCycles=serialCyclesRemaining_;
        if(dmaGamePakClaimPending_){
            const auto now=units_.scheduler.cycles();
            const auto until=dmaReadyCycle_>now?dmaReadyCycle_-now:0u;
            sleepCycles=static_cast<unsigned>(std::min<std::uint64_t>(sleepCycles,until));
            advanceCpuTime(sleepCycles);
        }else advanceHardware(sleepCycles);
        return sleepCycles+interruptEntryCycles+static_cast<unsigned>(dmaStepStallCycles_);
    }
    beginCpuInstructionTiming();
    const unsigned n=units_.cpu.stepInstruction(*this);
    // Capture explicit bus phases before the residual (overlapping/internal)
    // phase is retired. This instrumentation never changes emulator clocks.

    finishCpuInstructionTiming(n);
    

    

    return n+interruptEntryCycles+dmaStepStallCycles_;
}
bool GbaCore::runFrame(){
    if(!cart_.loaded())return false;
    const std::uint64_t start=frames_;
    for(unsigned guard=0;guard<500000 && frames_==start;guard++)step();
    return frames_>start;
}
} // namespace pixelgba
