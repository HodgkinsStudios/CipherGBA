// Created by HodgkinsStudios. Independent portable HLE BIOS library.
#include "pixelgba/HleInternal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <array>
#include <vector>
namespace {

std::uint32_t alignDown(std::uint32_t address,unsigned multiple){return address&~(multiple-1u);}
// BIOS sine lookup is indexed by the high byte of the 16-bit angle. Build
// the 1.14 fixed-point table once, rather than calling host libm for every
// sprite. Entries use nearest-integer rounding, including the quadrants.
const std::array<std::int32_t,256>& biosSine(){
    static const auto table=[](){
        std::array<std::int32_t,256> result{};
        constexpr double tau=6.283185307179586476925286766559;
        for(unsigned i=0;i<256;i++)
            result[i]=std::int32_t(std::lround(std::sin(tau*double(i)/256.0)*16384.0));
        return result;
    }();
    return table;
}
std::int16_t signed16(std::uint16_t v){return std::int16_t(v);}
// The ARM BIOS ArcTan polynomial runs with 32-bit ARM multiplies and signed
// arithmetic shifts. Each multiply truncates to 32 bits before shifting.
// Keeping that wraparound is important even for inputs outside s1.14 range.
std::int32_t armMulAsr(std::int32_t a,std::int32_t b,unsigned shift){
    const auto low=std::uint32_t(std::uint64_t(std::uint32_t(a))*std::uint32_t(b));
    return std::int32_t(low)>>shift;
}
void hleArcTan(std::uint32_t& r0,std::uint32_t& r1,std::uint32_t& r3){
    const auto x=std::int32_t(r0);
    std::int32_t a=-armMulAsr(x,x,14);
    std::int32_t b=armMulAsr(0xA9,a,14)+0x390;
    for(auto coefficient:{0x91C,0xFB6,0x16AA,0x2081,0x3651,0xA2F9})
        b=armMulAsr(b,a,14)+coefficient;
    r0=std::uint32_t(armMulAsr(x,b,16));
    r1=std::uint32_t(a);
    r3=std::uint32_t(b);
}

// Count the original square-root routine's control-flow costs, including its
// SWI entry/exit, stack traffic and ARM pipeline refills. The bounded loop
// structure is derived from the independently audited, instruction-address
// semantics in GBA_BIOS_Ripper (not from firmware opcode bytes). ARM7TDMI
// conditional instructions consume a cycle even when their condition fails;
// taken branches consume two additional refill cycles.
// This count has been checked against original-BIOS execution for inputs
// spanning zero, small squares, large values and the unsigned upper range.
unsigned hleSqrtCycles(std::uint32_t input){
    std::uint32_t value=input, scale=1;
    std::uint32_t instructions=3u, refillBranches=0u;
    for(;;){
        instructions+=4u;
        if(value<=scale)break;
        value>>=1u;
        scale<<=1u;
        ++refillBranches;
    }
    for(;;){
        value=input;
        const std::uint32_t candidate=scale;
        std::uint32_t resultBits=0,trial=scale;
        instructions+=4u;
        for(;;){
            instructions+=3u;
            const auto half=value>>1u;
            const bool shift=trial<=half;
            const bool repeat=trial<half;
            if(shift)trial<<=1u;
            if(!repeat)break;
            ++refillBranches;
        }
        for(;;){
            instructions+=6u;
            const bool subtract=value>=trial;
            resultBits=(resultBits<<1u)+std::uint32_t(subtract);
            if(subtract)value-=trial;
            if(trial==scale)break;
            trial>>=1u;
            ++refillBranches;
        }
        instructions+=4u;
        scale=(scale+resultBits)>>1u;
        if(scale>=candidate)break;
        ++refillBranches;
    }
    instructions+=3u;
    // 53 measured fixed clocks: SWI vector/dispatcher, return, stack access
    // and the final BX refill. Each taken branch incurs two extra clocks.
    return 53u+instructions+2u*refillBranches;
}

// Input-dependent control-flow clock cost of the BIOS restoring-divider.
// Each additional binary quotient place ordinarily costs thirteen ARM clocks;
// a final divisor equal to floor(dividend/2) takes a shorter branch on its
// final shift. DivArm enters through three extra register moves.
// Independently checked with original-BIOS SWIs over positive/negative and
// boundary inputs. Division by zero has no bounded original clock count.
unsigned hleDivCycles(std::int64_t dividend,std::int64_t divisor,bool swapped){
    const std::uint64_t numerator=std::uint64_t(dividend<0?-dividend:dividend);
    const std::uint64_t denominator=std::uint64_t(divisor<0?-divisor:divisor);
    if(!denominator)return 12u;
    const auto quotient=numerator/denominator;
    unsigned bits=0u;
    for(auto v=quotient;v>=2u;v>>=1u)++bits;
    unsigned cost=70u+13u*bits+(swapped?3u:0u);
    // The final shift's non-taken branch is selected when the shifted
    // divisor exactly equals floor(numerator/2). This also includes some
    // nonzero-remainder quotients (e.g. 9/4), not only exact powers of two.
    if(bits && (denominator<<(bits-1u))==(numerator>>1u))
        cost-=5u;
    return cost;
}

// ARM7TDMI MUL early-out: a signed word held in the multiplier register
// requires one internal multiply beat per significant byte (high bytes may
// be either zero-extended or sign-extended). The ArcTan polynomial has two
// such operand-width-sensitive multiply sites on the audited firmware path.
unsigned multiplyByteStages(std::uint32_t v){
    if((v>>8u)==0u||(v>>8u)==0x00ffffffu)return 1u;
    if((v>>16u)==0u||(v>>16u)==0x0000ffffu)return 2u;
    if((v>>24u)==0u||(v>>24u)==0x000000ffu)return 3u;
    return 4u;
}

// Convert the exact fixed-point product to signed 16 bits as written by BIOS.
std::uint16_t affineWord(std::int32_t scale,std::int32_t trig){
    return std::uint16_t((scale*trig)>>14);
}
}
extern "C" {
uint32_t cipher_gba_bios_hle_abi_version(void){return CIPHER_GBA_BIOS_HLE_ABI_VERSION;}
uint32_t cipher_gba_bios_hle_valid(const CipherGbaHleHost* h){return h && h->abi_version==CIPHER_GBA_BIOS_HLE_ABI_VERSION && h->user && h->regs && h->cpsr && h->spsr && h->interrupt_enable && h->interrupt_flags && h->wait_mask && h->irq_return && h->irq_status && h->saved_registers && h->protected_bios_latch && h->unsupported_swi_count && h->irq_dispatch_count && h->halt_write_count && h->halted && h->set_halted && h->irq_active && h->set_irq_active && h->halt_swi_pending && h->set_halt_swi_pending && h->read && h->write && h->access_cycles && h->advance_clocks && h->cpu_irq && h->set_cpsr && h->clear_memory && h->prefetch_enabled && h->prime_rom_return;}

void cipher_gba_bios_hle_begin_irq(const CipherGbaHleHost* h){
    if(!cipher_gba_bios_hle_valid(h))return;
    auto& r=*reinterpret_cast<uint32_t (*)[16]>(h->regs);
    const uint16_t active=uint16_t(*h->interrupt_enable & *h->interrupt_flags);
    if(!active)return;
    const uint16_t mirror=uint16_t(h->read(h->user,0x03007ff8u,2u));
    h->write(h->user,0x03007ff8u,uint16_t(mirror|active),2u);
    for(unsigned i=0;i<4;i++)h->saved_registers[i]=r[i];
    h->saved_registers[4]=r[12];
    h->cpu_irq(h->user);
    h->advance_clocks(h->user,20u);
    *h->irq_return=r[14];
    *h->irq_status=*h->spsr;
    ++*h->irq_dispatch_count;
    *h->protected_bios_latch=0xE25EF004u;
    const auto handler=h->read(h->user,0x03007ffcu,4u);
    if(handler==0u||handler==0xffffffffu){
        *h->interrupt_flags &= uint16_t(~active);
        cipher_gba_bios_hle_finish_irq(h);
        return;
    }
    h->set_irq_active(h->user,1u);
    r[14]=0x00000100u;
    *h->cpsr=(*h->cpsr&~0x20u)|((handler&1u)?0x20u:0u);
    r[15]=alignDown(handler,(handler&1u)?2u:4u);
}
void cipher_gba_bios_hle_finish_irq(const CipherGbaHleHost* h){
    if(!cipher_gba_bios_hle_valid(h))return;
    auto& r=*reinterpret_cast<uint32_t (*)[16]>(h->regs);
    h->advance_clocks(h->user,11u);
    if(h->halt_swi_pending(h->user)){
        h->advance_clocks(h->user,21u);
        h->set_halt_swi_pending(h->user,0u);
    }
    for(unsigned i=0;i<4;i++)r[i]=h->saved_registers[i];
    r[12]=h->saved_registers[4];
    r[14]=*h->irq_return;
    h->set_cpsr(h->user,*h->irq_status);
    r[15]=alignDown(*h->irq_return-4u,(*h->cpsr&0x20u)?2u:4u);
    if(!*h->wait_mask && (r[15]>>24u>=8u && r[15]>>24u<=13u)){
        const unsigned first=h->access_cycles(h->user,r[15],2u,0u,1u);
        h->advance_clocks(h->user,first-1u);
    }
    h->set_irq_active(h->user,0u);
    *h->protected_bios_latch=0xE55EC002u;
}
void cipher_gba_bios_hle_complete_wait(const CipherGbaHleHost* h,uint16_t flags){
    if(!cipher_gba_bios_hle_valid(h))return;
    auto& r=*reinterpret_cast<uint32_t (*)[16]>(h->regs);
    h->advance_clocks(h->user,41u);
    const uint32_t resumePc=r[15];
    if((resumePc>>24u)>=8u && (resumePc>>24u)<=13u){
        const unsigned width=(*h->cpsr&0x20u)?2u:4u;
        const unsigned seq=h->access_cycles(h->user,resumePc+width,2u,1u,1u);
        const bool prefetch=h->prefetch_enabled(h->user)!=0;
        const unsigned clocks=width==2u?1u+seq+2u*unsigned(prefetch):5u+2u*seq-unsigned(prefetch);
        const unsigned firstBeat=h->access_cycles(h->user,resumePc,2u,0u,1u);
        const unsigned sequentialBeat=h->access_cycles(h->user,resumePc+width,2u,1u,1u);
        const unsigned extra=width==2u?(firstBeat>3u?firstBeat-3u:0u)+unsigned(prefetch&&firstBeat>2u):firstBeat+sequentialBeat-1u;
        h->advance_clocks(h->user,clocks+extra);
        h->prime_rom_return(h->user,resumePc,width,uint8_t(prefetch));
    }
    h->write(h->user,0x03007ff8u,uint16_t(flags&~*h->wait_mask),2u);
    *h->wait_mask=0;
    h->set_halted(h->user,0u);
    *h->protected_bios_latch=0xE3A02004u;
    h->advance_clocks(h->user,3u);
}
void cipher_gba_bios_hle_complete_halt(const CipherGbaHleHost* h){
    if(!cipher_gba_bios_hle_valid(h))return;
    h->advance_clocks(h->user,21u);
    h->set_halt_swi_pending(h->user,0);
}

uint32_t cipher_gba_bios_hle_swi(const CipherGbaHleHost* h, uint32_t number){
    if(!cipher_gba_bios_hle_valid(h))return 0u;
    auto& r = *reinterpret_cast<uint32_t (*)[16]>(h->regs);
    auto read8=[&](uint32_t a){return uint8_t(h->read(h->user,a,1));};
    auto read16=[&](uint32_t a){return uint16_t(h->read(h->user,a,2));};
    auto read32=[&](uint32_t a){return h->read(h->user,a,4);};
    auto write8=[&](uint32_t a,uint8_t v){h->write(h->user,a,v,1);};
    auto write16=[&](uint32_t a,uint16_t v){h->write(h->user,a,v,2);};
    auto write32=[&](uint32_t a,uint32_t v){h->write(h->user,a,v,4);};
    auto cpuAccessCycles=[&](uint32_t a,unsigned n,bool seq=false,bool fetch=false){return h->access_cycles(h->user,a,n,seq,fetch);};

    // A software interrupt flushes the cartridge instruction prefetch and
    // resumes with a nonsequential refill, even when no firmware is loaded.
    // HLE SWIs return to user code without running the real firmware, but the
    // observable protected BIOS bus remembers its last prefetched instruction.
    *h->protected_bios_latch=0xE3A02004u;
    switch(number){
        case 0x00: // SoftReset: partial BIOS-free reset preserving cartridge.
            // With real firmware the 0x03007ffa flag selects the startup address.
            r[15]=0x08000000u;
            h->set_cpsr(h->user,(*h->cpsr&~0x3fu)|0x1fu);
            r[13]=0x03007f00u;
            return 3;
        case 0x01: { // RegisterRamReset
            const unsigned flags=r[0];
            h->clear_memory(h->user,flags);
            return 3;
        }
        case 0x02: // Halt
            h->set_halt_swi_pending(h->user,1);
            h->set_halted(h->user,1);
            ++*h->halt_write_count;
            // The genuine BIOS takes 37 clocks from ARM SWI to HALTCNT;
            // unlike STOP, this entry path has been measured independently.
            return 37;
        case 0x03: // Stop (low-power semantics still incomplete)
            h->set_halted(h->user,1);
            ++*h->halt_write_count;
            return 3;
        case 0x04:case 0x05: { // IntrWait / VBlankIntrWait
            const unsigned clear=number==0x05?1u:r[0];
            const std::uint16_t mask=std::uint16_t(number==0x05?1u:r[1]);
            if(clear){
                const auto mirror=read16(0x03007ff8);
                write16(0x03007ff8,std::uint16_t(mirror&~mask));
            }
            // The BIOS enables IME while waiting. Without this, a game that
            // called IntrWait with IME=0 can wait forever even with IE set.
            write16(0x04000208u,1u);
            *h->wait_mask=mask;
            h->set_halted(h->user,(read16(0x03007ff8)&mask)==0);
            if(!h->halted(h->user)){
                const auto mirror=read16(0x03007ff8);
                write16(0x03007ff8,std::uint16_t(mirror&~mask));
                *h->wait_mask=0;
            }
            // The real SWI4 discard-flags path reaches HALTCNT after a
            // multi-instruction BIOS prologue and initial flag check.
            // Independent IWRAM ARM/Thumb fixtures measure 55 clocks to
            // first HALTCNT for IntrWait and 57 for VBlankIntrWait (its
            // two additional argument-setting instructions).
            return h->halted(h->user) ? (number==0x05 ? 57u : 55u) : 3u;
        }
        case 0x06:case 0x07: { // Div / DivArm
            const auto numerator=std::int64_t(std::int32_t(r[number==0x06?0:1]));
            const auto denominator=std::int64_t(std::int32_t(r[number==0x06?1:0]));
            const auto cycles=hleDivCycles(numerator,denominator,number==0x07);
            if(!denominator){
                // The original GBA BIOS division routine returns a signed
                // unit quotient and leaves the numerator as remainder when
                // its denominator is zero. This is observable via SWI 06/07.
                r[0]=numerator<0?0xffffffffu:1u;
                r[1]=std::uint32_t(numerator);
                r[3]=1u;
                return 12;
            }
            const auto q=numerator/denominator,rem=numerator%denominator;
            r[0]=std::uint32_t(q);r[1]=std::uint32_t(rem);
            r[3]=std::uint32_t(q<0?-q:q);
            return cycles;
        }
        case 0x09: { // ArcTan: fixed-point BIOS polynomial (no firmware bytes)
            const unsigned stages=multiplyByteStages(r[0]);
            hleArcTan(r[0],r[1],r[3]);
            // The original firmware path has a constant instruction count;
            // two multiplier sites each add one clock per additional byte.
            return 97u+2u*stages;
        }
        case 0x0a: { // ArcTan2: quadrant correction around fixed-point ArcTan
            // The BIOS uses an integer Div followed by the ArcTan polynomial.
            // Preserve its final r1 scratch result and its r3 epilogue value.
            const auto x=std::int32_t(r[0]);
            const auto y=std::int32_t(r[1]);
            if(y==0){
                r[0]=x<0?0x8000u:0u;
            }else if(x==0){
                r[0]=y<0?0xc000u:0x4000u;
            }else{
                const auto ax=x<0?-std::int64_t(x):std::int64_t(x);
                const auto ay=y<0?-std::int64_t(y):std::int64_t(y);
                const bool xMajor=(ax>ay)||(ax==ay && !(x<0 && y<0));
                const auto n=xMajor?y:x;
                const auto d=xMajor?x:y;
                // 32-bit ARM left shift precedes signed division.
                const auto shifted=std::int32_t(std::uint32_t(n)<<14u);
                const auto ratio=std::int32_t(std::int64_t(shifted)/std::int64_t(d));
                std::uint32_t angle=std::uint32_t(ratio),scratch=0,poly=0;
                hleArcTan(angle,scratch,poly);
                r[1]=scratch;
                r[0]= xMajor ? (x<0?0x8000u+angle:
                                      (y<0?0x10000u:0u)+angle)
                              : (y<0?0xc000u:0x4000u)-angle;
            }
            r[3]=0x170u;
            return 32;
        }
        case 0x08: { // Sqrt
            const auto cycles=hleSqrtCycles(r[0]);
            std::uint32_t root=std::uint32_t(std::sqrt(double(r[0])));
            while(std::uint64_t(root+1)*(root+1)<=r[0])++root;
            while(std::uint64_t(root)*root>r[0])--root;
            r[0]=root;
            return cycles;
        }
        case 0x0e:case 0x0f: { // BgAffineSet / ObjAffineSet
            const bool bg=number==0x0e;
            const unsigned count=r[2];
            // An invalid count must not make HLE hang or modify arbitrary
            // host memory; preserve the emulator instead of guessing.
            if(count>4096u){++*h->unsupported_swi_count;return 3;}
            std::uint32_t src=r[0],dst=r[1];
            const std::uint32_t stride=bg?2u:r[3];
            const auto& sine=biosSine();
            for(unsigned i=0;i<count;i++){
                const std::int32_t sx=signed16(read16(src+(bg?12u:0u)));
                const std::int32_t sy=signed16(read16(src+(bg?14u:2u)));
                const unsigned angle=read16(src+(bg?16u:4u))>>8;
                const std::int32_t cs=sine[(angle+64u)&255u],sn=sine[angle];
                const std::int32_t pa=std::int16_t(affineWord(sx,cs));
                const std::int32_t pb=std::int16_t(affineWord(sx,sn));
                const std::int32_t pc=std::int16_t(affineWord(sy,sn));
                const std::int32_t pd=std::int16_t(affineWord(sy,cs));
                if(bg){
                    write16(dst,std::uint16_t(pa));
                    write16(dst+2u,std::uint16_t(-pb));
                    write16(dst+4u,std::uint16_t(pc));
                    write16(dst+6u,std::uint16_t(pd));
                    const auto cx=std::int32_t(read32(src));
                    const auto cy=std::int32_t(read32(src+4u));
                    const auto px=signed16(read16(src+8u));
                    const auto py=signed16(read16(src+10u));
                    const std::int64_t dx=std::int64_t(cx)-std::int64_t(pa)*px+std::int64_t(pb)*py;
                    const std::int64_t dy=std::int64_t(cy)-std::int64_t(pc)*px-std::int64_t(pd)*py;
                    write32(dst+8u,std::uint32_t(dx));
                    write32(dst+12u,std::uint32_t(dy));
                    src+=20u;dst+=16u;
                }else{
                    write16(dst,std::uint16_t(pa));
                    write16(dst+stride,std::uint16_t(-pb));
                    write16(dst+stride*2u,std::uint16_t(pc));
                    write16(dst+stride*3u,std::uint16_t(pd));
                    src+=8u;dst+=stride*4u;
                }
            }
            // The firmware advances source and destination pointers in r0
            // and r1. The game may observe these call-clobbered registers.
            r[0]=src;
            r[1]=dst;
            return 3u+count*12u;
        }
        case 0x0b:case 0x0c: { // CpuSet / CpuFastSet
            const bool fast=number==0x0c;
            const bool fill=(r[2]&0x01000000u)!=0;
            const unsigned width=(fast||(r[2]&0x04000000u))?4u:2u;
            std::uint32_t count=r[2]&0x001fffffu;
            if(fast)count=(count+7u)&~7u;
            // Preserve the preexisting empty-transfer behavior: no original
            // firmware timing measurement is available for a zero count.
            if(count==0u)return 3u;
            const std::uint32_t src=r[0];
            // The GBA firmware's CpuSet/CpuFastSet routines refuse copy
            // requests whose source range overlaps BIOS/unused low memory.
            // This is a software guard independent of the hardware's
            // protected BIOS read-latch behavior. Leave destination intact.
            const std::uint64_t transferBytes=std::uint64_t(fill?1u:count)*width;
            const std::uint64_t lastByte=std::uint64_t(src)+transferBytes-1u;
            if(src<0x02000000u || (lastByte>0xffffffffULL &&
               std::uint32_t(lastByte)<0x02000000u))
                // Measured in IWRAM using the unmodified firmware: the
                // guard exits through the normal SWI epilogue, costing 85
                // clocks for CpuSet and 87 for CpuFastSet. No copy occurs.
                return fast?87u:85u;
            const std::uint32_t initialDst=r[1];
            std::uint32_t dst=initialDst;
            // BIOS loads use ARM7TDMI unaligned semantics. In particular an
            // odd LDRH returns the addressed byte, while an unaligned LDR
            // rotates the aligned word. On the 8-bit cartridge save bus,
            // the selected *addressed byte* is repeated across word lanes.
            const auto load=[&](std::uint32_t address)->std::uint32_t {
                if(width==2u){
                    if(address&1u)return read8(address);
                    return read16(address);
                }
                if((address>>24u)==14u||(address>>24u)==15u)
                    return read32(address);
                // The observed BIOS block-copy output uses the aligned
                // source word for ordinary 32-bit memory regions.
                return read32(address&~3u);
            };
            const std::uint32_t transferSrc=src;
            for(std::uint32_t i=0;i<count;i++){
                const std::uint32_t address=transferSrc+(fill?0u:i*width);
                const std::uint32_t value=load(address);
                if(width==4u)write32(((dst>>24u)==14u||(dst>>24u)==15u)?dst:(dst&~3u),value);
                else write16(dst&~1u,std::uint16_t(value));
                dst+=width;
            }
            // The original ROM's 16-bit helper preserves the pointer
            // registers. The word-copy helper advances both, except that
            // FastSet fill leaves its source register unchanged.
            if(fast){
                if(!fill)r[0]=src+count*4u;
                r[1]=dst;
            }else if(width==4u){
                r[0]=src+(fill?(count?4u:0u):count*4u);
                r[1]=dst;
            }
            if(fast){
                // The original firmware processes eight words per LDM/STM
                // block. Each block has bus occupation for eight reads and
                // writes, plus seven ARM internal clocks. The fixed 94 clocks
                // cover SWI dispatch, stack setup and return. In fill mode
                // there is a single source load and each block has five
                // internal clocks. These phase counts were measured against
                // the original BIOS with IWRAM/EWRAM/ROM/SRAM endpoints,
                // across transfer sizes of 8..512 words.
                const std::uint32_t groups=count/8u;
                unsigned cost=fill?103u+cpuAccessCycles(src,4u,false):94u;
                for(std::uint32_t group=0;group<groups;group++){
                    cost+=fill?5u:7u;
                    for(unsigned word=0;word<8u;word++){
                        const std::uint32_t index=group*8u+word;
                        if(!fill)
                            cost+=cpuAccessCycles(src+index*4u,4u,word!=0u);
                        cost+=cpuAccessCycles(initialDst+index*4u,4u,word!=0u);
                    }
                }
                return cost;
            }
            // CpuSet executes a load/store loop rather than a single bulk
            // transfer. The inner loop has nine internal clocks per halfword
            // or eight per word (seven/six in fill mode). BIOS entry/exit
            // costs 97/95 clocks respectively. Count one bus transaction
            // per operand, using nonsequential timings because the load and
            // store alternate. Verified with original-BIOS IWRAM, EWRAM,
            // ROM and SRAM fixtures for multiple lengths and fill modes.
            unsigned cost=width==4u?95u:97u;
            if(fill)cost+=cpuAccessCycles(src,width,false);
            const unsigned bodyClocks=width==4u?(fill?6u:8u):(fill?7u:9u);
            for(std::uint32_t i=0;i<count;i++){
                if(!fill)cost+=cpuAccessCycles(src+i*width,width,false);
                cost+=bodyClocks+cpuAccessCycles(initialDst+i*width,width,false);
            }
            return cost;
        }
        case 0x11:case 0x12: { // LZ77UnCompWram / LZ77UnCompVram
            std::uint32_t src=r[0],dst=r[1];
            const std::uint32_t header=read32(src);src+=4;
            if((header&255u)!=0x10){++*h->unsupported_swi_count;return 3;}
            const unsigned size=header>>8;
            std::vector<std::uint8_t> output;
            output.reserve(size);
            while(output.size()<size){
                const unsigned bits=read8(src++);
                for(unsigned bit=0;bit<8&&output.size()<size;bit++){
                    if(!(bits&(0x80u>>bit)))output.push_back(read8(src++));
                    else {
                        const unsigned first=read8(src++),second=read8(src++);
                        const unsigned length=(first>>4)+3;
                        const unsigned distance=(((first&15u)<<8)|second)+1;
                        if(distance>output.size()){
                            ++*h->unsupported_swi_count;return 3;
                        }
                        for(unsigned j=0;j<length&&output.size()<size;j++)
                            output.push_back(output[output.size()-distance]);
                    }
                }
            }
            if(number==0x12){
                // VRAM decompression writes halfwords even in BG/OBJ VRAM.
                for(unsigned i=0;i<output.size();i+=2)
                    write16(dst+i,std::uint16_t(output[i]|((i+1<output.size()?output[i+1]:0)<<8)));
            }else for(unsigned i=0;i<output.size();i++)write8(dst+i,output[i]);
            return 3+size;
        }
        case 0x14:case 0x15: { // RLUnCompWram / RLUnCompVram
            std::uint32_t src=r[0],dst=r[1];
            const std::uint32_t header=read32(src);src+=4;
            if((header&255u)!=0x30){++*h->unsupported_swi_count;return 3;}
            const unsigned size=header>>8;
            std::vector<std::uint8_t> output;
            output.reserve(size);
            while(output.size()<size){
                const unsigned block=read8(src++),count=(block&0x7f)+((block&0x80)?3u:1u);
                if(block&0x80){
                    const auto value=read8(src++);
                    for(unsigned i=0;i<count&&output.size()<size;i++)output.push_back(value);
                }else for(unsigned i=0;i<count&&output.size()<size;i++)output.push_back(read8(src++));
            }
            if(number==0x15){
                for(unsigned i=0;i<output.size();i+=2)
                    write16(dst+i,std::uint16_t(output[i]|((i+1<output.size()?output[i+1]:0)<<8)));
            }else for(unsigned i=0;i<output.size();i++)write8(dst+i,output[i]);
            return 3+size;
        }
        default:
            ++*h->unsupported_swi_count;
            return 3;
    }
}

}
