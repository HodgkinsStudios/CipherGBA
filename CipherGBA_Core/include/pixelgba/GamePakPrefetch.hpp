// Created by Jacob Hodgkins.
#pragma once
#include <cstdint>
#include <algorithm>
#include <limits>

namespace pixelgba {
// Cycle-domain model of the GBA Game Pak's 16-bit ROM bus and eight-halfword
// CPU prefetch buffer. This component is intentionally separate from the
// interpreter's older fetch-overlap estimator: callers must schedule the
// resulting transactions at real ARM7TDMI pipeline phases before making it
// authoritative for CPU timing. No ROM bytes or guessed test outcomes live here.
class GamePakPrefetch {
public:
    struct Fetch {
        unsigned cycles=1;       // elapsed bus/fifo delivery latency
        unsigned hits=0;         // buffered 16-bit halfwords delivered
        unsigned misses=0;       // synchronous 16-bit ROM transactions
    };
    struct State {
        std::uint32_t head=0;     // address of first buffered halfword
        std::uint32_t next=0;     // next physical halfword to be read
        std::uint16_t waitcnt=0;
        std::uint8_t buffered=0;  // <= 8
        std::uint8_t inflight=0;  // 0 or 1, partial background transaction
        unsigned remaining=0;    // cycles remaining in that transaction
        bool valid=false;
        bool sequential=false;
    };

    static bool isRom(std::uint32_t addr) {
        const unsigned r=addr>>24;
        return r>=8u&&r<=13u;
    }
    static bool enabled(std::uint16_t waitcnt){return (waitcnt&0x4000u)!=0;}
    static unsigned busCycles(std::uint32_t addr,bool sequential,std::uint16_t waitcnt){
        const unsigned bank=((addr>>24)-8u)/2u;
        if((addr&0x1ffffu)==0)sequential=false;
        constexpr unsigned n[4]={4,3,2,8};
        constexpr unsigned s[3][2]={{2,1},{4,1},{8,1}};
        return 1u+(sequential?s[bank][(waitcnt>>(4u+3u*bank))&1u]
                               :n[(waitcnt>>(2u+3u*bank))&3u]);
    }
    void reset(){state_={};}
    State state() const {return state_;}
    // Reject corrupt snapshots rather than accepting impossible FIFO contents.
    bool restore(const State& state){
        if(state.buffered>8 || state.inflight>1 ||
           (state.inflight && (!state.remaining||!state.valid||state.buffered==8)) ||
           (!state.inflight && state.remaining) ||
           (!state.valid && (state.buffered||state.inflight||state.remaining)) ||
           (state.valid && (!isRom(state.head)||!isRom(state.next)||
              ((state.head|state.next)&1u)!=0 ||
              state.next!=state.head+2u*state.buffered)))return false;
        state_=state;return true;
    }
    // A WAITCNT change invalidates previous buffered transactions and their
    // partially elapsed wait states; even if prefetch stays enabled, the
    // cartridge region's S/N timing may have changed.
    void configure(std::uint16_t waitcnt){
        if(state_.waitcnt!=waitcnt){reset();state_.waitcnt=waitcnt;}
    }
    void externalBusAccess(){
        // ROM data transfer or DMA may claim the 16-bit bus and force the
        // instruction prefetch stream to restart nonsequentially.
        const auto waitcnt=state_.waitcnt;
        reset();state_.waitcnt=waitcnt;
    }
    void branch(){externalBusAccess();}
    Fetch instruction(std::uint32_t pc,unsigned bytes,bool sequential,std::uint16_t waitcnt,
                      bool refillDuringDelivery=false){
        (void)sequential; // FIFO stream identity, not caller guesses, defines S/N.
        configure(waitcnt);
        if(!enabled(waitcnt)||!isRom(pc)){
            externalBusAccess();
            return {};
        }
        pc&=~1u;
        // A branch, ARM/Thumb switch or mismatched address cannot consume an
        // unrelated queued halfword from the previous execution stream.
        if(!state_.valid || state_.head!=pc){
            state_.valid=true;
            state_.head=pc;
            state_.next=pc;
            state_.buffered=0;
            state_.inflight=0;
            state_.remaining=0;
            state_.sequential=false;
        }
        Fetch result{0,0,0};
        const unsigned count=bytes==4u?2u:1u;
        for(unsigned i=0;i<count;i++){
            const auto address=pc+2u*i;
            if(state_.buffered){
                --state_.buffered;
                ++result.hits;
            } else {
                // If the background beat was interrupted, wait only for its
                // outstanding clocks, not the whole N/S interval again.
                if(state_.inflight){
                    result.cycles+=state_.remaining;
                    state_.remaining=0;state_.inflight=0;
                } else {
                    // The initial cold fetch is nonsequential; uninterrupted
                    // code stream and FIFO fill proceed sequentially.
                    result.cycles+=busCycles(address,state_.sequential,waitcnt);
                }
                ++result.misses;
                state_.next=address+2u;
                state_.sequential=true;
            }
            state_.head+=2u;
        }
        // CPU still requires an instruction-delivery cycle even if all its
        // halfwords were already in the FIFO.
        result.cycles=std::max(1u,result.cycles);
        // A complete FIFO hit uses the CPU's local delivery path. If the
        // caller grants the external bus during that clock, its next beat
        // can progress concurrently. A miss already occupies that bus.
        if(refillDuringDelivery && !result.misses)internalCycles(1u,waitcnt);
        return result;
    }
    void internalCycles(unsigned cycles,std::uint16_t waitcnt){
        configure(waitcnt);
        if(!enabled(waitcnt)||!state_.valid||!isRom(state_.next))return;
        while(cycles && state_.buffered<8u){
            if(!state_.inflight){
                state_.remaining=busCycles(state_.next,state_.sequential,waitcnt);
                state_.inflight=1;
            }
            const unsigned spent=std::min(cycles,state_.remaining);
            cycles-=spent;state_.remaining-=spent;
            if(state_.remaining==0){
                ++state_.buffered;
                state_.next+=2u;
                state_.sequential=true;
                state_.inflight=0;
            }
        }
    }
    // Read-only readiness of complete FIFO halfwords at fixed WAITCNT.
    // Includes an in-flight beat and the physical N/S clocks of future beats.
    // An invalid/mismatched stream cannot promise delivery before DMA claims.
    unsigned clocksUntilAvailable(unsigned count,std::uint16_t waitcnt) const {
        constexpr auto unavailable=std::numeric_limits<unsigned>::max();
        if(!count)return 0u;
        if(count>8u || !state_.valid || !enabled(waitcnt) ||
           state_.waitcnt!=waitcnt)return unavailable;
        if(state_.buffered>=count)return 0u;
        unsigned missing=count-state_.buffered,clocks=0;
        auto address=state_.next;bool sequential=state_.sequential;
        if(state_.inflight){
            clocks=state_.remaining;--missing;address+=2u;sequential=true;
        }
        while(missing){
            if(!isRom(address))return unavailable;
            const auto span=busCycles(address,sequential,waitcnt);
            if(clocks>unavailable-span)return unavailable;
            clocks+=span;--missing;address+=2u;sequential=true;
        }
        return clocks;
    }
    unsigned available() const {return state_.buffered;}
private:
    State state_{};
};
}
