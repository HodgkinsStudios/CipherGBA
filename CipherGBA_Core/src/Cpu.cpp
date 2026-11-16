// Created by Jacob Hodgkins.
// ARM7TDMI ARMv4T interpreter (functional subset under active validation).
#include "pixelgba/Core.hpp"
#include "pixelgba/GamePakCycleTimeline.hpp"
#include <cstdint>
namespace pixelgba {
namespace {
constexpr std::uint32_t N=0x80000000u,Z=0x40000000u,C=0x20000000u,V=0x10000000u;
std::uint32_t ror(std::uint32_t v,unsigned n){n&=31;return n?((v>>n)|(v<<(32-n))):v;}
std::int32_t sx(std::uint32_t value,unsigned bits){return static_cast<std::int32_t>(value<<(32-bits))>>(32-bits);}
struct Shift {std::uint32_t value;bool carry;};
Shift shift(std::uint32_t x,unsigned type,unsigned amount,bool oldCarry,bool immediate){
    if (!amount) {
        if (!immediate) return {x,oldCarry};
        if(type==0) return {x,oldCarry};
        if(type==1) return {0,(x>>31)!=0};
        if(type==2) return {(x&0x80000000u)?0xffffffffu:0u,(x>>31)!=0};
        return {(oldCarry?0x80000000u:0u)|(x>>1),(x&1)!=0};
    }
    if(type==0){ if(amount<32)return {x<<amount,((x>>(32-amount))&1u)!=0};if(amount==32)return {0,(x&1)!=0};return {0,false};}
    if(type==1){ if(amount<32)return {x>>amount,((x>>(amount-1))&1u)!=0};if(amount==32)return {0,(x>>31)!=0};return {0,false};}
    if(type==2){if(amount>=32)return {(x&0x80000000u)?0xffffffffu:0u,(x>>31)!=0};return {static_cast<std::uint32_t>(static_cast<std::int32_t>(x)>>amount),((x>>(amount-1))&1u)!=0};}
    const unsigned n=amount&31;
    if(!n)return {x,(x>>31)!=0};
    return {ror(x,n),((x>>(n-1))&1u)!=0};
}
struct Add {std::uint32_t value;bool carry,overflow;};
Add add(std::uint32_t a,std::uint32_t b,bool carry){
    const std::uint64_t total=std::uint64_t(a)+b+unsigned(carry);
    const std::uint32_t out=std::uint32_t(total);
    return {out,(total>>32)!=0,((~(a^b)&(a^out))>>31)!=0};
}
// Number of internal multiply iterations on ARM7TDMI; it is the
// multiplier register Rs, not Rm, that controls the early-out timing.
unsigned multiplyIterations(std::uint32_t rs,bool signedEarlyOut=true){
    for(unsigned bytes=1;bytes<=3;bytes++){
        const unsigned top=rs>>(bytes*8u);
        if(top==0u || (signedEarlyOut && top==(0xffffffffu>>(bytes*8u))))return bytes;
    }
    return 4;
}
// ARM7TDMI's multiply C flag is architecturally meaningless but has a
// repeatable carry-save/Booth multiplier value on real GBA hardware.
// Model the partial-adder carry rather than leaving C unchanged. The flag
// depends on Rm, Rs, accumulator and the multiplier early-termination phase.
// Values checked against the independent hardware vectors in suite.gba.
std::uint32_t boothLowCarry(std::uint32_t multiplicand,std::uint32_t multiplier,std::uint32_t accumulator){
    const std::uint32_t factor=multiplicand|1u;
    auto booth=[&](unsigned shift)->std::int32_t{
        return static_cast<std::int32_t>(multiplier<<shift)>>shift;
    };
    std::int32_t previous=booth(31);
    std::uint32_t carry=factor*std::uint32_t(previous);
    std::uint32_t sum=carry+accumulator;
    std::uint32_t saved=accumulator;
    unsigned shiftAmount=29;
    // Each group of four radix-4 Booth addends consumes eight Rs bits.
    for(unsigned group=0;group<4;group++){
        for(unsigned pair=0;pair<4;pair++,shiftAmount-=2){
            const auto next=booth(shiftAmount);
            const std::uint32_t addend=factor*std::uint32_t(next-previous);
            previous=next;
            saved^=carry^addend;
            sum+=addend;
            carry=sum-saved;
        }
        if(previous==static_cast<std::int32_t>(multiplier))break;
    }
    return carry>>31;
}
std::uint32_t boothHighCarry(std::uint32_t multiplicand,std::uint32_t multiplier,
                             std::uint32_t accumulatorHigh,bool signedMultiply){
    const std::uint32_t a=(signedMultiply
        ?std::uint32_t(std::int32_t(multiplicand)>>6):multiplicand>>6)|1u;
    const std::uint32_t b=signedMultiply
        ?std::uint32_t(std::int32_t(multiplier)>>26):multiplier>>26;
    const auto booth=[&](unsigned shift)->std::int32_t{
        return static_cast<std::int32_t>(b<<shift)>>shift;
    };
    const std::int32_t b0=booth(27),b1=booth(29),b2=booth(31);
    const std::uint32_t penultimate=a*std::uint32_t(b1-b2);
    const std::uint32_t middle=a*std::uint32_t(b0-b1);
    const std::uint32_t last=a*std::uint32_t(std::int32_t(b)-b0);
    std::uint32_t state=accumulatorHigh-0x08000000u;
    state-=penultimate&0x10000000u;
    state-=middle&0x40000000u;
    std::uint32_t sum=state+(middle&0x20000000u);
    state-=(~accumulatorHigh)&0x20000000u;
    sum+=last&0x40000000u;
    return (sum^state)>>31;
}
std::uint32_t arm7LongMultiplyCarry(std::uint32_t a,std::uint32_t b,
                                   std::uint32_t low,std::uint32_t high,bool signedMultiply){
    if(multiplyIterations(b,signedMultiply)<4)return boothLowCarry(a,b,low);
    return boothHighCarry(a,b,high,signedMultiply);
}
bool condition(std::uint32_t psr,unsigned code){
    const bool n=psr&N,z=psr&Z,c=psr&C,v=psr&V;
    switch(code){case 0:return z;case 1:return !z;case 2:return c;case 3:return !c;
    case 4:return n;case 5:return !n;case 6:return v;case 7:return !v;
    case 8:return c&&!z;case 9:return !c||z;case 10:return n==v;case 11:return n!=v;
    case 12:return !z&&(n==v);case 13:return z||(n!=v);case 14:return true;default:return false;}
}
}
unsigned Arm7tdmi::modeBank(unsigned mode) const {
    switch(mode&31){case 0x11:return 1;case 0x12:return 2;case 0x13:return 3;case 0x17:return 4;case 0x1b:return 5;default:return 0;}
}
void Arm7tdmi::reset(bool hasBios){
    r.fill(0);banks_={};userHigh_={};fiqHigh_={};savedStatus_={};
    executed=unsupported=cartridgeInstructions=0;irqEntries=softwareInterrupts=0;
    lastCartridgePc=lastSwiImmediate=0;halted=false;
    dataWaitPenalty_=0;fetchValid_=false;expectedFetchPc_=0;lastFetchThumb_=false;lastStepHadDataAccess_=false;lastStepHadInternalRamAccess_=false;
    cpsr=hasBios?0xD3u:0x1Fu; spsr=0;
    r[15]=hasBios?0u:0x08000000u;
    if(!hasBios){r[13]=0x03007f00u;banks_[2][5]=0x03007fa0u;banks_[3][5]=0x03007fe0u;}
}
void Arm7tdmi::setCpsr(std::uint32_t next){
    unsigned old=modeBank(cpsr),now=modeBank(next);
    if(old!=now){
        for(unsigned i=0;i<2;i++)banks_[old][5+i]=r[13+i];
        if(old==1){for(unsigned i=0;i<5;i++)fiqHigh_[i]=r[8+i];}
        else {for(unsigned i=0;i<5;i++)userHigh_[i]=r[8+i];}
        for(unsigned i=0;i<5;i++)r[8+i]=(now==1?fiqHigh_:userHigh_)[i];
        for(unsigned i=0;i<2;i++)r[13+i]=banks_[now][5+i];
        savedStatus_[old]=spsr;spsr=savedStatus_[now];
    }
    cpsr=next;
}
std::uint32_t Arm7tdmi::operand(unsigned i,bool regShift) const {return i==15?r[15]+(thumb()?2u:(regShift?8u:4u)):r[i];}
void Arm7tdmi::exception(std::uint32_t vector,std::uint32_t mode,std::uint32_t lr){
    const std::uint32_t previous=cpsr;
    setCpsr((cpsr&~0x3fu)|mode|0x80u);
    spsr=previous;savedStatus_[modeBank(mode)]=previous;
    r[14]=lr;r[15]=vector;halted=false;
}
// On IRQ entry LR_irq is the next unexecuted instruction address + 4
// regardless of ARM or Thumb state. The BIOS returns via SUBS pc,lr,#4.
void Arm7tdmi::irq(){++irqEntries;exception(0x18,0x12,r[15]+4u);}
void Arm7tdmi::softwareInterrupt(){++softwareInterrupts;exception(0x08,0x13,r[15]);}
unsigned Arm7tdmi::stepInstruction(GbaCore& bus){
    if(halted)return 1;
    const std::uint32_t pc=r[15];
    if((pc>>24)>=8&&(pc>>24)<=13){++cartridgeInstructions;lastCartridgePc=pc;}
    const bool isThumb=thumb();
    const unsigned fetchBytes=isThumb?2u:4u;
    bus.observeInstructionFetch(pc,isThumb);
    const bool seq=fetchValid_ && !lastStepHadDataAccess_ &&
        pc==expectedFetchPc_ && isThumb==lastFetchThumb_;
    // The existing instruction costs already contain one fetch S cycle;
    // only add stalls beyond that single nominal cycle here.
#if !defined(CIPHERGBA_PREFETCH_PIPELINE_LOOKAHEAD) || CIPHERGBA_PREFETCH_PIPELINE_LOOKAHEAD
    constexpr bool pipelineLookahead=true;
#else
    constexpr bool pipelineLookahead=false;
#endif
    // FIFO traffic is for the instruction entering the fetch stage, two
    // slots ahead of the instruction currently executing.
    const std::uint32_t fetchAddress=pc+(pipelineLookahead &&
        GamePakPrefetch::isRom(pc) && bus.gamePakPrefetchEnabled()?2u*fetchBytes:0u);
    const unsigned fetchPenalty=bus.cpuInstructionFetchCycles(fetchAddress,fetchBytes,seq)-1u;
    // The fetch stage precedes execution. Let display, timers, and IRQ
    // sources observe the instruction fetch at its real point in time.
    // Data transfer phases remain aggregated until individually scheduled.
    bus.noteCpuInstructionBusRead(fetchAddress,bus.lastOpcodeFetchWasPhysical());
    bus.cpuTimedBusPhase(fetchPenalty+1u);
    
    
    
    fetchValid_=true;expectedFetchPc_=pc+fetchBytes;lastFetchThumb_=isThumb;
    dataWaitPenalty_=0;
    lastStepHadDataAccess_=false;lastStepHadInternalRamAccess_=false;
    unsigned cost;
    const std::uint32_t fetched=bus.fetchCpuOpcode(pc,isThumb);
    // Decode the rare internal-cycle bus exception before execution (flags
    // can change on MULS/shift). Keep the ordinary one-cycle path cheap.
    bool internalCycleWithPrefetchOff=false;
    if((pc>>24u)>=8u && (pc>>24u)<=13u){
        bool internalCycle=false;
        if(isThumb){
            const unsigned code=(fetched>>6)&15u;
            internalCycle=(fetched&0xfc00u)==0x4000u &&
                          ((code>=2u&&code<=4u)||code==7u||code==13u);
        }else{
            internalCycle=((fetched&0x0e000090u)==0x00000010u) ||
                          ((fetched&0x0fc000f0u)==0x00000090u) ||
                          ((fetched&0x0f8000f0u)==0x00800090u);
            if(internalCycle)internalCycle=condition(cpsr,fetched>>28);
        }
        if(internalCycle)internalCycleWithPrefetchOff=!bus.gamePakPrefetchEnabled();
    }
    
    if(isThumb){
        const std::uint16_t op=std::uint16_t(fetched);
        r[15]=pc+2;
        cost=thumbStep(bus,op,pc);
    } else {
        const std::uint32_t op=fetched;
        r[15]=pc+4;
        cost=armStep(bus,op,pc);
    }
    
    // With prefetch disabled, selected ARM7TDMI internal (I) cycles break
    // the external cartridge fetch burst. Keep the existing pipeline-state
    // storage and disk-save ABI; data transfers already mark that flag.
    if(internalCycleWithPrefetchOff && cost>1u)lastStepHadDataAccess_=true;
    // Non-prefetched ARM opcode reads occupy two active 16-bit Game Pak beats.
    // Charge those beats only when this one-cycle instruction has no data or
    // internal execution phase available for fetch overlap. More general
    // ARM7TDMI pipeline/bus arbitration is still under validation.
    // Short ARM multi-cycle execution can also expose a distinct ROM
    // fetch phase when there is no intervening CPU data-bus transfer.
    // Keep longer instructions on the conservative overlap path until
    // the full ARM7TDMI prefetch queue is modeled and hardware-validated.
    const bool shortInternalFetch = cost == 4u || cost == 5u;
    const bool distinctFetch = !isThumb && ((pc>>24u)>=8u) && ((pc>>24u)<=13u) &&
        !bus.gamePakPrefetchEnabled() && (cost == 1u || shortInternalFetch) &&
        !lastStepHadDataAccess_;
    // An ARM instruction that interrupts the non-prefetched ROM fetch burst
    // with a data transfer or an internal CPU operation may need two further
    // 16-bit Game Pak instruction-delivery beats.  The state flag below is
    // shared by data transactions and selected internal-cycle instructions.
    // This is still an aggregate scheduling approximation: it is validated
    // against the real-BIOS suite but not yet a cycle-exact bus grant model.
    const bool romFetchResumeAfterInterruption = !isThumb && ((pc>>24u)>=8u) &&
        ((pc>>24u)<=13u) && !bus.gamePakPrefetchEnabled() &&
        ((cost >= 3u && lastStepHadDataAccess_) ||
         (cost == 2u && (lastStepHadInternalRamAccess_ || internalCycleWithPrefetchOff)));
    // A firmware SWI returns to the same sequential guest PC but its
    // exception entry/exit has discarded the CPU's instruction pipeline.
    // BIOS-free SWIs must therefore refill the guest instruction stream
    // just as the real BIOS would, even without a visible PC branch.
    const bool hleSwiReturn=!bus.biosLoaded() &&
        (isThumb ? (fetched&0xff00u)==0xdf00u :
                    (fetched&0x0f000000u)==0x0f000000u &&
                    (fetched>>28u)==0xeu);
    const bool redirected = hleSwiReturn || r[15]!=pc+fetchBytes || thumb()!=isThumb;
    const bool physicalReadReturn=!redirected && bus.finishPhysicalRomDataReturn();
    const unsigned romReadReturn = !isThumb && ((pc>>24u)>=8u) && ((pc>>24u)<=13u) &&
        bus.gamePakPrefetchEnabled() && !physicalReadReturn ? (bus.lastCpuGamePakReadWidth()==4u ? 4u : (bus.lastCpuGamePakReadWidth()?3u:0u)) : 0u;
    if(distinctFetch || romFetchResumeAfterInterruption) bus.cpuTimedBusPhase(2u-bus.earlyRomFetchClocks());
    if(romReadReturn)bus.cpuTimedBusPhase(romReadReturn);
    // Most Pokémon (and other Thumb-heavy) instructions continue down the
    // sequential pipeline. No branch target bus refill, external queue reset,
    // ROM bridge or HLE firmware return can occur on this path. Retire it
    // immediately, preserving all already-scheduled fetch/data/ROM phases.
    if(!redirected){
        ++executed;
        return cost+fetchPenalty+dataWaitPenalty_+
            ((distinctFetch||romFetchResumeAfterInterruption)?2u:0u)+romReadReturn;
    }
    const unsigned refill = bus.cpuAccessCycles(r[15],thumb()?2u:4u,false,true)-1u;
    // Taken branches break the sequential instruction stream. A cartridge
    // target first claims an N-cycle bus slot, followed by S-cycle delivery
    // for the next pipeline word/halfword; EWRAM likewise needs two physical
    // word/halfword transfers. IWRAM has no extra wait-state slot.
    const unsigned targetRegion=r[15]>>24u;
    const bool targetRom=targetRegion>=8u && targetRegion<=13u;
    const bool sourceRom=(pc>>24u)>=8u && (pc>>24u)<=13u;
    const bool targetPrefetch=targetRom && bus.gamePakPrefetchEnabled();
#if !defined(CIPHERGBA_PHYSICAL_ROM_BRANCH_REFILL) || CIPHERGBA_PHYSICAL_ROM_BRANCH_REFILL
    // Deliver two target instructions at the destination ISA width. Source
    // bus occupation and internal-port readiness are separate phases; target
    // transactions must not be charged again on sequential CPU resume.
#if !defined(CIPHERGBA_ROM_INTERWORKING_REFILL) || CIPHERGBA_ROM_INTERWORKING_REFILL
    constexpr bool physicalInterworking=true;
#else
    constexpr bool physicalInterworking=false;
#endif
#if !defined(CIPHERGBA_INTERNAL_ROM_PIPELINE) || CIPHERGBA_INTERNAL_ROM_PIPELINE
    const bool internalRomBridge=redirected && targetRom && !sourceRom &&
        (!targetPrefetch || pipelineLookahead);
#else
    constexpr bool internalRomBridge=false;
#endif
    // Keep CPU port readiness separate from physical cartridge wait clocks.
    // The FIFO port uses one launch slot; the unbuffered word port uses two.
    // Direct Thumb delivery requires no additional launch slot.
    const unsigned internalBridgeClocks=internalRomBridge?
        (targetPrefetch?1u:(thumb()?0u:2u)):0u;
    const bool physicalColdRefill=redirected && targetRom &&
        ((sourceRom && (isThumb==thumb() || physicalInterworking) && (!targetPrefetch || pipelineLookahead)) ||
         internalRomBridge);
#else
    constexpr unsigned internalBridgeClocks=0u;
    const bool physicalColdRefill=false;
#endif
    const auto targetPair=physicalColdRefill?GamePakCycleTimeline::branchTargetPair(
        r[15],thumb(),bus.read16(0x04000204u)):GamePakCycleTimeline::BranchTargetPair{};
    const unsigned adjustedRefill=physicalColdRefill?targetPair.clocks[0]-unsigned(!pipelineLookahead):refill;
    const unsigned targetWidth=thumb()?2u:4u;
    // A cold ARM cartridge branch refills the externally-sourced pipeline.
    // A slow sequential beat cannot overlap the CPU's nominal refill slots.
    // Keep this bus-phase approximation isolated until per-beat scheduling lands.
    // experimental model: the fastest sequential Game Pak beat still
    // occupies a physical 16-bit bus slot during a cold ARM pipeline refill.
    // The legacy estimator accidentally skipped subsequent ROM deliveries
    // when its *wait-state* component was one cycle. Retain a build-time
    // fallback because other ARM branch patterns still need phase alignment.
    const unsigned refillSequentialWait = redirected && targetRom && !targetPrefetch && !thumb()
        ? bus.cpuAccessCycles(r[15]+targetWidth,2u,true,true) : 0u;
    const unsigned refillNonSequentialWait = refillSequentialWait
        ? bus.cpuAccessCycles(r[15],2u,false,true) : 0u;
#if CIPHERGBA_FAST_SEQUENTIAL_REFILL
    const bool uncachedRomRefill = refillSequentialWait != 0u;
    // When the N setup is short enough to end before the fixed refill
    // pipeline slot, a further external beat cannot hide behind that slot.
    const unsigned fastSequentialRefill = !physicalColdRefill && refillSequentialWait==1u
        ? 1u + unsigned(refillNonSequentialWait<=3u) : 0u;
#else
    const bool uncachedRomRefill = refillSequentialWait>1u;
    constexpr unsigned fastSequentialRefill = 0u;
#endif
    const unsigned secondRefill=physicalColdRefill?targetPair.clocks[1]-unsigned(!pipelineLookahead):
        redirected && (targetRegion==2u || targetPrefetch || uncachedRomRefill)
        ? bus.cpuAccessCycles(r[15]+targetWidth,targetWidth,true,true)-(uncachedRomRefill?0u:1u) : 0u;
    // The third ROM delivery slot becomes visible when a fast N beat cannot
    // hide the subsequent S beat. ARM word delivery occupies two 16-bit bus
    // beats, including when a slower N grant is followed by a multi-cycle S
    // transfer. A cold Thumb return from internal memory likewise refills an
    // empty external queue. This is a bus-phase approximation, not yet a
    // complete cycle-scheduled ARM7TDMI instruction pipeline.
    const unsigned romN=targetPrefetch && redirected
        ? bus.cpuAccessCycles(r[15],2u,false,true) : 0u;
    const unsigned romS=targetPrefetch && redirected
        ? bus.cpuAccessCycles(r[15]+targetWidth,2u,true,true) : 0u;
    const bool thirdSlot=targetPrefetch && redirected &&
        (romN<=3u || (romN==4u && romS>=2u && (!thumb() || !sourceRom)));
    const unsigned thirdRefill=!physicalColdRefill && (thirdSlot || uncachedRomRefill)
        ? bus.cpuAccessCycles(r[15]+2u*targetWidth,targetWidth,true,true)-1u : 0u;
    // The ARM source word has two active halfword beats. Data/interlock
    // paths above already booked those beats; ordinary branches have not.
    const unsigned sourceActive=physicalColdRefill && sourceRom && !isThumb && !bus.gamePakPrefetchEnabled() &&
        !(distinctFetch||romFetchResumeAfterInterruption)?2u:0u;
    if(sourceActive)bus.cpuTimedBusPhase(sourceActive);
    if(redirected)bus.gamePakBranch();
    if(physicalColdRefill && targetPrefetch)bus.primeBranchTargetPair(r[15],targetWidth);
    if(internalBridgeClocks){
        bus.cpuTimedBusPhase(internalBridgeClocks);
        
    }
    if(adjustedRefill)bus.cpuTimedBusPhase(adjustedRefill);
    if(secondRefill)bus.cpuTimedBusPhase(secondRefill);
    if(thirdRefill)bus.cpuTimedBusPhase(thirdRefill);
    if(physicalColdRefill){
        fetchValid_=true;expectedFetchPc_=r[15];lastFetchThumb_=thumb();lastStepHadDataAccess_=false;
    }
    // A BIOS HLE SWI resumes through a cold instruction-bus return.
    // Account for the first nonsequential Game Pak wait-state interval
    // that an ordinary same-PC continuation would skip. This is tied to
    // the destination bus, never to a specific SWI, test, or game.
    unsigned hleFirmwareReturnClocks=0u;
    if(hleSwiReturn){
        if(targetRegion==2u) hleFirmwareReturnClocks=2u;
        else if(targetRom){
            constexpr unsigned firstWait[4]={4u,3u,2u,8u};
            const unsigned waitcnt=bus.read16(0x04000204u);
            const unsigned bank=(targetRegion-8u)/2u;
            hleFirmwareReturnClocks=firstWait[(waitcnt>>(2u+3u*bank))&3u];
        }
        if(hleFirmwareReturnClocks)
            bus.cpuTimedBusPhase(hleFirmwareReturnClocks);
    }
    if(hleSwiReturn && targetPrefetch)
        bus.deferHleFirmwareRomReturn(r[15],targetWidth);
    
    ++executed;
    return internalBridgeClocks+sourceActive+cost-(physicalColdRefill&&pipelineLookahead?2u:0u)+fetchPenalty+dataWaitPenalty_+((distinctFetch||romFetchResumeAfterInterruption)?2u:0u)+romReadReturn+adjustedRefill+secondRefill+thirdRefill + fastSequentialRefill + hleFirmwareReturnClocks;
}
static std::uint32_t cpuAlignedReadAddress(std::uint32_t address,unsigned width){
    // The 8-bit cartridge backup bus sees the low address bits even for
    // ARM7TDMI halfword/word transfers. Other memory buses force alignment.
    const unsigned region=address>>24u;
    return (region==14u||region==15u)?address:(address&~(width-1u));
}
unsigned Arm7tdmi::armStep(GbaCore& bus,std::uint32_t op,std::uint32_t pc){
    if(!condition(cpsr,op>>28))return 1;
    const unsigned type=(op>>25)&7;
    const unsigned rd=(op>>12)&15,rn=(op>>16)&15,rm=op&15;
    const bool carry=cpsr&C;
#if !defined(CIPHERGBA_ARM_ROM_READ_LATCH_PHASE) || CIPHERGBA_ARM_ROM_READ_LATCH_PHASE
    const bool promoteReadBeat=GamePakPrefetch::isRom(pc) && !bus.gamePakPrefetchEnabled() &&
        ((((type==2u)||(type==3u)) && (op&(1u<<20))) ||
         ((op&0x0e000090u)==0x00000090u && ((op>>5u)&3u)!=0u && (op&(1u<<20))) ||
         (type==4u && (op&(1u<<20))));
#else
    constexpr bool promoteReadBeat=false;
#endif
    auto read8=[&](std::uint32_t a){
        lastStepHadDataAccess_=true;
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,false,1u);
        unsigned clocks=bus.cpuAccessCycles(a,1u,false);
        dataWaitPenalty_+=clocks-1u;
        if(promoteReadBeat)bus.promoteRomReadFetchBeat();
        const auto v=bus.read8(a);
        
        bus.cpuDataBusPhase(clocks);
        return v;
    };
    auto read16=[&](std::uint32_t a){
        lastStepHadDataAccess_=true;
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,false,2u);
        unsigned clocks=bus.cpuAccessCycles(a,2u,false);
        dataWaitPenalty_+=clocks-1u;
        if(promoteReadBeat)bus.promoteRomReadFetchBeat();
        const auto v=bus.read16(a);
        
