// Created by Jacob Hodgkins.
#include "pixelgba/Subsystems.hpp"
#include "pixelgba/AtomicReplace.hpp"
#include <algorithm>
#include <fstream>
#include <cerrno>
#include <cstring>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
namespace pixelgba {
namespace {
// This memo contains only a derived instantaneous analog mix. It is NOT
// hardware state, and never appears in save states or rewind snapshots.
// One small entry per host thread avoids allocations and changes to Apu's ABI.
struct ApuMixMemo {
    const Apu* owner=nullptr;
    std::array<int,2> levels{};
    bool valid=false;
};
// For a locally built executable which links the core at startup, use
// initial-exec TLS for this 24-byte hot cache. Generic shared-library builds
// retain dynamic TLS so they remain safe for arbitrary late dlopen hosts.
#if defined(CIPHERGBA_APU_FAST_TLS) && defined(__GNUC__) && defined(__ELF__)
thread_local __attribute__((tls_model("initial-exec"))) ApuMixMemo gMixMemo;
#else
thread_local ApuMixMemo gMixMemo;
#endif
}
void Apu::invalidateMixCache() const {
    if(gMixMemo.owner==this)gMixMemo.valid=false;
}
std::array<int,2> Apu::cachedMixLevels(const std::array<std::uint8_t,1024>& io) const {
    if(gMixMemo.owner!=this || !gMixMemo.valid){
        gMixMemo.owner=this;
        gMixMemo.levels=mixLevels(io);
        gMixMemo.valid=true;
    }
    return gMixMemo.levels;
}
void Ppu::reset(){framebuffer_.fill(0);affineReference_={};}
// GBA audio engine: four clocked PSG voices, two timer-fed FIFO DACs,
// 512 Hz frame sequencer, 32,768 Hz host samples and integrated downsampling.
// Created by Jacob Hodgkins.
void Apu::reset() {
    invalidateMixCache();
    channels_={};squares_={};sweep_={};wave_={};noise_={};waveRam_={};
    samples_.clear();dcInput_={};dcOutput_={};mixIntegral_={};pwmSum_={};
    pwmPhase_=pwmWindows_=0;pwmTicks_=0;
    samplePhase_=framePhase_=frameStep_=0;masterEnabled_=false;
}
void Apu::disableMaster(){
    invalidateMixCache();
    squares_={};sweep_={};wave_={};noise_={};channels_={};
    framePhase_=frameStep_=0;masterEnabled_=false;
}
std::uint8_t Apu::status() const {
    return std::uint8_t((squares_[0].enabled?1u:0u)|(squares_[1].enabled?2u:0u)|
        (wave_.enabled?4u:0u)|(noise_.enabled?8u:0u));
}
std::uint8_t Apu::readWave(unsigned off,const std::array<std::uint8_t,1024>& io) const {
    // CPU sees the nonplaying bank (NR30 bit 6 selects playback bank).
    return waveRam_[((io[0x70]>>6)&1u)^1u][off-0x90];
}
unsigned Apu::squarePeriod(unsigned frequency){return 16u*(2048u-(frequency&2047u));}
unsigned Apu::wavePeriod(unsigned frequency){return 8u*(2048u-(frequency&2047u));}
unsigned Apu::noisePeriod(std::uint8_t control){
    const unsigned div=control&7u,shift=(control>>4)&15u;
    return (div?div*32u:16u)<<(shift+1u);
}
void Apu::envelopeTrigger(Envelope& e,std::uint8_t v){
    e.volume=(v>>4)&15u;e.period=v&7u;e.increase=(v&8u)!=0;
    e.timer=e.period?e.period:8u;e.running=true;
}
void Apu::envelopeTick(Envelope& e){
    if(!e.running)return;
    if(--e.timer)return;
    e.timer=e.period?e.period:8u;
    if(!e.period)return;
    if(e.increase){if(e.volume<15)++e.volume;else e.running=false;}
    else {if(e.volume>0)--e.volume;else e.running=false;}
}
int Apu::sweepCalculate() const {
    const unsigned difference=sweep_.shadow>>sweep_.shift;
    return sweep_.negate?int(sweep_.shadow)-int(difference):int(sweep_.shadow)+int(difference);
}
void Apu::triggerSquare(unsigned index,const std::array<std::uint8_t,1024>& io){
    auto& c=squares_[index];
    const unsigned env=index?0x69:0x63,frequency=index?0x6c:0x64;
    c.enabled=(io[env]&0xf8u)!=0u;
    if(!c.length)c.length=64;
    c.frequency=unsigned(io[frequency])|((unsigned(io[frequency+1])&7u)<<8);
    c.timer=squarePeriod(c.frequency);
    // Retrigger reloads the frequency timer/envelope, not the pulse duty
    // sequencer position. Only a master-sound reset clears its phase.
    envelopeTrigger(c.env,io[env]);
    if(index==0){
        sweep_.shadow=c.frequency;
        sweep_.period=(io[0x60]>>4)&7u;
        sweep_.timer=sweep_.period?sweep_.period:8u;
        sweep_.shift=io[0x60]&7u;
        sweep_.negate=(io[0x60]&8u)!=0u;
        sweep_.negated=false;
        sweep_.enabled=(sweep_.period!=0 || sweep_.shift!=0);
        if(sweep_.shift && sweepCalculate()>2047)c.enabled=false;
    }
}
void Apu::triggerWave(const std::array<std::uint8_t,1024>& io){
    wave_.enabled=(io[0x70]&0x80u)!=0;
    if(!wave_.length)wave_.length=256;
    wave_.frequency=unsigned(io[0x74])|((unsigned(io[0x75])&7u)<<8);
    wave_.position=0;wave_.timer=wavePeriod(wave_.frequency);
}
void Apu::triggerNoise(const std::array<std::uint8_t,1024>& io){
    noise_.enabled=(io[0x79]&0xf8u)!=0;
    if(!noise_.length)noise_.length=64;
    noise_.lfsr=0x7fff;
    noise_.timer=noisePeriod(io[0x7c]);
    envelopeTrigger(noise_.env,io[0x79]);
}
void Apu::write(unsigned offset,std::uint8_t value,const std::array<std::uint8_t,1024>& io){
    invalidateMixCache();
    if(offset>=0x90&&offset<=0x9f){
        waveRam_[((io[0x70]>>6)&1u)^1u][offset-0x90]=value;
        return;
    }
    if(offset>=0xa0&&offset<=0xa7){
        if(!masterEnabled_)return;
        auto& f=channels_[(offset>=0xa4)?1:0];
        if(f.size<32){f.bytes[(f.rd+f.size)&31u]=static_cast<std::int8_t>(value);++f.size;}
        return;
    }
    if(offset==0x83){
        if(value&0x08u)channels_[0]={};
        if(value&0x80u)channels_[1]={};
    }
    if(offset==0x84){
        if(!(value&0x80u))disableMaster();
        else if(!masterEnabled_){
            squares_={};sweep_={};wave_={};noise_={};channels_={};
            framePhase_=frameStep_=0;masterEnabled_=true;
        }
        return;
    }
    if(!masterEnabled_)return;
    if(offset==0x62)squares_[0].length=64u-(value&63u);
    if(offset==0x68)squares_[1].length=64u-(value&63u);
    if(offset==0x72)wave_.length=256u-value;
    if(offset==0x78)noise_.length=64u-(value&63u);
    if(offset==0x63&&!(value&0xf8u))squares_[0].enabled=false;
    if(offset==0x69&&!(value&0xf8u))squares_[1].enabled=false;
    if(offset==0x79&&!(value&0xf8u))noise_.enabled=false;
    if(offset==0x70&&!(value&0x80u))wave_.enabled=false;
    if(offset==0x60){
        const bool oldNegate=sweep_.negate;
        sweep_.negate=(value&8u)!=0;
        if(oldNegate&&!sweep_.negate&&sweep_.negated)squares_[0].enabled=false;
        sweep_.shift=value&7u;sweep_.period=(value>>4)&7u;
    }
    if(offset==0x64||offset==0x65){
        if(squares_[0].enabled)squares_[0].frequency=unsigned(io[0x64])|((unsigned(io[0x65])&7u)<<8);
    }
    if(offset==0x6c||offset==0x6d){
        if(squares_[1].enabled)squares_[1].frequency=unsigned(io[0x6c])|((unsigned(io[0x6d])&7u)<<8);
    }
    if(offset==0x74||offset==0x75){
        if(wave_.enabled)wave_.frequency=unsigned(io[0x74])|((unsigned(io[0x75])&7u)<<8);
    }
    if(offset==0x65&&(value&0x80u))triggerSquare(0,io);
    if(offset==0x6d&&(value&0x80u))triggerSquare(1,io);
    if(offset==0x75&&(value&0x80u))triggerWave(io);
    if(offset==0x7d&&(value&0x80u))triggerNoise(io);
}
void Apu::timerOverflow(unsigned timer,const std::array<std::uint8_t,1024>& io){
    invalidateMixCache();
    if(!masterEnabled_)return;
    for(unsigned i=0;i<2;i++){
        if(((io[0x83]>>(i?6u:2u))&1u)!=timer)continue;
        auto& f=channels_[i];
        if(f.size){f.sample=f.bytes[f.rd];f.rd=(f.rd+1)&31u;--f.size;}
        // Real DAC latches its most recent sample on FIFO underrun.
    }
}
bool Apu::fifoNeedsRefill(unsigned fifo) const {
    return masterEnabled_ && fifo<2 && channels_[fifo].size<=16;
}
void Apu::frameSequencer(const std::array<std::uint8_t,1024>& io){
    invalidateMixCache();
    if((frameStep_&1u)==0u){
        for(unsigned i=0;i<2;i++){
            auto& c=squares_[i];
            if((io[i?0x6d:0x65]&0x40u) && c.length && --c.length==0)c.enabled=false;
        }
        if((io[0x75]&0x40u)&&wave_.length&&--wave_.length==0)wave_.enabled=false;
        if((io[0x7d]&0x40u)&&noise_.length&&--noise_.length==0)noise_.enabled=false;
    }
    if(frameStep_==2||frameStep_==6){
        if(sweep_.enabled){
            if(sweep_.timer)--sweep_.timer;
            if(!sweep_.timer){
                sweep_.timer=sweep_.period?sweep_.period:8;
                if(sweep_.shift && squares_[0].enabled){
                    const int next=sweepCalculate();
                    if(next>2047||next<0)squares_[0].enabled=false;
                    else {
                        if(sweep_.negate)sweep_.negated=true;
                        sweep_.shadow=unsigned(next);
                        squares_[0].frequency=unsigned(next);
                        if(sweepCalculate()>2047)squares_[0].enabled=false;
                    }
                }
            }
        }
    }
    if(frameStep_==7){
        for(auto& sq:squares_)if(sq.enabled)envelopeTick(sq.env);
        if(noise_.enabled)envelopeTick(noise_.env);
    }
    frameStep_=(frameStep_+1u)&7u;
}
void Apu::stepOscillators(unsigned n,const std::array<std::uint8_t,1024>& io){
    for(auto& sq:squares_)if(sq.enabled){
        sq.timer-=n;
        if(!sq.timer){invalidateMixCache();sq.timer=squarePeriod(sq.frequency);sq.step=(sq.step+1u)&7u;}
    }
    if(wave_.enabled){
        wave_.timer-=n;
        if(!wave_.timer){
            invalidateMixCache();
            wave_.timer=wavePeriod(wave_.frequency);
            wave_.position=(wave_.position+1u)&((io[0x70]&0x20u)?63u:31u);
        }
    }
    if(noise_.enabled){
        noise_.timer-=n;
        if(!noise_.timer){
            invalidateMixCache();
            noise_.timer=noisePeriod(io[0x7c]);
            const unsigned bit=(noise_.lfsr^(noise_.lfsr>>1u))&1u;
            noise_.lfsr=std::uint16_t((noise_.lfsr>>1u)|(bit<<14u));
            if(io[0x7c]&8u)noise_.lfsr=std::uint16_t((noise_.lfsr&~(1u<<6u))|(bit<<6u));
        }
    }
}
std::array<int,2> Apu::mixLevels(const std::array<std::uint8_t,1024>& io) const {
    // The GBA combines all six channels in units of its 10-bit DAC, before
    // adding SOUNDBIAS. See mGBA src/gba/audio.c GBAAudioSample(), and
    // src/gb/audio.c GBAudioSamplePSG(). There is no independent arbitrary
    // "PSG=100 / DirectSound=96" amplification stage.
    std::array<int,2> result{}; // indices: 0=left, 1=right
    if (!masterEnabled_)return result;
    const unsigned psgRatio=io[0x82]&3u;
    if(psgRatio<3u){
        // Hardware PSG DAC levels are *unipolar*: silent/low state is zero.
        // AC coupling later removes this offset. Feeding signed bipolar
        // signals here changes saturation and the relative instrument levels.
        static constexpr std::uint8_t dutyMasks[4]={0x01,0x81,0x87,0x7e};
        std::array<int,4> voices{};
        for(unsigned i=0;i<2;i++){
            const auto& c=squares_[i];
            if(c.enabled){
                const unsigned pattern=(io[i?0x68:0x62]>>6)&3u;
                const bool high=(dutyMasks[pattern]>>c.step)&1u;
                voices[i]=high?int(c.env.volume):0;
            }
        }
        if(wave_.enabled){
            const unsigned selected=(io[0x70]>>6)&1u;
            const unsigned pos=wave_.position;
            const unsigned bank=selected^(pos>=32u?1u:0u);
            const std::uint8_t packed=waveRam_[bank][(pos&31u)>>1u];
            const int nibble=(pos&1u)?int(packed&15u):int(packed>>4u);
            const unsigned volume=(io[0x73]>>5)&3u;
            voices[2]=(io[0x73]&0x80u)?nibble*3/4:
                (volume==1?nibble:(volume==2?nibble/2:(volume==3?nibble/4:0)));
        }
        if(noise_.enabled)voices[3]=(noise_.lfsr&1u)?0:int(noise_.env.volume);
        int left=0,right=0;
        for(unsigned i=0;i<4;i++){
            if(!(channelMask_&(1u<<i)))continue;
            if(io[0x81]&(1u<<i))right+=voices[i];
            if(io[0x81]&(1u<<(i+4)))left+=voices[i];
        }
        // NR50 volume (1..8), PSG internal x8, and SOUNDCNT_H
        // PSG ratio (1/4,1/2,1) are all applied before mixing PCM.
        const unsigned shift=4u-psgRatio;
        result[0]+=(left*8*int(((io[0x80]>>4)&7u)+1u))>>shift;
        result[1]+=(right*8*int((io[0x80]&7u)+1u))>>shift;
    }
    for(unsigned i=0;i<2;i++){
        if(!(channelMask_&(1u<<(i+4u))))continue;
        // SOUNDCNT_H bit2/3: 0=50%, 1=100%. Each signed PCM byte
        // contributes up to +/-512 units of the 10-bit DAC (full volume).
        const int pcm=int(channels_[i].sample)*((io[0x82]&(1u<<(2u+i)))?4:2);
        if(io[0x83]&(1u<<(i*4u)))result[1]+=pcm;
        if(io[0x83]&(1u<<(i*4u+1u)))result[0]+=pcm;
    }
    return result;
}
void Apu::producePwmStep(const std::array<std::uint8_t,1024>& io,unsigned period){
    const int bias=int(unsigned(io[0x88])|(unsigned(io[0x89]&3u)<<8u));
    const unsigned resolution=(io[0x89]>>6u)&3u;
    const unsigned mask=(1u<<(resolution+1u))-1u;
    for(unsigned side=0;side<2;side++){
        const int level=int(mixIntegral_[side]/period);
        const int dac=std::clamp(bias+level,0,1023);
        // Quantize the actual DAC value, then subtract the programmable
        // bias *before* high-pass filtering. A non-default SOUNDBIAS must
        // not inject a permanent synthetic 512-bias DC signal.
        pwmSum_[side]+=(dac&~int(mask))-bias;
    }
    ++pwmWindows_;++pwmTicks_;
    mixIntegral_={};
}
void Apu::produceSample(const std::array<std::uint8_t,1024>&){
    if(samples_.size()+2>SampleRate*2){pwmSum_={};pwmWindows_=0;return;}
    for(unsigned side=0;side<2;side++){
        const double delta=pwmWindows_?double(pwmSum_[side])/pwmWindows_:0.0;
        const double v=delta*48.0; // Matches mGBA default volume gain (0x100 * 3 / 16).
        dcOutput_[side]=v-dcInput_[side]+0.996*dcOutput_[side];
        dcInput_[side]=v;
        samples_.push_back(static_cast<std::int16_t>(std::clamp(int(dcOutput_[side]),-32768,32767)));
    }
    pwmSum_={};pwmWindows_=0;
}
void Apu::advanceCycles(std::uint32_t n,const std::array<std::uint8_t,1024>& io){
    // Common case: small CPU bus phases that do not cross *any* audio
    // event. It is valid for both PSG and DirectSound output. Checking
    // this before the more specialized silent/DAC cases avoids testing
    // six unused states on every ordinary Thumb instruction; the existing
    // code below still owns every event boundary and timer FIFO sample.
    // Most CPU bus phases do not cross a PSG transition, DAC sample, PWM
    // boundary or frame-sequencer edge. Advance this entire interval in one
    // exact step, avoiding the generic event-queue and stepOscillators calls.
    // Strict inequalities defer boundary handling to the unchanged scheduler.
    // The phase modulus is required after a mid-window SOUNDBIAS write.
    if(n){
        const unsigned pwmPeriod=512u>>((io[0x89]>>6u)&3u);
        pwmPhase_&=pwmPeriod-1u;
        if(n<512u-samplePhase_ && n<32768u-framePhase_ &&
           n<pwmPeriod-pwmPhase_ &&
           (!squares_[0].enabled || n<squares_[0].timer) &&
           (!squares_[1].enabled || n<squares_[1].timer) &&
           (!wave_.enabled || n<wave_.timer) &&
           (!noise_.enabled || n<noise_.timer)){
            const auto mix=cachedMixLevels(io);
            mixIntegral_[0]+=std::int64_t(mix[0])*n;
            mixIntegral_[1]+=std::int64_t(mix[1])*n;
            if(squares_[0].enabled)squares_[0].timer-=n;
            if(squares_[1].enabled)squares_[1].timer-=n;
            if(wave_.enabled)wave_.timer-=n;
            if(noise_.enabled)noise_.timer-=n;
            samplePhase_+=n;
            framePhase_+=n;
            pwmPhase_+=n;
            return;
        }
    }
    // Cold-start fast path. During the BIOS boot the APU master is off and
    // SOUNDBIAS sits at its neutral value. No oscillators or PCM are running;
    // preserve the exact PWM/sample/frame phases without running the complete
    // mixer for every CPU instruction. Fall back immediately on *any* change
    // to the analog state or mixer registers. The core supplies segments
    // ending at or before the next output-sample boundary.
    // Many games enable sound long before playing their first note. The
    // silent-but-enabled case can advance PWM/sample clocks without repeatedly
    // evaluating six mixers per CPU instruction. Frame-sequencer events are
    // still processed at their exact boundary (including disabled lengths).
    // DC filters may still be decaying from a previous note: produceSample()
    // is called at exactly the same boundaries and continues that decay.
    // Neither zero input nor the neutral DAC bias depends on filter history.
    const bool silentDac=!masterEnabled_ ||
        (!squares_[0].enabled && !squares_[1].enabled && !wave_.enabled &&
         !noise_.enabled && channels_[0].sample==0 && channels_[1].sample==0);
    if(silentDac && n<=512u-samplePhase_ &&
       (!masterEnabled_ || n<=32768u-framePhase_) &&
       io[0x88]==0 && (io[0x89]&3u)==2u &&
       mixIntegral_[0]==0 && mixIntegral_[1]==0 &&
       pwmSum_[0]==0 && pwmSum_[1]==0){
        // PWM periods are exactly 512, 256, 128 or 64 cycles. Dividing
        // by the variable period incurs an integer divide on this frequent
        // silent-DAC path; shifts/masks give identical unsigned results.
        const unsigned periodShift=9u-((io[0x89]>>6u)&3u);
        const unsigned period=1u<<periodShift;
        pwmPhase_&=period-1u;
        const unsigned elapsed=pwmPhase_+n;
        const unsigned ticks=elapsed>>periodShift;
        pwmPhase_=elapsed&(period-1u);
        pwmTicks_+=ticks;
        pwmWindows_+=ticks;
        // Neutral 0x200 DAC bias produces zero AC-coupled signal.
        framePhase_+=n;
        if(framePhase_==32768u){
            framePhase_=0;
            if(masterEnabled_)frameSequencer(io);
        }
        samplePhase_+=n;
        if(samplePhase_==512u){samplePhase_=0;produceSample(io);}
        return;
    }
    // DirectSound-only playback is common in commercial games. When all PSG
    // oscillators are idle and this CPU step crosses no PWM, output-sample, or
    // frame-sequencer boundary, the mixer result is invariant throughout the
    // interval. Integrate it once rather than executing the generic per-event
    // scheduler. Never skip a timer FIFO update: the GbaCore caller already
    // splits work at hardware timer boundaries.
    if(!squares_[0].enabled && !squares_[1].enabled &&
       !wave_.enabled && !noise_.enabled){
        const unsigned pwmPeriod=512u>>((io[0x89]>>6u)&3u);
        pwmPhase_&=pwmPeriod-1u;
        if(n<=pwmPeriod-pwmPhase_ && n<=512u-samplePhase_ &&
           n<=32768u-framePhase_){
            const auto mix=cachedMixLevels(io);
            mixIntegral_[0]+=std::int64_t(mix[0])*n;
            mixIntegral_[1]+=std::int64_t(mix[1])*n;
            samplePhase_+=n;framePhase_+=n;pwmPhase_+=n;
            if(pwmPhase_==pwmPeriod){pwmPhase_=0;producePwmStep(io,pwmPeriod);}
            if(framePhase_==32768u){framePhase_=0;if(masterEnabled_)frameSequencer(io);}
            if(samplePhase_==512u){samplePhase_=0;produceSample(io);}
            return;
        }
    }
    while(n){
        const unsigned pwmPeriod=512u>>((io[0x89]>>6u)&3u);
        // An application may change PWM resolution in the middle of a
        // window. Retain phase in the new clock's period.
        pwmPhase_&=pwmPeriod-1u;
        unsigned step=std::min({n,512u-samplePhase_,32768u-framePhase_,pwmPeriod-pwmPhase_});
        for(const auto& s:squares_)if(s.enabled)step=std::min(step,s.timer);
        if(wave_.enabled)step=std::min(step,wave_.timer);
        if(noise_.enabled)step=std::min(step,noise_.timer);
        // Core normally invokes this at intervals <=32 CPU cycles. The
        // segmentation below also handles callers that pass large intervals.
        if(!step)step=1;
        const auto mix=cachedMixLevels(io);
        for(unsigned i=0;i<2;i++)mixIntegral_[i]+=std::int64_t(mix[i])*step;
        stepOscillators(step,io);
        samplePhase_+=step;framePhase_+=step;pwmPhase_+=step;n-=step;
        if(pwmPhase_==pwmPeriod){pwmPhase_=0;producePwmStep(io,pwmPeriod);}
        if(framePhase_==32768){framePhase_=0;if(masterEnabled_)frameSequencer(io);}
        if(samplePhase_==512){samplePhase_=0;produceSample(io);}
    }
}
std::vector<std::int16_t> Apu::takeSamples(){
    std::vector<std::int16_t> v;v.swap(samples_);return v;
}
void DmaController::reset(){channels.fill({});}
void TimerController::reset(){timers.fill({});}
void InterruptController::reset(){interruptEnable=interruptFlags=0;masterEnable=false;}
void InterruptController::request(std::uint16_t v){interruptFlags|=v;}
bool InterruptController::pending() const {return masterEnable && (interruptEnable&interruptFlags)!=0;}
SaveMemory::SaveMemory(){configure(Type::SRAM);}
void SaveMemory::configure(Type type){
    type_=type;
    const std::size_t bytes=(type==Type::Flash128?131072:(type==Type::EEPROM512?512:(type==Type::EEPROM8K?8192:(type==Type::SRAM?32768:65536))));
    raw_.assign(bytes,0xff);
    flash_=FlashState::Idle;identification_=false;bank_=0;dirty_=false;
    eepromAddressBits_=type==Type::EEPROM8K?14:6;
    serial_.clear();response_.clear();responseAt_=0;
}
void SaveMemory::eraseForNewCartridge(){configure(Type::SRAM);}
std::uint8_t SaveMemory::read(std::uint32_t a) const {
    if(type_==Type::EEPROM512||type_==Type::EEPROM8K)return 0xff;
    if(identification_){
        if((a&0xffffu)==0)return type_==Type::Flash128?0x62:0x32; // Sanyo / Panasonic vendor IDs
        if((a&0xffffu)==1)return type_==Type::Flash128?0x13:0x1b;
    }
    const std::size_t offset=((a&0xffffu)+(type_==Type::Flash128?bank_*65536u:0u))%raw_.size();
    return raw_[offset];
}
void SaveMemory::write(std::uint32_t a,std::uint8_t v){
    if(type_==Type::EEPROM512||type_==Type::EEPROM8K)return;
    if(type_==Type::SRAM){
        auto& byte=raw_[a&0x7fffu];
        if(byte!=v){byte=v;dirty_=true;}
        return;
    }
    const unsigned addr=a&0xffffu;
    switch(flash_){
    case FlashState::Program:
        raw_[(addr+bank_*65536u)%raw_.size()] &= v;
        dirty_=true;flash_=FlashState::Idle;return;
    case FlashState::Bank:
        if(addr==0 && type_==Type::Flash128)bank_=v&1u;
        flash_=FlashState::Idle;return;
    case FlashState::Erase3:
        if(addr==0x5555&&v==0x10){std::fill(raw_.begin(),raw_.end(),0xff);dirty_=true;}
        else if(v==0x30){auto start=(bank_*65536u)+(addr&0xf000u);std::fill(raw_.begin()+start,raw_.begin()+start+4096,0xff);dirty_=true;}
        flash_=FlashState::Idle;return;
    case FlashState::Erase2:flash_=(addr==0x2aaa&&v==0x55)?FlashState::Erase3:FlashState::Idle;return;
    case FlashState::Erase1:flash_=(addr==0x5555&&v==0xaa)?FlashState::Erase2:FlashState::Idle;return;
    case FlashState::Unlock2:
        flash_=FlashState::Idle;
        if(addr==0x5555){
            switch(v){case 0x90:identification_=true;break;case 0xf0:identification_=false;break;
                case 0xa0:flash_=FlashState::Program;break;case 0x80:flash_=FlashState::Erase1;break;
                case 0xb0:flash_=FlashState::Bank;break;default:break;}
        }
        return;
    case FlashState::Unlock1:flash_=(addr==0x2aaa&&v==0x55)?FlashState::Unlock2:FlashState::Idle;return;
    case FlashState::Idle:
        if(addr==0x5555&&v==0xaa)flash_=FlashState::Unlock1;
        else if(v==0xf0)identification_=false;
        return;
    }
}
void SaveMemory::setEepromAddressBits(unsigned bits){
    if(bits!=6&&bits!=14)return;
    if(type_==Type::EEPROM8K && bits==6){
        // Some older PixelGBA versions wrongly expanded a 512 B EEPROM on a
        // 68-halfword readback. A later *actual* 9/73-halfword write command
        // identifies the real 6-bit protocol. Shrink only if no meaningful
        // data resides past the 512-byte boundary; never silently erase data.
        if(!std::all_of(raw_.begin()+512,raw_.end(),[](std::uint8_t v){return v==0xff;}))
            return;
        raw_.resize(512);
        type_=Type::EEPROM512;
        dirty_=true; // The on-disk file must be migrated to 512 bytes.
    }
    eepromAddressBits_=bits;
    if(bits==14 && type_==Type::EEPROM512){type_=Type::EEPROM8K;raw_.resize(8192,0xff);}
}
void SaveMemory::writeEeprom(std::uint16_t bit){
    if(type_!=Type::EEPROM512&&type_!=Type::EEPROM8K)return;
    // Idle zeros are not valid command start bits. Discard them immediately,
    // including DMA padding, rather than letting them consume a later command.
    if(serial_.empty() && !(bit&1u))return;
    serial_.push_back(bit&1u);
    if(serial_.size()<2+eepromAddressBits_+1)return;
    if(serial_[0]!=1){serial_.clear();return;}
    const bool read=serial_[1]!=0;
    const std::size_t required=2+eepromAddressBits_+(read?1:65);
    if(serial_.size()<required)return;
    unsigned address=0;
    for(unsigned i=0;i<eepromAddressBits_;i++)address=(address<<1)|serial_[2+i];
    address=(address*8u)%raw_.size();
    if(read){
        response_.assign(4,0);
        for(unsigned i=0;i<8;i++)for(int b=7;b>=0;b--)response_.push_back((raw_[address+i]>>b)&1);
        responseAt_=0;
    }else{
        for(unsigned i=0;i<64;i++){
            auto& byte=raw_[address+i/8];
            if(i%8==0)byte=0;
            byte|=static_cast<std::uint8_t>(serial_[2+eepromAddressBits_+i]<<(7-(i%8)));
        }
        dirty_=true;
    }
    serial_.clear();
}
std::uint16_t SaveMemory::readEeprom(){
    if(type_!=Type::EEPROM512&&type_!=Type::EEPROM8K)return 1;
    if(responseAt_<response_.size())return static_cast<std::uint16_t>(response_[responseAt_++]);
    return 1;
}
bool SaveMemory::save(const std::filesystem::path& path,std::string& err){
    // Write to a sibling temp file and atomically replace the previous save.
    // A short write, I/O error, or interrupted write leaves the old .sav intact.
    auto temp=path;temp+=".tmp";
    try{
        if(path.empty()){err="Save path is empty";return false;}
        std::filesystem::create_directories(path.parent_path());
#ifdef _WIN32
        // C++ file streams accept native wide filesystem paths on Windows.
        // Do not use POSIX file descriptors or Linux-only flags here.
        {
            std::ofstream out(temp,std::ios::binary|std::ios::trunc);
            if(!out){err="Cannot open temporary save for writing";return false;}
            out.write(reinterpret_cast<const char*>(raw_.data()),
                      static_cast<std::streamsize>(raw_.size()));
            out.flush();
            if(!out){err="Cannot finish writing temporary save";return false;}
            out.close();
            if(!out){err="Cannot close temporary save";return false;}
        }
        std::error_code renameError;
        if(!replaceFile(temp,path,renameError)){
            std::error_code ignored;std::filesystem::remove(temp,ignored);
            err="Cannot replace save file: "+renameError.message();return false;
        }
#else
        const int fd=::open(temp.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
        if(fd<0){err="Cannot open temporary save: "+std::string(std::strerror(errno));return false;}
        std::size_t written=0;
        int problem=0;
        while(written<raw_.size()){
            const ssize_t n=::write(fd,raw_.data()+written,raw_.size()-written);
            if(n>0)written+=static_cast<std::size_t>(n);
            else if(n<0 && errno==EINTR)continue;
            else {problem=(n==0)?EIO:errno;break;}
        }
        if(!problem && ::fsync(fd)!=0)problem=errno;
        if(::close(fd)!=0 && !problem)problem=errno;
        if(problem){
            std::error_code ignored;std::filesystem::remove(temp,ignored);
            err="Save write/sync failed: "+std::string(std::strerror(problem));
            return false;
        }
        std::error_code renameError;
        if(!replaceFile(temp,path,renameError)){
            std::error_code ignored;std::filesystem::remove(temp,ignored);
            err="Cannot replace save file: "+renameError.message();return false;
        }
        // Sync the directory entry as well, so a sudden power loss is less
        // likely to leave the rename unapplied after the data reached disk.
        const int dirfd=::open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        if(dirfd>=0){
            const int result=::fsync(dirfd);
            const int dirError=errno;
            ::close(dirfd);
            if(result!=0){err="Save directory sync failed: "+std::string(std::strerror(dirError));return false;}
        }
#endif
        dirty_=false;err.clear();return true;
    }catch(const std::exception& e){
        std::error_code ignored;std::filesystem::remove(temp,ignored);
        err=e.what();return false;
    }
}
bool SaveMemory::load(const std::filesystem::path& path,std::string& err){
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in){err="Save file not found";return false;}
    const auto diskSize=in.tellg();
    const bool legacySram=type_==Type::SRAM && diskSize==65536;
    if(diskSize!=static_cast<std::streamoff>(raw_.size()) && !legacySram){
        err="Save size does not match cartridge backup type (" +
            std::to_string(static_cast<long long>(diskSize)) + " bytes on disk; expected " +
            std::to_string(raw_.size()) + ")";
        return false;
    }
    in.seekg(0);std::vector<std::uint8_t> bytes(raw_.size());
    // Older PixelGBA builds emitted 64 KiB SRAM saves. The GBA SRAM window
    // is 32 KiB and mirrored; import the first 32 KiB without losing progress.
    in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!in){err="Save read failed";return false;}
    raw_.swap(bytes);dirty_=false;err.clear();return true;
}
void CycleScheduler::reset(){cycles_=0;}
void Subsystems::reset(bool hasBios){cpu.reset(hasBios);ppu.reset();apu.reset();dma.reset();timers.reset();interrupts.reset();scheduler.reset();}
} // namespace pixelgba
