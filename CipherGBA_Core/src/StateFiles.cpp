#include "pixelgba/StateFiles.hpp"
#include "pixelgba/AtomicReplace.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <type_traits>
#include <vector>
#include <zlib.h>
namespace pixelgba {
namespace {
constexpr std::size_t MaxPayload=16u*1024u*1024u;
constexpr std::size_t MaxSamples=32768u*8u;
struct Writer {
    std::vector<std::uint8_t> bytes;
    template<typename T>void pod(const T& item){
        static_assert(std::is_trivially_copyable<T>::value,"POD state field expected");
        const auto* p=reinterpret_cast<const std::uint8_t*>(&item);
        bytes.insert(bytes.end(),p,p+sizeof(T));
    }
    template<typename T>void vec(const std::vector<T>& arr){
        static_assert(std::is_trivially_copyable<T>::value,"POD vector expected");
        const std::uint64_t length=arr.size();pod(length);
        if(length){const auto* p=reinterpret_cast<const std::uint8_t*>(arr.data());
            bytes.insert(bytes.end(),p,p+length*sizeof(T));}
    }
};
struct Reader {
    const std::vector<std::uint8_t>& bytes;std::size_t at=0;
    template<typename T>bool pod(T& item){
        static_assert(std::is_trivially_copyable<T>::value,"POD state field expected");
        if(at>bytes.size()||sizeof(T)>bytes.size()-at)return false;
        std::memcpy(&item,bytes.data()+at,sizeof(T));at+=sizeof(T);return true;
    }
    template<typename T>bool vec(std::vector<T>& dst,std::size_t maxItems){
        static_assert(std::is_trivially_copyable<T>::value,"POD vector expected");
        std::uint64_t count=0;if(!pod(count)||count>maxItems||count>(bytes.size()-at)/sizeof(T))return false;
        dst.resize(std::size_t(count));if(count)std::memcpy(dst.data(),bytes.data()+at,std::size_t(count)*sizeof(T));
        at+=std::size_t(count)*sizeof(T);return true;
    }
};
struct Header {
    char magic[8]={'C','G','B','A','S','T','2',0};
    std::uint32_t version=2;
    std::uint32_t abi=0;
    std::uint64_t romSize=0;
    std::uint32_t romCrc=0;
    std::uint32_t biosMode=0;
    std::uint32_t payloadBytes=0;
    std::uint32_t checksum=0;
};
std::uint32_t signature(){
    const std::uint32_t layout[]={sizeof(GbaCore::State),sizeof(Arm7tdmi),sizeof(Apu),sizeof(SaveMemory),
        sizeof(DmaController),sizeof(TimerController),sizeof(InterruptController),sizeof(CycleScheduler),
        sizeof(std::size_t),0xC0FFEE02};
    return std::uint32_t(crc32(0,reinterpret_cast<const Bytef*>(layout),sizeof(layout)));
}
}
struct StateFieldsAccess {
    static void serialize(const GbaCore::State& s,Writer& w);
    static bool deserialize(Reader& r,GbaCore::State& s);
};
void StateFieldsAccess::serialize(const GbaCore::State& s,Writer& w){
    w.pod(s.cpu);
    const auto& a=s.apu;
    w.pod(a.channels_);w.pod(a.squares_);w.pod(a.sweep_);w.pod(a.wave_);w.pod(a.noise_);
    w.pod(a.waveRam_);w.vec(a.samples_);
    w.pod(a.dcInput_);w.pod(a.dcOutput_);w.pod(a.mixIntegral_);w.pod(a.pwmSum_);
    w.pod(a.pwmPhase_);w.pod(a.pwmWindows_);w.pod(a.pwmTicks_);
    w.pod(a.samplePhase_);w.pod(a.framePhase_);w.pod(a.frameStep_);
    w.pod(a.masterEnabled_);w.pod(a.channelMask_);
    w.pod(s.dma);w.pod(s.timers);w.pod(s.interrupts);
    const auto& m=s.save;
    w.pod(m.type_);w.vec(m.raw_);w.pod(m.flash_);w.pod(m.identification_);
    w.pod(m.bank_);w.pod(m.dirty_);w.pod(m.eepromAddressBits_);w.vec(m.serial_);
    w.vec(m.response_);w.pod(m.responseAt_);
    w.pod(s.scheduler.cycles_);w.pod(s.affine);w.vec(s.packedMemory);
    w.pod(s.keyInput);w.pod(s.keyIrqLevel);w.pod(s.hleIrqActive);
    w.pod(s.line);w.pod(s.lineCycles);w.pod(s.renderedX);w.pod(s.frames);
    w.pod(s.haltWrites);w.pod(s.lastSaveFlushFrame);w.pod(s.dmaBursts);
    w.pod(s.hleIrqReturn);w.pod(s.hleIrqStatus);w.pod(s.hleSavedRegisters);
    w.pod(s.hleWaitMask);w.pod(s.unsupportedHleSwis);w.pod(s.hleIrqDispatches);
    w.pod(s.biosPrefetchLatch);w.pod(s.biosExecutionActive);
    w.pod(s.pipePc);w.pod(s.pipeCurrent);w.pod(s.pipeNext);w.pod(s.pipeThumb);w.pod(s.pipeValid);
    w.pod(s.dmaBusLatch);w.pod(s.dmaOpenBusValid);w.pod(s.dmaOpenBusValue);
    w.pod(s.serialCyclesRemaining);w.pod(s.timer0IrqAssertCycle);w.pod(s.timer0IrqAsserted);
    w.pod(s.dmaPendingCpuCycles);w.pod(s.dmaReadyCycle);
    w.pod(s.hblankDmaPendingMask);w.pod(s.hleHaltSwiPending);
    w.pod(s.gamePakPrefetch);w.pod(s.conservativeNextFetch);w.pod(s.cartridgeDataReturn);
    w.pod(s.timerStartDelayMask);w.pod(s.timerHasTickedMask);w.pod(s.dmaGamePakClaimPending);
    w.pod(s.physicalOpcodeFetchIssued);w.pod(s.gamePakDataBusOccupied);w.pod(s.cpuPhysicalRomDataRead);
    w.pod(s.cpuLastGamePakReadWidth);w.pod(s.hleFirmwareRomReturnTarget);w.pod(s.hleFirmwareRomReturnWidth);
}
bool StateFieldsAccess::deserialize(Reader& r,GbaCore::State& s){
    auto& a=s.apu;
    if(!r.pod(s.cpu)||!r.pod(a.channels_)||!r.pod(a.squares_)||!r.pod(a.sweep_)||!r.pod(a.wave_)||
       !r.pod(a.noise_)||!r.pod(a.waveRam_)||!r.vec(a.samples_,MaxSamples)||
       !r.pod(a.dcInput_)||!r.pod(a.dcOutput_)||!r.pod(a.mixIntegral_)||!r.pod(a.pwmSum_)||
       !r.pod(a.pwmPhase_)||!r.pod(a.pwmWindows_)||!r.pod(a.pwmTicks_)||
       !r.pod(a.samplePhase_)||!r.pod(a.framePhase_)||!r.pod(a.frameStep_)||
       !r.pod(a.masterEnabled_)||!r.pod(a.channelMask_)||
       !r.pod(s.dma)||!r.pod(s.timers)||!r.pod(s.interrupts))return false;
    auto& m=s.save;
    if(!r.pod(m.type_)||!r.vec(m.raw_,128u*1024u)||!r.pod(m.flash_)||
       !r.pod(m.identification_)||!r.pod(m.bank_)||!r.pod(m.dirty_)||
       !r.pod(m.eepromAddressBits_)||!r.vec(m.serial_,256)||!r.vec(m.response_,256)||
       !r.pod(m.responseAt_)||!r.pod(s.scheduler.cycles_)||!r.pod(s.affine)||
       !r.vec(s.packedMemory,2u*1024u*1024u)||
       !r.pod(s.keyInput)||!r.pod(s.keyIrqLevel)||!r.pod(s.hleIrqActive)||
       !r.pod(s.line)||!r.pod(s.lineCycles)||!r.pod(s.renderedX)||!r.pod(s.frames)||
       !r.pod(s.haltWrites)||!r.pod(s.lastSaveFlushFrame)||!r.pod(s.dmaBursts)||
       !r.pod(s.hleIrqReturn)||!r.pod(s.hleIrqStatus)||!r.pod(s.hleSavedRegisters)||
       !r.pod(s.hleWaitMask)||!r.pod(s.unsupportedHleSwis)||!r.pod(s.hleIrqDispatches)||
       !r.pod(s.biosPrefetchLatch)||!r.pod(s.biosExecutionActive)||
       !r.pod(s.pipePc)||!r.pod(s.pipeCurrent)||!r.pod(s.pipeNext)||
       !r.pod(s.pipeThumb)||!r.pod(s.pipeValid)||!r.pod(s.dmaBusLatch)||
       !r.pod(s.dmaOpenBusValid)||!r.pod(s.dmaOpenBusValue)||
       !r.pod(s.serialCyclesRemaining)||!r.pod(s.timer0IrqAssertCycle)||
       !r.pod(s.timer0IrqAsserted)||!r.pod(s.dmaPendingCpuCycles)||!r.pod(s.dmaReadyCycle)||
       !r.pod(s.hblankDmaPendingMask)||!r.pod(s.hleHaltSwiPending)||
       !r.pod(s.gamePakPrefetch)||!r.pod(s.conservativeNextFetch)||
       !r.pod(s.cartridgeDataReturn)||!r.pod(s.timerStartDelayMask)||
       !r.pod(s.timerHasTickedMask)||!r.pod(s.dmaGamePakClaimPending)||
       !r.pod(s.physicalOpcodeFetchIssued)||!r.pod(s.gamePakDataBusOccupied)||
       !r.pod(s.cpuPhysicalRomDataRead)||!r.pod(s.cpuLastGamePakReadWidth)||
       !r.pod(s.hleFirmwareRomReturnTarget)||!r.pod(s.hleFirmwareRomReturnWidth))return false;
    if(a.channelMask_>0x3fu||m.raw_.size()>128u*1024u||m.responseAt_>m.response_.size()||
       s.line>=228u||s.lineCycles>=1232u||s.renderedX>240u)return false;
    return r.at==r.bytes.size();
}
bool StateCodec::save(const GbaCore& core,const std::filesystem::path& file,std::string& error){
    GbaCore::State state;if(!core.captureState(state)){error="No loaded cartridge.";return false;}
    Writer writer;StateFieldsAccess::serialize(state,writer);if(writer.bytes.size()>MaxPayload){error="Save state too large.";return false;}
    Header header;header.abi=signature();header.romSize=core.cartridge().size();
    header.romCrc=core.cartridge().fingerprint();header.biosMode=core.biosLoaded()?1u:0u;
    header.payloadBytes=std::uint32_t(writer.bytes.size());
    header.checksum=std::uint32_t(crc32(0,writer.bytes.data(),uInt(writer.bytes.size())));
    std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);
    if(ec){error="Cannot create state directory: "+ec.message();return false;}
    auto temp=file;temp+=".tmp";
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        if(!out||!out.write(reinterpret_cast<const char*>(&header),sizeof(header))||
           !out.write(reinterpret_cast<const char*>(writer.bytes.data()),std::streamsize(writer.bytes.size()))||!out.flush()){
            error="Save state write failed.";std::filesystem::remove(temp,ec);return false;}
    }
    if(!replaceFile(temp,file,ec)){error="Save state finalize failed: "+ec.message();std::filesystem::remove(temp,ec);return false;}
    error.clear();return true;
}
bool StateCodec::load(GbaCore& core,const std::filesystem::path& file,std::string& error){
    if(!core.cartridge().loaded()){error="No loaded cartridge.";return false;}
    std::ifstream in(file,std::ios::binary|std::ios::ate);if(!in){error="Save state not found.";return false;}
    const auto length=in.tellg();if(length<std::streamoff(sizeof(Header))||
        length>std::streamoff(sizeof(Header)+MaxPayload)){error="Invalid save state length.";return false;}
    in.seekg(0);Header header{};if(!in.read(reinterpret_cast<char*>(&header),sizeof(header))||
        std::memcmp(header.magic,"CGBAST2",8)!=0||header.version!=2||header.abi!=signature()){
        error="Unsupported or incompatible save state ABI.";return false;}
    if(header.romCrc!=core.cartridge().fingerprint()||header.romSize!=core.cartridge().size()){
        error="This state belongs to a different ROM.";return false;}
    if(header.biosMode!=unsigned(core.biosLoaded())){error="BIOS/HLE mode mismatch.";return false;}
    if(header.payloadBytes!=std::size_t(length)-sizeof(Header)){
        error="Incomplete state payload.";return false;}
    std::vector<std::uint8_t> bytes(header.payloadBytes);
    if(!in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))||
       header.checksum!=std::uint32_t(crc32(0,bytes.data(),uInt(bytes.size())))){
        error="Save state CRC failed.";return false;}
    Reader reader{bytes};GbaCore::State state;
    if(!StateFieldsAccess::deserialize(reader,state)){error="Corrupt state or invalid emulation data.";return false;}
    if(!core.restoreState(state)){error="State hardware validation rejected the snapshot.";return false;}
    core.commitRewindBranch();error.clear();return true;
}
} // namespace pixelgba