        bus.cpuDataBusPhase(clocks);
        return v;
    };
    auto read32=[&](std::uint32_t a,bool seq=false){
        lastStepHadDataAccess_=true;
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,false,4u);
        unsigned clocks=bus.cpuAccessCycles(a,4u,seq);
        // Sequential ROM data words occupy both external halfword beats.
        if(seq && (a&0x1ffffu)!=0u && bus.gamePakPrefetchEnabled() &&
           (a>>24u)>=8u && (a>>24u)<=13u)clocks+=2u;
        dataWaitPenalty_+=clocks-1u;
        if(promoteReadBeat)bus.promoteRomReadFetchBeat();
        const bool unmapped=(a>>24u)>15u;
        if(unmapped){
            
            bus.cpuDataBusPhase(clocks);
            
        }

        const auto v=bus.read32(a);
        
        if(!unmapped)bus.cpuDataBusPhase(clocks);
        return v;
    };
    auto write8=[&](std::uint32_t a,std::uint8_t v){
        lastStepHadDataAccess_=true;
        lastStepHadInternalRamAccess_=((a>>24u)==2u||(a>>24u)==3u);
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,true);
        unsigned clocks=bus.cpuAccessCycles(a,1u,false);
        dataWaitPenalty_+=clocks-1u;
        
        bus.write8(a,v);
        bus.cpuDataBusPhase(clocks);
    };
    auto write16=[&](std::uint32_t a,std::uint16_t v){
        lastStepHadDataAccess_=true;
        lastStepHadInternalRamAccess_=((a>>24u)==2u||(a>>24u)==3u);
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,true);
        unsigned clocks=bus.cpuAccessCycles(a,2u,false);
        dataWaitPenalty_+=clocks-1u;
        
        bus.write16(((a>>24u)==14u||(a>>24u)==15u)?a:(a&~1u),v);
        bus.cpuDataBusPhase(clocks);
    };
    auto write32=[&](std::uint32_t a,std::uint32_t v,bool seq=false){
        lastStepHadDataAccess_=true;
        lastStepHadInternalRamAccess_=((a>>24u)==2u||(a>>24u)==3u);
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,true);
        unsigned clocks=bus.cpuAccessCycles(a,4u,seq);
        dataWaitPenalty_+=clocks-1u;
        
        bus.writeCpu32(((a>>24u)==14u||(a>>24u)==15u)?a:(a&~3u),v);
        bus.cpuDataBusPhase(clocks);
    };
    auto flags=[&](std::uint32_t result){cpsr=(cpsr&~(N|Z))|(result&N)|(result==0?Z:0u);};
    auto writepc=[&](std::uint32_t value){r[15]=value&~(thumb()?1u:3u);};
    // Branch and exchange / BLX(register): instruction state follows bit 0.
    if((op&0x0ffffff0u)==0x012fff30u){
        // BLX(register) is ARMv5T; GBA ARM7TDMI implements ARMv4T only.
        ++unsupported;return 1;
    }
    if((op&0x0ffffff0u)==0x012fff10u){
        const std::uint32_t target=operand(rm);
        cpsr=(cpsr&~0x20u)|((target&1u)?0x20u:0u);
        writepc(target);return 3;
    }
    if(type==5){
        if(op&(1u<<24))r[14]=pc+4;
        r[15]=pc+8+std::uint32_t(sx((op&0xffffffu)<<2,26));return 3;
    }
    if((op&0x0f000000u)==0x0f000000u){
        lastSwiImmediate=op&0xffffffu;
        if(!bus.biosLoaded()){
            ++softwareInterrupts;
            return bus.handleHleSwi((lastSwiImmediate>>16)&255u);
        }
        softwareInterrupt();return 3;
    }
    if(type==4){ // LDM / STM including ARMv4T empty-list and user-bank cases
        const bool load=op&(1u<<20),writeback=op&(1u<<21),up=op&(1u<<23),pre=op&(1u<<24);
        const bool user=op&(1u<<22);
        const std::uint32_t rawList=op&0xffffu;
        // ARM7TDMI empty list transfers PC but updates Rn by 0x40.
        const std::uint32_t list=rawList?rawList:0x8000u;
        unsigned count=0;for(unsigned i=0;i<16;i++)count+=(list>>i)&1u;
        const unsigned displacement=rawList?count:16u;
        const std::uint32_t base=operand(rn);
        const std::uint32_t start=up?base+(pre?4u:0u):base-4u*displacement+(pre?0u:4u);
        const std::uint32_t finalBase=up?base+4u*displacement:base-4u*displacement;
        // LDM^ with PC restores SPSR; all other ^ transfers use user registers.
        const bool userBank=user && !(load&&(list&0x8000u));
        auto userGet=[&](unsigned i)->std::uint32_t {
            if(!userBank)return operand(i)+(i==15?4u:0u);
            if(i>=8&&i<=12 && modeBank(cpsr)==1)return userHigh_[i-8];
            if(i==13||i==14)return modeBank(cpsr)==0?r[i]:banks_[0][i-8];
            return operand(i)+(i==15?4u:0u);
        };
        auto userSet=[&](unsigned i,std::uint32_t v){
            if(userBank && i>=8&&i<=12 && modeBank(cpsr)==1)userHigh_[i-8]=v;
            else if(userBank && (i==13||i==14) && modeBank(cpsr)!=0)banks_[0][i-8]=v;
            else r[i]=v;
        };
        std::uint32_t address=start,loadedPc=0;
        unsigned transferred=0;
        for(unsigned i=0;i<16;i++)if(list&(1u<<i)){
            if(load){const std::uint32_t v=read32(cpuAlignedReadAddress(address,4u),transferred!=0);
                if(i==15)loadedPc=v;else userSet(i,v);
            }else{
                // STM with base register in the list: when it is not the
                // first register, ARM7 stores the post-writeback base value.
                const std::uint32_t value=(writeback && i==rn && transferred!=0)?finalBase:userGet(i);
                write32(address,value,transferred!=0);
            }
            address+=4;++transferred;
        }
        if(writeback && !(load&&(list&(1u<<rn))))r[rn]=finalBase;
        if(load&&(list&0x8000u)){
            if(user)setCpsr(spsr);
            writepc(loadedPc);
        }
        return count+(load?2u:1u)+(load&&(list&0x8000u)?2u:0u);
    }
    if(type==2||type==3){ // LDR/STR with byte and register offsets
        const bool regOffset=op&(1u<<25),pre=op&(1u<<24),up=op&(1u<<23),byte=op&(1u<<22),wb=op&(1u<<21),load=op&(1u<<20);
        std::uint32_t offset=op&0xfffu;
        if(regOffset){if(op&0x10u){++unsupported;return 1;} offset=shift(operand(rm),(op>>5)&3,(op>>7)&31,carry,true).value;}
        const std::uint32_t base=operand(rn),effective=up?base+offset:base-offset,addr=pre?effective:base;
        if(load){
            std::uint32_t value;
            if(byte)value=read8(addr);else value=ror(read32(cpuAlignedReadAddress(addr,4u)),8*(addr&3u));
            if(rd==15)writepc(value);else r[rd]=value;
        }else{
            const std::uint32_t value=operand(rd)+(rd==15?4u:0u);
            if(byte)write8(addr,std::uint8_t(value));else write32(addr,value);
        }
        if(!pre||wb){if(!(load&&rd==rn))r[rn]=effective;}
        return load?(rd==15?5u:3u):2u;
    }
    if((op&0x0fb00ff0u)==0x01000090u){ // ARMv4 SWP/SWPB atomic exchange
        const std::uint32_t address=operand(rn);
        const std::uint32_t source=operand(rm);
        const bool byte=op&0x400000u;
        const std::uint32_t previous=byte?read8(address):ror(read32(cpuAlignedReadAddress(address,4u)),(address&3u)*8);
        if(byte)write8(address,static_cast<std::uint8_t>(source));
        else write32(address,source);
        if(rd==15)writepc(previous);else r[rd]=previous;
        return 4;
    }
    if((op&0x0fc000f0u)==0x00000090u){ // MUL / MLA (ARMv4T field layout)
        // Unlike ARM data processing, MUL/MLA encode the DESTINATION in
        // bits 19..16 (Rd), and the MLA accumulator in bits 15..12 (Rn).
        // Reversing these corrupts integer math used by software audio.
        const unsigned mulDest=(op>>16)&15u;
        const unsigned mulAcc=(op>>12)&15u;
        const bool acc=op&(1u<<21),s=op&(1u<<20);
        // Rs is sampled at instruction issue. Rd may alias Rs (notably
        // compiler-emitted two-operand MUL and MLA); writing the result before
        // computing the early-out duration spuriously times the *product*,
        // not the original multiplier. ARM7TDMI's iterations are based on
        // the input Rs, even when its register is also the destination.
        const unsigned iterations=multiplyIterations(r[(op>>8)&15u]);
        std::uint32_t value=std::uint32_t(std::uint64_t(r[rm])*r[(op>>8)&15]);
        if(acc)value+=r[mulAcc];
        if(mulDest==15){++unsupported;return 1;}
        r[mulDest]=value;if(s)flags(value);
        return 1u+iterations+(acc?1u:0u);
    }
    if((op&0x0f8000f0u)==0x00800090u){ // signed/unsigned long multiplication
        const bool sign=op&(1u<<22),acc=op&(1u<<21),s=op&(1u<<20);
        const std::uint32_t a=r[rm],b=r[(op>>8)&15];
        // Latch internal-cycle duration before writing RdLo/RdHi; the
        // destination and source register fields can alias.
        const unsigned iterations=multiplyIterations(b,sign);
        const std::uint32_t low=acc?r[rd]:0u,high=acc?r[rn]:0u;
        std::uint64_t result=sign?std::uint64_t(std::int64_t(std::int32_t(a))*std::int64_t(std::int32_t(b))):
                                std::uint64_t(a)*b;
        if(acc)result+=(std::uint64_t(high)<<32)|low;
        r[rd]=std::uint32_t(result);r[rn]=std::uint32_t(result>>32);
        if(s)cpsr=(cpsr&~(N|Z|C))|((result&(1ull<<63))?N:0)|(!result?Z:0)|
                    (arm7LongMultiplyCarry(a,b,low,high,sign)?C:0);
        return 2u+iterations+(acc?1u:0u);
    }
    if((op&0x0e000090u)==0x00000090u && ((op>>5)&3)){ // LDRH/LDRSB/LDRSH/STRH
        const bool load=op&(1u<<20),pre=op&(1u<<24),up=op&(1u<<23),wb=op&(1u<<21);
        std::uint32_t offset=(op&(1u<<22))?(((op>>8)&15)*16+(op&15)):operand(rm);
        std::uint32_t base=operand(rn),eff=up?base+offset:base-offset,addr=pre?eff:base;
        const unsigned kind=(op>>5)&3;
        if(load){
            // LDRSH must read a full halfword on an aligned address. The
            // previous implementation always fetched a byte for kind==3,
            // which discarded the sign/high byte of 16-bit PCM arithmetic.
            // ARM7TDMI odd-address LDRSH instead sign-extends the byte.
            std::uint32_t v=(kind==1 || (kind==3 && !(addr&1u)))
                ?read16(cpuAlignedReadAddress(addr,2u)):read8(addr);
            if(kind==1&&(addr&1u))v=ror(v,8);
            if(kind==2)v=std::uint32_t(sx(v,8));
            if(kind==3)v=std::uint32_t((addr&1u)?sx(v,8):sx(v,16));
            if(rd==15)writepc(v);else r[rd]=v;
        }else if(kind==1)write16(addr,std::uint16_t(operand(rd)));
        else {++unsupported;return 1;}
        if(!pre||wb){if(!(load&&rd==rn))r[rn]=eff;}
        return load?3u:2u;
    }
    if((op&0x0fbf0fffu)==0x010f0000u){
        r[rd]=(op&0x00400000u)?spsr:cpsr;return 1; // MRS
    }
    if((op&0x0db0f000u)==0x0120f000u){
        std::uint32_t value;
        if(op&(1u<<25))value=ror(op&255,(op>>8&15)*2);
        else value=operand(rm);
        std::uint32_t mask=0;
        for(unsigned i=0;i<4;i++)if(op&(1u<<(16+i)))mask|=0xffu<<(8*i);
        if(op&(1u<<22))spsr=(spsr&~mask)|(value&mask);
        else {if((cpsr&31)==0x10)mask&=0xff000000u;setCpsr((cpsr&~mask)|(value&mask));}
        return 1;
    }
    if((op&0x0c000000u)==0){ // ARM data-processing
        const unsigned opcode=(op>>21)&15;
        const bool s=op&(1u<<20),immediate=op&(1u<<25);
        Shift rhs{};
        if(immediate){const unsigned rot=((op>>8)&15)*2;rhs={ror(op&255,rot),rot?((ror(op&255,rot)>>31)!=0):carry};}
        else {const bool regShift=op&0x10u;
            // Register-specified shifts consume an extra pipeline step. If
            // R15 is read as Rm/Rn/Rs its value is PC+12 (rather than +8).
            const unsigned rs=(op>>8)&15;
            rhs=shift(operand(rm,regShift),(op>>5)&3,
                      regShift?(operand(rs,true)&255u):((op>>7)&31),carry,!regShift);
        }
        const bool registerShift=!(op&(1u<<25)) && (op&0x10u);
        const std::uint32_t lhs=operand(rn,registerShift);std::uint32_t result=0;
        bool arith=false,c=rhs.carry,v=cpsr&V;
        Add sum{};
        switch(opcode){
            case 0:result=lhs&rhs.value;break;case 1:result=lhs^rhs.value;break;
            case 2:sum=add(lhs,~rhs.value,true);arith=true;break;
            case 3:sum=add(rhs.value,~lhs,true);arith=true;break;
            case 4:sum=add(lhs,rhs.value,false);arith=true;break;
            case 5:sum=add(lhs,rhs.value,carry);arith=true;break;
            case 6:sum=add(lhs,~rhs.value,carry);arith=true;break;
            case 7:sum=add(rhs.value,~lhs,carry);arith=true;break;
            case 8:result=lhs&rhs.value;break;case 9:result=lhs^rhs.value;break;
            case 10:sum=add(lhs,~rhs.value,true);arith=true;break;
            case 11:sum=add(lhs,rhs.value,false);arith=true;break;
            case 12:result=lhs|rhs.value;break;case 13:result=rhs.value;break;
            case 14:result=lhs&~rhs.value;break;case 15:result=~rhs.value;break;
        }
        if(arith){result=sum.value;c=sum.carry;v=sum.overflow;}
        const bool compare=opcode>=8&&opcode<=11;
        // ARM7TDMI: even the normally flag-only test/compare opcodes may
        // encode Rd=PC. That unpredictable-looking ARMv4T form restores the
        // CPSR from the current mode's SPSR, but DOES NOT write the PC or
        // flush the pipeline. The ARM ROM specifically checks both effects.
        // Ordinary data-processing writes to PC with S=1 also restore SPSR.
        const bool restoreStatus=rd==15 && (s || compare);
        if(restoreStatus)setCpsr(spsr);
        if(!compare){if(rd==15)writepc(result);else r[rd]=result;}
        if((s||compare) && !restoreStatus){
            flags(result);cpsr=(cpsr&~C)|(c?C:0u);
            if(arith)cpsr=(cpsr&~V)|(v?V:0u);
        }
        return (rd==15&&!compare?3u:1u)+(registerShift?1u:0u);
    }
    ++unsupported;return 1;
}
unsigned Arm7tdmi::thumbStep(GbaCore& bus,std::uint16_t op,std::uint32_t pc){
    const bool carry=cpsr&C;
#if (!defined(CIPHERGBA_PHYSICAL_ROM_DATA_RETURN) || CIPHERGBA_PHYSICAL_ROM_DATA_RETURN) && \
    (!defined(CIPHERGBA_PREFETCH_PIPELINE_LOOKAHEAD) || CIPHERGBA_PREFETCH_PIPELINE_LOOKAHEAD)
    // Filter on the instruction bits before querying the cartridge prefetch
    // control register. Most Thumb instructions do not access memory, so the
    // bus-state query is unnecessary for them. Both sides are read-only and
    // retain the identical physical-return predicate and guest timing.
    const bool physicalSingleLoad=(
        (op&0xf800u)==0x4800u ||
        ((op&0xf000u)==0x5000u && ((op>>9u)&7u)>=3u) ||
        ((op&0xe000u)==0x6000u && (op&0x0800u)) ||
        ((op&0xf000u)==0x8000u && (op&0x0800u)) ||
        ((op&0xf000u)==0x9000u && (op&0x0800u))) &&
        GamePakPrefetch::isRom(pc) && bus.gamePakPrefetchEnabled() &&
        bus.canPhysicalRomReadReturn();
#else
    constexpr bool physicalSingleLoad=false;
#endif
    auto read8=[&](std::uint32_t a){
        lastStepHadDataAccess_=true;
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,false,1u);
        unsigned clocks=bus.cpuAccessCycles(a,1u,false);
        if(physicalSingleLoad && GamePakPrefetch::isRom(a)){
            const auto wait=bus.read16(0x04000204u);
            clocks=GamePakPrefetch::busCycles(a,false,wait);
            bus.notePhysicalRomDataRead();
        }
        dataWaitPenalty_+=clocks-1u;
        const auto v=bus.read8(a);
        
        bus.cpuDataBusPhase(clocks);
        return v;
    };
    auto read16=[&](std::uint32_t a){
        lastStepHadDataAccess_=true;
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,false,2u);
        unsigned clocks=bus.cpuAccessCycles(a,2u,false);
        if(physicalSingleLoad && GamePakPrefetch::isRom(a)){
            const auto wait=bus.read16(0x04000204u);
            clocks=GamePakPrefetch::busCycles(a,false,wait);
            bus.notePhysicalRomDataRead();
        }
        dataWaitPenalty_+=clocks-1u;
        const auto v=bus.read16(a);
        
        bus.cpuDataBusPhase(clocks);
        return v;
    };
    auto read32=[&](std::uint32_t a,bool seq=false){
        lastStepHadDataAccess_=true;
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,false,4u);
        unsigned clocks=bus.cpuAccessCycles(a,4u,seq);
        if(physicalSingleLoad && GamePakPrefetch::isRom(a)){
            const auto wait=bus.read16(0x04000204u);
            clocks=GamePakPrefetch::busCycles(a,false,wait);
            clocks+=GamePakPrefetch::busCycles(a+2u,true,wait);
            bus.notePhysicalRomDataRead();
        }
        // Sequential ROM data words occupy both external halfword beats.
        if(seq && (a&0x1ffffu)!=0u && bus.gamePakPrefetchEnabled() &&
           (a>>24u)>=8u && (a>>24u)<=13u)clocks+=2u;
        dataWaitPenalty_+=clocks-1u;
        // Unmapped read samples the bus at the transfer's completion,
        // after DMA arbitration that can occur during its single-clock beat.
        const bool unmapped=(a>>24u)>15u;
        if(unmapped){
            
            bus.cpuDataBusPhase(clocks);
            
        }

        const auto v=bus.read32(a);
        
        if(!unmapped)bus.cpuDataBusPhase(clocks);
        // The unmapped read samples open bus at its completion without
        // driving an external word, but it is still a CPU data access for
        // cartridge opcode-burst continuity. Retain the interlock so the
        // following physical ROM fetch is nonsequential. Independent NBA
        // reference trace measures 36 clocks per corresponding Thumb loop.
        // Do not skip the data-return clock or change the sampled value.
        return v;
    };
    auto write8=[&](std::uint32_t a,std::uint8_t v){
        lastStepHadDataAccess_=true;
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,true);
        unsigned clocks=bus.cpuAccessCycles(a,1u,false);
        dataWaitPenalty_+=clocks-1u;
        
        bus.write8(a,v);
        bus.cpuDataBusPhase(clocks);
    };
    auto write16=[&](std::uint32_t a,std::uint16_t v){
        lastStepHadDataAccess_=true;
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,true);
        unsigned clocks=bus.cpuAccessCycles(a,2u,false);
        dataWaitPenalty_+=clocks-1u;
        
        bus.write16(((a>>24u)==14u||(a>>24u)==15u)?a:(a&~1u),v);
        bus.cpuDataBusPhase(clocks);
    };
    auto write32=[&](std::uint32_t a,std::uint32_t v,bool seq=false){
        lastStepHadDataAccess_=true;
        dataWaitPenalty_+=bus.waitForCpuGamePakClaim(a);
        bus.noteCpuDataBusAccess(a,true);
        unsigned clocks=bus.cpuAccessCycles(a,4u,seq);
        dataWaitPenalty_+=clocks-1u;
        
        bus.writeCpu32(((a>>24u)==14u||(a>>24u)==15u)?a:(a&~3u),v);
        bus.cpuDataBusPhase(clocks);
    };
    auto nz=[&](std::uint32_t value){cpsr=(cpsr&~(N|Z))|(value&N)|(value==0?Z:0);};
    auto arithmetic=[&](Add x){nz(x.value);cpsr=(cpsr&~(C|V))|(x.carry?C:0u)|(x.overflow?V:0u);return x.value;};
    auto setc=[&](bool c){cpsr=(cpsr&~C)|(c?C:0u);};
    auto branch=[&](std::uint32_t target){r[15]=target&~1u;};
    // Group the 16-bit Thumb ISA by its architecturally fixed high nibble.
    // The existing instruction bodies and cycle counts remain unchanged;
    // this jump table avoids testing unrelated opcode families on each step.
    switch(op>>12u){
        case 0u:case 1u:goto thumb_shift_add;
        case 2u:case 3u:goto thumb_immediate;
        case 4u:goto thumb_alu;
        case 5u:goto thumb_reg_load;
        case 6u:case 7u:goto thumb_imm_load;
        case 8u:goto thumb_halfword;
        case 9u:goto thumb_sp_load;
        case 10u:goto thumb_address;
        case 11u:goto thumb_sp_misc;
        case 12u:goto thumb_multi;
        case 13u:goto thumb_conditional;
        case 14u:goto thumb_branch;
        case 15u:goto thumb_long_branch;
    }
