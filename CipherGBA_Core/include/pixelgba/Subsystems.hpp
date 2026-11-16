// Created by Jacob Hodgkins.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace pixelgba {
class GbaCore;
class Arm7tdmi {
public:
    std::array<std::uint32_t,16> r{};
    std::uint32_t cpsr=0xD3, spsr=0;
    bool halted=false;
    std::uint64_t executed=0, unsupported=0, cartridgeInstructions=0;
    std::uint64_t irqEntries=0,softwareInterrupts=0;
    std::uint32_t lastCartridgePc=0,lastSwiImmediate=0;
    // Counts CPU data-bus wait penalties separately from the ISA instruction costs.
    unsigned dataWaitPenalty_=0;
    std::uint32_t expectedFetchPc_=0;
    bool lastFetchThumb_=false,fetchValid_=false,lastStepHadDataAccess_=false, lastStepHadInternalRamAccess_=false;
    void reset(bool hasBios);
    unsigned stepInstruction(GbaCore& bus);
    void irq();
    void softwareInterrupt();
    void setCpsr(std::uint32_t value);
    bool thumb() const { return (cpsr & 0x20u)!=0; }
private:
    std::array<std::array<std::uint32_t,7>,6> banks_{};
    std::array<std::uint32_t,5> userHigh_{};
    std::array<std::uint32_t,5> fiqHigh_{};
    std::array<std::uint32_t,6> savedStatus_{};
    std::uint32_t operand(unsigned index, bool regShift=false) const;
    void exception(std::uint32_t vector, std::uint32_t mode, std::uint32_t lr);
    unsigned armStep(GbaCore& bus, std::uint32_t op, std::uint32_t pc);
    unsigned thumbStep(GbaCore& bus, std::uint16_t op, std::uint32_t pc);
    unsigned modeBank(unsigned mode) const;
};
class Ppu {
    friend class GbaCore;
public:
    static constexpr unsigned Width=240, Height=160;
    void reset();
    // GBA affine BG origin has a second, per-scanline hardware accumulator.
    void affineRegisterWritten(unsigned offset, const std::array<std::uint8_t,1024>& io);
    void latchAffine(const std::array<std::uint8_t,1024>& io);
    void advanceAffine(const std::array<std::uint8_t,1024>& io);
    const std::array<std::uint32_t,Width*Height>& pixels() const { return framebuffer_; }
    void renderLine(unsigned y, const std::array<std::uint8_t,1024>& io,
                    const std::array<std::uint8_t,1024>& palette,
                    const std::array<std::uint8_t,96*1024>& vram,
                    const std::array<std::uint8_t,1024>& oam);
    void renderSpan(unsigned y, unsigned beginX, unsigned endX,
                    const std::array<std::uint8_t,1024>& io,
                    const std::array<std::uint8_t,1024>& palette,
                    const std::array<std::uint8_t,96*1024>& vram,
                    const std::array<std::uint8_t,1024>& oam);
private:
    std::array<std::array<std::int32_t,2>,2> affineReference_{};
    std::array<std::uint32_t,Width*Height> framebuffer_{};
};
class Apu {
    friend struct StateFieldsAccess;
public:
    static constexpr unsigned SampleRate=32768;
    // Presentation-only: PSG square1/2, wave, noise, Direct Sound A/B.
    void reset();
    // Mix-only channel mask. Oscillators, FIFO DMA, frame sequencer remain clocked.
    void setChannelMask(unsigned mask){channelMask_=mask&0x3fu;invalidateMixCache();}
    unsigned channelMask() const{return channelMask_;}
    // Transient host-only cache; not part of the emulated state or save-state ABI.
    void invalidateMixCache() const;
    void write(unsigned offset,std::uint8_t value,const std::array<std::uint8_t,1024>& io);
    void advanceCycles(std::uint32_t,const std::array<std::uint8_t,1024>& io);
    void timerOverflow(unsigned timer,const std::array<std::uint8_t,1024>& io);
    bool fifoNeedsRefill(unsigned fifo) const;
    std::uint8_t readWave(unsigned offset,const std::array<std::uint8_t,1024>& io) const;
    std::uint8_t status() const;
    std::vector<std::int16_t> takeSamples();
    std::size_t pendingSamples() const {return samples_.size()/2;}
    std::uint64_t pwmTicks() const {return pwmTicks_;}
    unsigned cyclesUntilSample() const {return 512u-samplePhase_;}
    unsigned fifoSize(unsigned channel) const {return channels_[channel].size;}
    int pcmLatch(unsigned channel) const {return channel<2?channels_[channel].sample:0;}
    unsigned squareDutyStep(unsigned channel) const {return channel<2?squares_[channel].step:0;}
private:
    struct Fifo {
        std::array<std::int8_t,32> bytes{};
        unsigned rd=0,size=0;
        std::int8_t sample=0;
    };
    struct Envelope {
        unsigned volume=0,period=0,timer=0;
        bool increase=false,running=false;
    };
    struct Square {
        unsigned length=0,frequency=0,step=0,timer=0;
        bool enabled=false;
        Envelope env{};
    };
    struct Sweep {
        unsigned shadow=0,period=0,timer=0,shift=0;
        bool enabled=false,negate=false,negated=false;
    };
    struct Wave {
        unsigned length=0,frequency=0,position=0,timer=0;
        bool enabled=false;
    };
    struct Noise {
        unsigned timer=0,length=0;
        std::uint16_t lfsr=0x7fff;
        bool enabled=false;
        Envelope env{};
    };
    std::array<Fifo,2> channels_{};
    std::array<Square,2> squares_{};
    Sweep sweep_{};
    Wave wave_{};
    Noise noise_{};
    std::array<std::array<std::uint8_t,16>,2> waveRam_{};
    std::vector<std::int16_t> samples_{};
    std::array<double,2> dcInput_{}, dcOutput_{};
    std::array<std::int64_t,2> mixIntegral_{};
    std::array<std::int64_t,2> pwmSum_{};
    unsigned pwmPhase_=0,pwmWindows_=0;
    std::uint64_t pwmTicks_=0;
    std::uint32_t samplePhase_=0,framePhase_=0;
    unsigned frameStep_=0;
    bool masterEnabled_=false;
    unsigned channelMask_=0x3fu;
    static unsigned squarePeriod(unsigned frequency);
    static unsigned wavePeriod(unsigned frequency);
    static unsigned noisePeriod(std::uint8_t control);
    static void envelopeTrigger(Envelope&,std::uint8_t);
    static void envelopeTick(Envelope&);
    void triggerSquare(unsigned,const std::array<std::uint8_t,1024>&);
    void triggerWave(const std::array<std::uint8_t,1024>&);
    void triggerNoise(const std::array<std::uint8_t,1024>&);
    int sweepCalculate() const;
    void frameSequencer(const std::array<std::uint8_t,1024>& io);
    void stepOscillators(unsigned,const std::array<std::uint8_t,1024>& io);
    std::array<int,2> mixLevels(const std::array<std::uint8_t,1024>& io) const;
    std::array<int,2> cachedMixLevels(const std::array<std::uint8_t,1024>& io) const;
    void producePwmStep(const std::array<std::uint8_t,1024>& io,unsigned period);
    void produceSample(const std::array<std::uint8_t,1024>& io);
    void disableMaster();

};
class DmaController { public: struct Channel {
    // Latched active bus pointers are distinct from software-visible registers.
    std::uint32_t source=0,destination=0,initialDestination=0,count=0,control=0;
}; std::array<Channel,4> channels{}; void reset(); };
class TimerController { public: struct Timer {std::uint16_t counter=0,reload=0,control=0; std::uint32_t phase=0;}; std::array<Timer,4> timers{}; void reset(); };
class InterruptController { public: std::uint16_t interruptEnable=0,interruptFlags=0; bool masterEnable=false; void reset(); void request(std::uint16_t); bool pending() const; };
class SaveMemory {
    friend struct StateFieldsAccess;
public:
    enum class Type { SRAM, Flash64, Flash128, EEPROM512, EEPROM8K };
    SaveMemory();
    void configure(Type type);
    Type type() const { return type_; }
    std::uint8_t read(std::uint32_t) const;
    void write(std::uint32_t,std::uint8_t);
    std::uint16_t readEeprom();
    void writeEeprom(std::uint16_t bit);
    void setEepromAddressBits(unsigned bits);
    void eraseForNewCartridge();
    bool save(const std::filesystem::path&,std::string&);
    bool load(const std::filesystem::path&,std::string&);
    bool dirty() const {return dirty_;}
    void markDirty(){dirty_=true;}
    std::size_t size() const {return raw_.size();}
private:
    enum class FlashState { Idle, Unlock1, Unlock2, Program, Erase1, Erase2, Erase3, Bank };
    Type type_=Type::SRAM;
    std::vector<std::uint8_t> raw_;
    FlashState flash_=FlashState::Idle;
    bool identification_=false;
    unsigned bank_=0;
    bool dirty_=false;
    unsigned eepromAddressBits_=6;
    std::vector<unsigned> serial_;
    std::vector<unsigned> response_;
    std::size_t responseAt_=0;
};
class CycleScheduler { friend struct StateFieldsAccess; public: void reset(); void advance(unsigned n) {cycles_+=n;} std::uint64_t cycles() const {return cycles_;} private: std::uint64_t cycles_=0;};
struct Subsystems { Arm7tdmi cpu; Ppu ppu; Apu apu; DmaController dma; TimerController timers; InterruptController interrupts; SaveMemory save; CycleScheduler scheduler; void reset(bool hasBios); };
} // namespace pixelgba
