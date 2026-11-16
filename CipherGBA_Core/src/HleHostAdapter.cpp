// Created by HodgkinsStudios. Core-to-portable BIOS service bridge.
// Contains no firmware behavior: only host memory/CPU/scheduler callbacks.
#include "pixelgba/Core.hpp"
#include "pixelgba/HleInternal.hpp"
#include <algorithm>
namespace pixelgba {
CipherGbaHleHost GbaCore::makeHleHost(){
    CipherGbaHleHost h{};
    h.abi_version=CIPHER_GBA_BIOS_HLE_ABI_VERSION;
    h.user=this;
    h.regs=units_.cpu.r.data();
    h.cpsr=&units_.cpu.cpsr;
    h.spsr=&units_.cpu.spsr;
    h.interrupt_enable=&units_.interrupts.interruptEnable;
    h.interrupt_flags=&units_.interrupts.interruptFlags;
    h.wait_mask=&hleWaitMask_;
    h.irq_return=&hleIrqReturn_;
    h.irq_status=&hleIrqStatus_;
    h.saved_registers=hleSavedRegisters_.data();
    h.protected_bios_latch=&biosPrefetchLatch_;
    h.unsupported_swi_count=&unsupportedHleSwis_;
    h.irq_dispatch_count=&hleIrqDispatches_;
    h.halt_write_count=&haltWrites_;
    h.halted=[](void* p)->uint8_t{return static_cast<GbaCore*>(p)->units_.cpu.halted;};
    h.set_halted=[](void* p,uint8_t x){static_cast<GbaCore*>(p)->units_.cpu.halted=x!=0;};
    h.irq_active=[](void* p)->uint8_t{return static_cast<GbaCore*>(p)->hleIrqActive_;};
    h.set_irq_active=[](void* p,uint8_t x){static_cast<GbaCore*>(p)->hleIrqActive_=x!=0;};
    h.halt_swi_pending=[](void* p)->uint8_t{return static_cast<GbaCore*>(p)->hleHaltSwiPending_;};
    h.set_halt_swi_pending=[](void* p,uint8_t x){static_cast<GbaCore*>(p)->hleHaltSwiPending_=x!=0;};
    h.read=[](void* p,uint32_t a,uint32_t width)->uint32_t{
        auto& c=*static_cast<GbaCore*>(p);
        return width==1?c.read8(a):width==2?c.read16(a):c.read32(a);
    };
    h.write=[](void* p,uint32_t a,uint32_t v,uint32_t width){
        auto& c=*static_cast<GbaCore*>(p);
        if(width==1)c.write8(a,uint8_t(v));
        else if(width==2)c.write16(a,uint16_t(v));
        else c.write32(a,v);
    };
    h.access_cycles=[](void* p,uint32_t a,uint32_t width,uint8_t seq,uint8_t fetch)->uint32_t{
        return static_cast<GbaCore*>(p)->cpuAccessCycles(a,width,seq!=0,fetch!=0);
    };
    h.advance_clocks=[](void* p,uint32_t n){static_cast<GbaCore*>(p)->advanceHardware(n);};
    h.cpu_irq=[](void* p){static_cast<GbaCore*>(p)->units_.cpu.irq();};
    h.set_cpsr=[](void* p,uint32_t v){static_cast<GbaCore*>(p)->units_.cpu.setCpsr(v);};
    h.clear_memory=[](void* p,uint32_t flags){
        auto& c=*static_cast<GbaCore*>(p);
        if(flags&1u)c.ewram_.fill(0);
        if(flags&2u)std::fill(c.iwram_.begin(),c.iwram_.begin()+0x7e00,0);
        if(flags&4u)c.palette_.fill(0);
        if(flags&8u)c.vram_.fill(0);
        if(flags&16u)c.oam_.fill(0);
    };
    h.prefetch_enabled=[](void* p)->uint8_t{return static_cast<GbaCore*>(p)->gamePakPrefetchEnabled();};
    h.prime_rom_return=[](void* p,uint32_t pc,uint32_t width,uint8_t prefetch){
        auto& c=*static_cast<GbaCore*>(p);
        c.units_.cpu.fetchValid_=true;
        c.units_.cpu.expectedFetchPc_=pc;
        c.units_.cpu.lastFetchThumb_=c.units_.cpu.thumb();
        c.units_.cpu.lastStepHadDataAccess_=false;
        if(prefetch){
            c.gamePakPrefetch_.branch();
            c.primeBranchTargetPair(pc,width);
        }
    };
    return h;
}
unsigned GbaCore::handleHleSwi(unsigned number){
    auto host=makeHleHost();return cipher_gba_bios_hle_swi(&host,number);
}
void GbaCore::beginHleIrq(){auto host=makeHleHost();cipher_gba_bios_hle_begin_irq(&host);}
void GbaCore::finishHleIrq(){auto host=makeHleHost();cipher_gba_bios_hle_finish_irq(&host);}
void GbaCore::finishHleWait(std::uint16_t flags){auto host=makeHleHost();cipher_gba_bios_hle_complete_wait(&host,flags);}
void GbaCore::finishHleHalt(){auto host=makeHleHost();cipher_gba_bios_hle_complete_halt(&host);}
} // namespace pixelgba