thumb_shift_add:
    if((op&0xf800)==0x1800){ // add/sub 3-bit register or immediate
        const unsigned a=(op>>6)&7,b=(op>>3)&7,d=op&7;
        r[d]=arithmetic((op&0x200)?add(r[b],~((op&0x400)?a:r[a]),true):add(r[b],(op&0x400)?a:r[a],false));return 1;
    }
    if((op&0xe000)==0){ // shift immediate
        const unsigned d=op&7,s=(op>>3)&7,typ=(op>>11)&3;
        auto x=shift(r[s],typ,(op>>6)&31,carry,true);
        r[d]=x.value;nz(x.value);setc(x.carry);return 1;
    }
thumb_immediate:
    if((op&0xe000)==0x2000){ // MOV/CMP/ADD/SUB immediate
        const unsigned d=(op>>8)&7,v=op&255;
        switch((op>>11)&3){case 0:r[d]=v;nz(r[d]);break;case 1:arithmetic(add(r[d],~v,true));break;
            case 2:r[d]=arithmetic(add(r[d],v,false));break;case 3:r[d]=arithmetic(add(r[d],~v,true));break;}
        return 1;
    }
thumb_alu:
    if((op&0xfc00)==0x4000){ // register ALU
        unsigned code=(op>>6)&15,d=op&7,s=(op>>3)&7;std::uint32_t a=r[d],b=r[s],out=a;
        if(code<=1){out=code?(a^b):(a&b);nz(out);}
        else if(code>=2&&code<=4){auto x=shift(a,code==2?0:code==3?1:2,b&255,carry,false);out=x.value;nz(out);setc(x.carry);}
        else if(code==5)out=arithmetic(add(a,b,carry));
        else if(code==6)out=arithmetic(add(a,~b,carry));
        else if(code==7){auto x=shift(a,3,b&255,carry,false);out=x.value;nz(out);setc(x.carry);}
        else if(code==8){nz(a&b);return 1;}
        else if(code==9)out=arithmetic(add(0,~b,true));
        else if(code==10){arithmetic(add(a,~b,true));return 1;}
        else if(code==11){arithmetic(add(a,b,false));return 1;}
        else if(code==12){out=a|b;nz(out);}
        else if(code==13){out=std::uint32_t(std::uint64_t(a)*b);nz(out);}
        else if(code==14){out=a&~b;nz(out);}
        else if(code==15){out=~b;nz(out);}
        r[d]=out;
        if(code>=2&&code<=4)return 2; // register-specified shift: 1S+1I
        if(code==7)return 2;
        if(code==13)return 1u+multiplyIterations(a); // Rd controls multiplier iterations
        return 1;
    }
    if((op&0xfc00)==0x4400){ // high registers, BX
        const unsigned code=(op>>8)&3,d=(op&7)|((op>>4)&8),s=(op>>3)&15;
        const std::uint32_t b=operand(s),a=operand(d);
        if(code==0){if(d==15)branch(a+b);else r[d]=a+b;}
        else if(code==1)arithmetic(add(a,~b,true));
        else if(code==2){if(d==15)branch(b);else r[d]=b;}
        else {
            if(op&0x80u){++unsupported;return 1;} // Thumb BLX is ARMv5T only
            cpsr=(cpsr&~0x20u)|((b&1u)?0x20u:0u);
            r[15]=b&~(thumb()?1u:3u);
        }
        // High-register ADD/MOV are 1S, except PC writes/BX (pipeline refill).
        return code==3 || (d==15 && code!=1)?3u:1u;
    }
    if((op&0xf800)==0x4800){const unsigned d=(op>>8)&7;const std::uint32_t addr=((pc+4)&~3u)+((op&255)*4);r[d]=read32(addr);return 3;}
