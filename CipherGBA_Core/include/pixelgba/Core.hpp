// Created by Jacob Hodgkins.
#pragma once
#include "pixelgba/Bios.hpp"
#include "pixelgba/HleInternal.hpp"
#include "pixelgba/Cartridge.hpp"
#include "pixelgba/Subsystems.hpp"
#include "pixelgba/GamePakPrefetch.hpp"
#include <cstdio>
#include <cstring>
#include <array>
#include <cstdint>
#include <string>
namespace pixelgba {
class GbaCore {
public:
    static constexpr unsigned Width=Ppu::Width,Height=Ppu::Height;
    GbaCore();
    ~GbaCore();
    bool configureSaveDirectory(const std::filesystem::path& path, std::string& error);
    bool flushSave(std::string& error);
    SaveMemory::Type saveType() const {return units_.save.type();}
    const std::filesystem::path& savePath() const {return savePath_;}
    const std::string& lastSaveError() const {return lastSaveError_;}
    bool loadCartridge(const std::filesystem::path&,std::string&);
    bool importBios(const std::filesystem::path&,const std::filesystem::path&,std::string&);
    bool loadInstalledBios(const std::filesystem::path&,std::string&);
    void unloadBios();
    void reset();
    // Self-contained in-memory emulation state; never includes ROM or BIOS data.
    // Video and RAM are compressed to keep an active rewind history affordable.
    struct State {
        Arm7tdmi cpu;
        Apu apu;
        DmaController dma;
        TimerController timers;
        InterruptController interrupts;
        SaveMemory save;
        CycleScheduler scheduler;
        std::array<std::array<std::int32_t,2>,2> affine{};
        std::vector<std::uint8_t> packedMemory;
        std::uint16_t keyInput=0x3ff;
        bool keyIrqLevel=false,hleIrqActive=false;
        unsigned line=0,lineCycles=0,renderedX=0;
        std::uint64_t frames=0,haltWrites=0,lastSaveFlushFrame=0;
        std::array<std::uint64_t,4> dmaBursts{};
        std::uint32_t hleIrqReturn=0,hleIrqStatus=0;
        std::array<std::uint32_t,5> hleSavedRegisters{};
        std::uint16_t hleWaitMask=0;
        std::uint64_t unsupportedHleSwis=0,hleIrqDispatches=0;
        std::uint32_t biosPrefetchLatch=0;
        bool biosExecutionActive=false;
        // ARM7TDMI three-stage instruction pipeline; needed for code that
        // modifies instructions already prefetched from IWRAM or VRAM.
        std::uint32_t pipePc=0,pipeCurrent=0,pipeNext=0;
        bool pipeThumb=false,pipeValid=false;
        std::array<std::uint32_t,4> dmaBusLatch{};
        bool dmaOpenBusValid=false;
        std::uint32_t dmaOpenBusValue=0;
        std::uint32_t serialCyclesRemaining=0;
        // Timer0 IRQ input synchronization, relative to the CPU clock.
        std::uint64_t timer0IrqAssertCycle=0;
        bool timer0IrqAsserted=false;
        // Deferred immediate-DMA arbitration state, for exact resume.
        std::uint64_t dmaPendingCpuCycles=0,dmaReadyCycle=0;
        // HBlank DMA requests sampled on the DISPSTAT edge, before their 2-clock grant.
        std::uint8_t hblankDmaPendingMask=0;
        bool hleHaltSwiPending=false;
        GamePakPrefetch::State gamePakPrefetch{};
        bool conservativeNextFetch=false,cartridgeDataReturn=false;
        // One CPU-clock delay while /1 timer enables become active.
        std::uint8_t timerStartDelayMask=0;
        // Tracks whether /1 timer has completed an increment since enable.
        // Needed to sample a CPU timer read at the preceding peripheral clock,
        // distinguishing the enable latch from a wrap to reload.
        std::uint8_t timerHasTickedMask=0;
        // Append-only pending external DMA ownership, independent of data copy.
        std::uint8_t dmaGamePakClaimPending=0;
        bool physicalOpcodeFetchIssued=false,gamePakDataBusOccupied=false,cpuPhysicalRomDataRead=false;
        unsigned cpuLastGamePakReadWidth=0;
        std::uint32_t hleFirmwareRomReturnTarget=0;
        unsigned hleFirmwareRomReturnWidth=0;
        std::size_t memoryBytes() const {return packedMemory.size()+save.size()+apu.pendingSamples()*4+sizeof(State);}
    };
    bool captureState(State& out) const;
    // Faster internal rewind snapshots; disk states retain their original format.
    bool captureRewindState(State& out) const;
    bool restoreState(const State& in);
    // Rewind branches may undo a cartridge write already flushed to disk.
    // Persist the restored backup at the next autosave or normal shutdown.
    void commitRewindBranch(){units_.save.markDirty();}
    void setButtons(const std::array<bool,10>&);
    void setAudioChannelMask(unsigned mask){units_.apu.setChannelMask(mask);}
    unsigned audioChannelMask()const{return units_.apu.channelMask();}
    std::uint8_t read8(std::uint32_t) const;
    std::uint16_t read16(std::uint32_t) const;
    std::uint32_t read32(std::uint32_t) const;
    void write8(std::uint32_t,std::uint8_t);
    void write16(std::uint32_t,std::uint16_t);
    void write32(std::uint32_t,std::uint32_t);
    // CPU stores to aligned EWRAM/IWRAM are side-effect-free: hardware
    // timing, bus ownership and open-bus updates are performed by the ARM7
    // caller before this store. Keep the ordinary write32 decoder for all
    // other regions and misaligned direct callers. This small helper is
    // intentionally visible to the interpreter for cross-TU inlining.
    inline void writeCpu32(std::uint32_t address,std::uint32_t value){
        if((address&3u)==0u){
            std::uint8_t* destination=nullptr;
            const unsigned region=address>>24u;
            if(region==2u)destination=ewram_.data()+(address&0x3ffffu);
            else if(region==3u)destination=iwram_.data()+(address&0x7fffu);
            if(destination){
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                std::memcpy(destination,&value,sizeof(value));
#else
                destination[0]=std::uint8_t(value);
                destination[1]=std::uint8_t(value>>8u);
                destination[2]=std::uint8_t(value>>16u);
                destination[3]=std::uint8_t(value>>24u);
#endif
                return;
            }
        }
        write32(address,value);
    }
    bool setRomCheatPatch16(std::uint32_t offset,std::uint16_t value){return cart_.setRomCheatPatch16(offset,value);}
    void clearRomCheatPatches(){cart_.clearRomCheatPatches();}
    // Suppress only framebuffer construction; hardware/scanline/audio timings
    // still advance normally. Useful for catching up after UI stalls.
    bool runFrame();
    unsigned step();
    // Called before instruction memory is read, so BIOS fetches bypass the
    // protection that applies to cartridge/IRQ code outside BIOS.
    void observeInstructionFetch(std::uint32_t pc,bool thumb);
    std::uint32_t fetchCpuOpcode(std::uint32_t pc,bool thumb);
    void noteCpuDataBusAccess(std::uint32_t address,bool write,unsigned width=0){
        // DMA can leave the last transferred word on the internal data bus.
        // A subsequent mapped CPU data read replaces that residue, unlike
        // an unmapped/open-bus read, which only samples the existing word.
        if(GamePakPrefetch::isRom(address)) {
            if(!write && width) cpuLastGamePakReadWidth_=width;
            gamePakPrefetch_.externalBusAccess();
            gamePakDataBusOccupied_=true;
            conservativeNextFetch_=true;
            cartridgeDataReturn_=true;
        }
        const unsigned region=address>>24u;
        if(region<=7u && region!=1u)dmaOpenBusValid_=false;
    }
    // CPU bus transfer latency (including first cycle), configurable by WAITCNT.
    unsigned cpuAccessCycles(std::uint32_t address,unsigned bytes,bool sequential=false,bool opcodeFetch=false) const;
    bool gamePakPrefetchEnabled() const;
    unsigned lastCpuGamePakReadWidth() const {return cpuLastGamePakReadWidth_;}
    unsigned cpuInstructionFetchCycles(std::uint32_t address,unsigned bytes,bool sequential);
    // Physical CPU instruction read occupies the Game Pak instruction port.
    // The grant is issued BEFORE advancing time, allowing DMA at that clock
    // to replace the bus ownership, unlike a logical pipeline decode.
    void noteCpuInstructionBusRead(std::uint32_t address,bool physical=true){
        if(GamePakPrefetch::isRom(address) && physical)
            dmaOpenBusValid_=false;
    }
    bool lastOpcodeFetchWasPhysical() const {return physicalOpcodeFetchIssued_;}
    void gamePakBranch(){gamePakPrefetch_.branch();}
    void primeBranchTargetPair(std::uint32_t target,unsigned width);
    // HLE executes firmware work in one host call. Prevent that simulated
    // firmware interval from prefetching guest cartridge instructions.
    void deferHleFirmwareRomReturn(std::uint32_t target,unsigned width){
        hleFirmwareRomReturnTarget_=target;hleFirmwareRomReturnWidth_=width;
    }
    // Instruction-phase scheduler: fetch clocks precede execution. CPU data
    // samples/writes use the leading latch edge, then spend the transfer's
    // clocks before a subsequent data access. Retirement spends the remainder.
    void beginCpuInstructionTiming();
    void cpuTimedBusPhase(unsigned cycles);
    void cpuDataBusPhase(unsigned cycles);
    // Cancel speculative wait clocks, but finish an already active final
    // prefetch transfer before a CPU data access takes the external bus.
    // Returns clocks already advanced; the CPU adds them to its bus budget.
    unsigned waitForCpuGamePakClaim(std::uint32_t address);
    bool canPhysicalRomReadReturn() const {return !dmaPendingCpuCycles_;}
    void notePhysicalRomDataRead(){cpuPhysicalRomDataRead_=true;}
    bool finishPhysicalRomDataReturn();
    void promoteRomReadFetchBeat();
    unsigned earlyRomFetchClocks() const {return cpuInstructionTimingActive_?cpuEarlyRomFetchClocks_:0u;}
    void finishCpuInstructionTiming(unsigned instructionCycles);
    // Optional BIOS-free startup path. Unsupported SWIs are reported instead
    // of pretending they succeeded; firmware-backed startup is preferred.
    unsigned handleHleSwi(unsigned number);
    std::uint64_t unsupportedHleSwis() const {return unsupportedHleSwis_;}
    std::uint64_t hleIrqDispatches() const {return hleIrqDispatches_;}
    void advanceHardware(unsigned cycles);
    const std::array<std::uint32_t,Width*Height>& framebuffer() const {return units_.ppu.pixels();}
    const Cartridge& cartridge() const {return cart_;}
    bool biosLoaded() const {return bios_.loaded();}
    std::uint32_t programCounter() const {return units_.cpu.r[15];}
    const Arm7tdmi& cpu() const {return units_.cpu;}
    Arm7tdmi& cpu() {return units_.cpu;}
    std::uint64_t cycles() const {return units_.scheduler.cycles();}
    std::uint64_t frames() const {return frames_;}
    std::vector<std::int16_t> takeAudioSamples(){return units_.apu.takeSamples();}
    unsigned audioFifoSize(unsigned ch) const{return units_.apu.fifoSize(ch);}
    int audioPcmLatch(unsigned ch) const{return units_.apu.pcmLatch(ch);}
    std::uint32_t dmaCurrentSource(unsigned ch) const{return ch<4?units_.dma.channels[ch].source:0;}
    std::uint64_t haltWrites() const {return haltWrites_;}
    const std::array<std::uint64_t,4>& dmaBursts() const {return dmaBursts_;}
    std::uint16_t scanline() const {return line_;}
private:
    bool captureStateInternal(State&,bool quickRewind) const;
    std::uint16_t readIo16(unsigned offset) const;
    bool ioWriteOnly(unsigned offset) const;
    bool ioZeroRead(unsigned offset) const;
    bool ioOpenBus(unsigned offset) const;
    void updateDisplayStatus(bool hblankEvent=false,bool vblankEvent=false);
    // Draw elapsed visible pixels before software changes PPU-visible registers/memory.
    void flushVisiblePixels(unsigned targetX);
    std::uint64_t dmaPendingCpuCycles_=0;
    std::uint64_t dmaReadyCycle_=0;
    std::uint8_t hblankDmaPendingMask_=0;
    unsigned dmaStepStallCycles_=0;
    void advanceCpuTime(unsigned cycles);
    void runDma(unsigned channel,unsigned timing);
    void advanceTimers(unsigned cycles);
    std::uint64_t timer0IrqAssertCycle_=0;
    bool timer0IrqAsserted_=false;
    void beginHleIrq();
    void finishHleIrq();
    void finishHleWait(std::uint16_t flags);
    void finishHleHalt();
    CipherGbaHleHost makeHleHost();
    static unsigned vramIndex(std::uint32_t address);
    Subsystems units_;
    std::array<std::uint8_t,256*1024> ewram_{};
    std::array<std::uint8_t,32*1024> iwram_{};
    std::array<std::uint8_t,1024> io_{};
    std::array<std::uint8_t,1024> palette_{};
    std::array<std::uint8_t,96*1024> vram_{};
    std::array<std::uint8_t,1024> oam_{};
    Bios bios_;
    std::uint32_t biosPrefetchLatch_=0;
    bool biosExecutionActive_=false;
    std::uint32_t pipePc_=0,pipeCurrent_=0,pipeNext_=0;
    bool pipeThumb_=false,pipeValid_=false;
    std::filesystem::path saveDirectory_, savePath_;
    std::string lastSaveError_;
    std::uint64_t lastSaveFlushFrame_=0;
    Cartridge cart_;
    std::uint16_t keyInput_=0x03ff;
    bool keyIrqLevel_=false;
    unsigned line_=0,lineCycles_=0,renderedX_=0;
    std::uint64_t frames_=0,haltWrites_=0;
    std::array<std::uint64_t,4> dmaBursts_{};
    std::array<std::uint32_t,4> dmaBusLatch_{};
    bool dmaOpenBusValid_=false;
    bool physicalOpcodeFetchIssued_=false;
    std::uint32_t dmaOpenBusValue_=0;
    std::uint32_t serialCyclesRemaining_=0;
    void advanceSerial(unsigned cycles);
    GamePakPrefetch gamePakPrefetch_{};
    bool gamePakDataBusOccupied_=false;
    bool conservativeNextFetch_=false;
    bool cartridgeDataReturn_=false;
    unsigned cpuLastGamePakReadWidth_=0;
    std::uint8_t timerStartDelayMask_=0;
    std::uint8_t timerHasTickedMask_=0;
    unsigned cpuEarlyRomFetchClocks_=0; // transient within an instruction
    std::uint8_t dmaGamePakClaimPending_=0;
    bool cpuPhysicalRomDataRead_=false; // transient, valid within one CPU step only
    bool cpuInstructionTimingActive_=false;
    std::uint32_t hleFirmwareRomReturnTarget_=0u;
    unsigned hleFirmwareRomReturnWidth_=0u;
    unsigned cpuInstructionCyclesSpent_=0;
    bool hleIrqActive_=false;
    bool hleHaltSwiPending_=false;
    std::uint32_t hleIrqReturn_=0,hleIrqStatus_=0;
    std::array<std::uint32_t,5> hleSavedRegisters_{};
    std::uint16_t hleWaitMask_=0;
    std::uint64_t unsupportedHleSwis_=0,hleIrqDispatches_=0;
};
}
