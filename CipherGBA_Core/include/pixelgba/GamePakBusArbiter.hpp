// Copyright (c) HodgkinsStudios. Original CipherGBA implementation.
#pragma once
#include "pixelgba/GamePakPrefetch.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pixelgba {
// Deterministic *shadow* scheduler for the shared 16-bit Game Pak bus.
//
// Requests are bus beats, not full CPU instructions. Arbitration occurs only
// at beat boundaries; an already-started Game Pak transfer is nonpreemptible.
// DMA0..3 outrank CPU data, CPU fetch, and opportunistic prefetch, in channel
// order for simultaneous requests. These priorities are a modeled scheduling
// policy awaiting cycle-level CPU/DMA integration, NOT a claim that the existing
// emulator's aggregate instruction timestamps represent physical grant times.
// This class does not advance real emulated time or change save-state layout.
class GamePakBusArbiter {
public:
    enum class Master : unsigned {
        CpuFetch=0, CpuData=1, Dma0=2, Dma1=3, Dma2=4, Dma3=5, Prefetch=6
    };
    struct Request {
        Master master=Master::CpuFetch;
        std::uint32_t address=0;
        std::uint32_t tag=0;
        bool sequential=false;
        std::uint8_t beat=0;
    };
    struct Grant {
        Request request{};
        std::uint64_t begin=0;
        std::uint64_t end=0; // exclusive
        unsigned cycles=0;
        std::uint16_t waitcnt=0;
        bool sequential=false;
    };

    void reset(){
        for(auto& q:pending_)q.clear();
        done_.clear();active_={};occupied_=false;
        cycle_=0;lastAddress_=0;lastMaster_=Master::Prefetch;
        lastValid_=false;
    }
    // Enqueue an aligned halfword or word as one/two atomic-size requests.
    // A 32-bit word uses two 16-bit bus beats and may yield to a higher-priority
    // request *between* them. The original request's tag survives both beats.
    void request(Master master,std::uint32_t address,unsigned bytes,
                 bool sequential,std::uint32_t tag=0){
        if(!GamePakPrefetch::isRom(address) ||
           (bytes!=2u && bytes!=4u) || static_cast<unsigned>(master)>6u)
            throw std::invalid_argument("GamePak bus request must be ROM halfword/word");
        const auto base=address&~(bytes-1u);
        auto& queue=pending_[static_cast<unsigned>(master)];
        for(unsigned b=0;b<bytes/2u;++b){
            const auto a=base+2u*b;
            if(!GamePakPrefetch::isRom(a))
                throw std::invalid_argument("GamePak bus request crosses ROM mapping");
            queue.push_back({master,a,tag,sequential || b!=0u,
                            static_cast<std::uint8_t>(b)});
        }
    }
    // Discard speculative background work when a CPU data access, DMA or
    // redirected instruction fetch invalidates the previous prefetch stream.
    // An active (nonpreemptible) prefetch beat finishes; a future scheduler
    // can model the abandoned partial transfer explicitly.
    void cancelQueuedPrefetch(){pending_[6].clear();}
    // Each call advances exactly one virtual master clock. WAITCNT is sampled
    // when a new beat starts and latched until it finishes, so mid-beat writes
    // cannot retroactively shorten an active transfer.
    void tick(std::uint16_t waitcnt){
        if(!occupied_)start(waitcnt);
        ++cycle_;
        if(occupied_ && cycle_==active_.end){
            lastAddress_=active_.request.address;
            lastMaster_=active_.request.master;
            lastValid_=true;
            done_.push_back(active_);
            occupied_=false;
        }
    }
    void advance(unsigned cycles,std::uint16_t waitcnt){
        while(cycles--)tick(waitcnt);
    }
    void drain(std::uint16_t waitcnt,unsigned maxCycles=1000000u){
        while((occupied_||pendingCount()) && maxCycles--)tick(waitcnt);
        if(occupied_||pendingCount())throw std::runtime_error("GamePak arbiter drain limit");
    }
    std::uint64_t cycle()const{return cycle_;}
    bool active()const{return occupied_;}
    std::size_t pendingCount()const{
        std::size_t n=0;
        for(const auto& q:pending_)n+=q.size();
        return n;
    }
    const std::vector<Grant>& completed()const{return done_;}
    void clearCompleted(){done_.clear();}
private:
    static constexpr std::array<unsigned,7> priority_={{2,3,4,5,1,0,6}};
    void start(std::uint16_t waitcnt){
        for(const unsigned m:priority_){
            auto& q=pending_[m];
            if(q.empty())continue;
            const Request r=q.front();q.pop_front();
            // Ownership changes and 128 KiB boundaries terminate S bursts.
            // A DMA channel retains its Game Pak burst across source and
            // destination accesses, even when their ROM addresses differ.
            // CPU instruction/data streams instead require contiguous beats.
            const bool dmaMaster=static_cast<unsigned>(r.master)>=2u &&
                static_cast<unsigned>(r.master)<=5u;
            const bool continuous=lastValid_ && lastMaster_==r.master &&
                (dmaMaster || r.address==lastAddress_+2u) &&
                (dmaMaster || (r.address>>25u)==(lastAddress_>>25u));
            const bool sequential=r.sequential && continuous &&
                ((r.address&0x1ffffu)!=0u);
            const unsigned clocks=GamePakPrefetch::busCycles(r.address,sequential,waitcnt);
            active_={r,cycle_,cycle_+clocks,clocks,waitcnt,sequential};
            occupied_=true;
            return;
        }
        // No bus request: give the external bus an idle clock; no implicit
        // instruction or DMA traffic is invented by this diagnostic model.
    }
    std::array<std::deque<Request>,7> pending_{};
    std::vector<Grant> done_{};
    Grant active_{};
    std::uint64_t cycle_=0;
    std::uint32_t lastAddress_=0;
    Master lastMaster_=Master::Prefetch;
    bool occupied_=false,lastValid_=false;
};
} // namespace pixelgba