thumb_reg_load:
    if((op&0xf000)==0x5000){ // register offset and sign/halfword
        const unsigned d=op&7,b=(op>>3)&7,s=(op>>6)&7;const std::uint32_t addr=r[b]+r[s];
        bool load=(op&0x200u)?(((op>>10)&3u)!=0u):bool(op&0x800u);
        if(op&0x200){switch((op>>10)&3){
            case 0:write16(addr,std::uint16_t(r[d]));break;
            case 1:r[d]=std::uint32_t(sx(read8(addr),8));break;
            case 2:r[d]=(addr&1)?ror(read16(cpuAlignedReadAddress(addr,2u)),8):read16(addr);break;
            case 3:r[d]=std::uint32_t((addr&1)?sx(read8(addr),8):sx(read16(addr),16));break;
        }}else{
            const bool load=op&0x800,byte=op&0x400;
            if(load)r[d]=byte?read8(addr):ror(read32(cpuAlignedReadAddress(addr,4u)),8*(addr&3));
            else if(byte)write8(addr,std::uint8_t(r[d]));else write32(addr,r[d]);
        }return load?3u:2u;
    }
thumb_imm_load:
    if((op&0xe000)==0x6000){ // immediate offset word/byte
        const unsigned d=op&7,b=(op>>3)&7;const bool byte=op&0x1000,load=op&0x800;
        const std::uint32_t addr=r[b]+((op>>6&31)*(byte?1:4));
        if(load)r[d]=byte?read8(addr):ror(read32(cpuAlignedReadAddress(addr,4u)),8*(addr&3u));
        else if(byte)write8(addr,std::uint8_t(r[d]));
        else write32(addr,r[d]);
        return load?3u:2u;
    }
