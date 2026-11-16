// ROM-free synthetic Thumb/I/O timing regression. No BIOS or game required.
// Verifies implemented clock phases; not a physical GBA accuracy certificate.
#include "pixelgba/Core.hpp"
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
namespace {
constexpr std::uint32_t RAM=0x03000000u,DISP=0x04000004u,TIMER=0x04000100u;
void test(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void setup(pixelgba::GbaCore& c,std::uint32_t address) {
    c.write16(RAM,0x8808u); // LDRH r0,[r1,#0]
    c.write16(RAM+2,0xE7FEu);
    c.cpu().r[1]=address;c.cpu().r[15]=RAM;c.cpu().cpsr|=0x20u;
}
void checkDisplay() {
    for(unsigned phase=1000;phase<=1010;phase++){
        auto c=std::make_unique<pixelgba::GbaCore>();
        setup(*c,DISP);c->advanceHardware(phase);
        test(c->step()==3u,"Thumb LDRH clocks");
        test(c->cycles()==phase+3u,"LDRH cycle drift");
        test(((c->cpu().r[0]&2u)!=0u)==(phase>=1005u),"DISPSTAT sample clock");
        test(c->programCounter()==RAM+2,"LDRH PC increment");
    }
}
void checkLoop() {
    auto c=std::make_unique<pixelgba::GbaCore>();
    // Synthetic Thumb pattern: LDRH r0,[r1]; MOVS r2,#2; ANDS r0,r2;
    // BEQ start; NOP. This is not copied from an accuracy ROM.
    c->write16(RAM,0x8808u);c->write16(RAM+2,0x2202u);
    c->write16(RAM+4,0x4010u);c->write16(RAM+6,0xD0FBu);
    c->write16(RAM+8,0x46C0u);
    c->cpu().r[15]=RAM;c->cpu().r[1]=DISP;c->cpu().cpsr|=0x20u;
    c->advanceHardware(992u);
    for(unsigned i=0;i<2;i++){
        const auto begin=c->cycles();
        test(c->step()==3u,"poll LDRH clocks");
        test(!(c->cpu().r[0]&2u),"early HBlank");
        test(c->step()==1u,"MOVS clocks");
        test(c->step()==1u,"ANDS clocks");
        test(c->step()==3u,"taken BEQ clocks");
        test(c->cycles()==begin+8u&&c->programCounter()==RAM,"8-clock polling loop");
    }
    test(c->cycles()==1008u,"polling event alignment");
    test(c->step()==3u&&(c->cpu().r[0]&2u),"HBlank poll transition");
    test(c->step()==1u&&c->step()==1u,"post-edge ALU cost");
    test(c->step()==1u&&c->programCounter()==RAM+8u,"untaken BEQ");
    test(c->cycles()==1014u,"poll exit clock");
}
void checkTimer() {
    auto c=std::make_unique<pixelgba::GbaCore>();
    setup(*c,TIMER);c->write16(TIMER,0);c->write16(TIMER+2,0x80);
    c->advanceHardware(20u);
    const auto old=c->read16(TIMER);
    test(old==19u,"Timer0 start latch");
    test(c->step()==3u,"Timer LDRH clocks");
    test(c->cpu().r[0]==old,"Timer0 sampled wrong phase");
    test(c->read16(TIMER)==old+3u,"Timer0 did not run during instruction");
}
}
int main() {
    try{checkDisplay();checkLoop();checkTimer();
        std::cout<<"PASS: Thumb LDRH phase, 8-clock HBlank polling, Timer0 latch\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