thumb_halfword:
    if((op&0xf000)==0x8000){ // halfword immediate
        const unsigned d=op&7,b=(op>>3)&7;const std::uint32_t addr=r[b]+((op>>6&31)*2);
        if(op&0x800)r[d]=(addr&1)?ror(read16(cpuAlignedReadAddress(addr,2u)),8):read16(addr);else write16(addr,std::uint16_t(r[d]));return (op&0x800u)?3u:2u;
    }
    // SP-relative single LDR rotates unaligned words; STR aligns the bus write.
thumb_sp_load:
    if((op&0xf000)==0x9000){const unsigned d=(op>>8)&7;const std::uint32_t addr=r[13]+((op&255)*4);
        if(op&0x800)r[d]=ror(read32(cpuAlignedReadAddress(addr,4u)),8u*(addr&3u));
        else write32(addr,r[d]);
        return (op&0x800u)?3u:2u;
    }
thumb_address:
    if((op&0xf000)==0xa000){const unsigned d=(op>>8)&7;r[d]=((op&0x800)?r[13]:((pc+4)&~3u))+((op&255)*4);return 1;}
thumb_sp_misc:
    if((op&0xff00)==0xb000){r[13]+=(op&0x80)?-(std::int32_t((op&127)*4)):((op&127)*4);return 1;}
    if((op&0xf600)==0xb400){ // PUSH/POP
        const bool pop=op&0x800;const unsigned list=op&255;
        unsigned count=0;for(unsigned i=0;i<8;i++)count+=(list>>i)&1u;
        const bool extra=bool(op&0x100u);
        if(pop){
            unsigned transferred=0;
            for(unsigned i=0;i<8;i++)if(list&(1u<<i)){
                r[i]=read32(cpuAlignedReadAddress(r[13],4u),transferred!=0);r[13]+=4;++transferred;
            }
            if(extra){branch(read32(cpuAlignedReadAddress(r[13],4u),transferred!=0));r[13]+=4;}
            else if(!list){branch(read32(cpuAlignedReadAddress(r[13],4u)));r[13]+=0x40;} // ARMv4T empty-list quirk
        }else if(!extra&&!list){
            r[13]-=0x40;write32((r[13]+0x3cu)&~3u,pc+4u);
        }else{
            // ARM7 transfers PUSH in ascending memory address order, even
            // though its final SP is lower. Preserve IO side-effect order.
            const std::uint32_t base=r[13]-4u*(count+unsigned(extra));
            std::uint32_t address=base;unsigned transferred=0;
            for(unsigned i=0;i<8;i++)if(list&(1u<<i)){
                write32(address,r[i],transferred!=0);address+=4;++transferred;
            }
            if(extra)write32(address,r[14],transferred!=0);
            r[13]=base;
        }
        const unsigned transfers=count+unsigned(extra);
        return (pop?transfers+2u:transfers+1u)+(pop&&extra?2u:0u);
    }
    // ARM7TDMI multiple transfers always force word alignment, even when Rb
    // is unaligned. Writeback retains the original low address bits.
    // Crucial to BIOS CpuSet moving Thumb code from odd source pointers.
thumb_multi:
    if((op&0xf000)==0xc000){ // LDMIA/STMIA
        const unsigned b=(op>>8)&7;const bool load=op&0x800;
        const unsigned list=op&255;
        const std::uint32_t base=r[b];
        std::uint32_t addr=base;unsigned count=0;
        if(!list){
            if(load)branch(read32(cpuAlignedReadAddress(addr,4u)));
            else write32(addr&~3u,pc+6u); // Thumb empty-list STM writes PC+6 (ARM7TDMI pipeline)
            r[b]=base+0x40u;
            return load?5u:2u;
        }
        unsigned transferCount=0;for(unsigned i=0;i<8;i++)transferCount+=(list>>i)&1u;
        for(unsigned i=0;i<8;i++)if(list&(1u<<i)){
            if(load)r[i]=read32(cpuAlignedReadAddress(addr,4u),count!=0);
            else write32(addr,(i==b && count!=0u)?(base+4u*transferCount):r[i],count!=0);
            addr+=4;++count;
        }
        if(!load||!(list&(1u<<b)))r[b]=base+4u*count;
        return count+(load?2u:1u);
    }
thumb_conditional:
    if((op&0xff00)==0xdf00){
        lastSwiImmediate=op&255u;
        if(!bus.biosLoaded()){
            ++softwareInterrupts;
            return bus.handleHleSwi(lastSwiImmediate);
        }
        softwareInterrupt();return 3;
    }
    if((op&0xf000)==0xd000){const unsigned cond=(op>>8)&15;
        if(cond==14||cond==15){++unsupported;return 1;}
        if(condition(cpsr,cond)){branch(pc+4+std::uint32_t(sx((op&255)*2,9)));return 3;}return 1;}
thumb_branch:
    if((op&0xf800)==0xe000){branch(pc+4+std::uint32_t(sx((op&0x7ff)*2,12)));return 3;}
thumb_long_branch:
    if((op&0xf800)==0xf000){r[14]=pc+4+std::uint32_t(sx((op&0x7ff)<<12,23));return 1;}
    if((op&0xf800)==0xf800){const std::uint32_t target=r[14]+((op&0x7ff)*2);r[14]=(pc+2)|1u;branch(target);return 3;}
    ++unsupported;return 1;
}
} // namespace pixelgba
