// Copyright (c) HodgkinsStudios. Independent GBA bus timing research.
#pragma once
#include "pixelgba/GamePakBusArbiter.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pixelgba {
// observational clock-domain scheduling layer.
//
// Each request enters at its *specified* ready clock; the shared cartridge
// bus arbitrates at 16-bit beat boundaries. A DMA transfer arriving while an
// instruction beat is in flight cannot cancel the in-flight portion, but can
// claim the bus at the next halfword boundary. The CPU's fetch/decode/execute
// overlap is intentionally expressed by caller-supplied ready clocks: this
// utility MUST NOT be used to charge interpreter cycles without validating
// those stage-ready times against independent ARM7TDMI observations.
//
// branchTargetPair has a separately validated, allocation-free live
// consumer for same-state unbuffered ROM branches. The general contention
// scheduler and its caller-supplied readiness clocks remain diagnostic.
//
// This model owns no live emulator state, has no ABI/save-state footprint, and
// does not inspect ROM contents or timing-test expected values.
class GamePakCycleTimeline {
public:
    using Master=GamePakBusArbiter::Master;
    struct Transfer {
        std::uint64_t ready=0; // relative to the start of this trace
        Master master=Master::CpuFetch;
        std::uint32_t address=0;
        unsigned bytes=2;
        bool sequential=false;
        std::uint32_t tag=0;
        // Optional pipeline dependency. This transfer may only issue after
        // the complete earlier word/halfword (all of its beats) has retired.
        // Existing six-field initializers remain independent/ABI-free.
        std::uint32_t afterTag=std::numeric_limits<std::uint32_t>::max();
    };
    // Two destination deliveries performed by a taken branch. The source
    // lookahead fetch is a separate CPU phase, and the third destination
    // lookahead belongs to the subsequent instruction. Fixed WAITCNT and
    // no competing grants permit this allocation-free closed form; tests
    // compare it with the dependency-aware per-halfword scheduler below.
    struct BranchTargetPair {
        std::array<unsigned,2> clocks{};
        unsigned total() const {return clocks[0]+clocks[1];}
    };
    static BranchTargetPair branchTargetPair(std::uint32_t target,bool thumb,
                                             std::uint16_t waitcnt){
        BranchTargetPair out;
        if(!GamePakPrefetch::isRom(target))return out;
        const unsigned width=thumb?2u:4u;
        target&=~(width-1u);
        for(unsigned i=0;i<2;++i){
            const auto a=target+i*width;
            out.clocks[i]=GamePakPrefetch::busCycles(a,i!=0u,waitcnt);
            if(width==4u)out.clocks[i]+=GamePakPrefetch::busCycles(a+2u,true,waitcnt);
        }
        return out;
    }
    struct WaitcntWrite {
        std::uint64_t at=0;
        std::uint16_t value=0;
    };
    struct Result {
        std::uint64_t elapsed=0;
        std::uint64_t lastCpuFetchEnd=0;
        std::uint64_t lastCpuDataEnd=0;
        std::uint64_t lastDmaEnd=0;
        std::uint64_t idleCycles=0;
        std::vector<GamePakBusArbiter::Grant> grants;
        // A lower bound on instruction retirement when external fetch/data
        // grants and CPU internal execution may run concurrently. Addition
        // would double-charge overlapping clocks. This deliberately does
        // not model decode dependencies or pipeline readiness.
        std::uint64_t earliestCpuCompletion(std::uint64_t internalReady) const {
            return std::max(internalReady,
                            std::max(lastCpuFetchEnd,lastCpuDataEnd));
        }
    };
    static Result schedule(std::vector<Transfer> requests,
                           std::uint16_t initialWaitcnt,
                           std::vector<WaitcntWrite> writes={},
                           std::uint64_t maxCycles=1000000u){
        // Retain the proven fast path for existing independent observations.
        // Linked pipeline requests are released only when the preceding
        // *complete transfer*, including both beats of an ARM word, finishes.
        const auto none=std::numeric_limits<std::uint32_t>::max();
        const bool dependent=std::any_of(requests.begin(),requests.end(),
            [none](const Transfer& t){return t.afterTag!=none;});
        if(dependent){
            std::unordered_map<std::uint32_t,std::size_t> jobs;
            for(std::size_t i=0;i<requests.size();++i){
                if(requests[i].tag==none || !jobs.emplace(requests[i].tag,i).second)
                    throw std::invalid_argument("Game Pak dependency requires unique transfer tags");
            }
            for(const auto& r:requests){
                if(r.afterTag!=none && (!jobs.count(r.afterTag) || r.afterTag==r.tag))
                    throw std::invalid_argument("Game Pak dependency missing or self-referential");
            }
            std::stable_sort(writes.begin(),writes.end(),
                [](const WaitcntWrite& a,const WaitcntWrite& b){return a.at<b.at;});
            GamePakBusArbiter bus;
            std::vector<bool> enqueued(requests.size(),false);
            std::vector<unsigned> completed(requests.size(),0u);
            std::size_t sent=0,write=0,observed=0;
            std::uint16_t waitcnt=initialWaitcnt;
            Result out;
            while(sent<requests.size() || bus.pendingCount() || bus.active()){
                if(bus.cycle()>=maxCycles)
                    throw std::runtime_error("Game Pak dependency cycle/deadlock limit");
                const auto now=bus.cycle();
                while(write<writes.size() && writes[write].at<=now)
                    waitcnt=writes[write++].value;
                // Scan in original caller order: same-ready CPU and DMA work
                // is enqueued together, then the arbiter selects priorities.
                for(std::size_t i=0;i<requests.size();++i){
                    const auto& r=requests[i];
                    if(enqueued[i] || r.ready>now)continue;
                    if(r.afterTag!=none){
                        const auto j=jobs.at(r.afterTag);
                        if(completed[j]<requests[j].bytes/2u)continue;
                    }
                    bus.request(r.master,r.address,r.bytes,r.sequential,r.tag);
                    enqueued[i]=true;++sent;
                }
                if(!bus.pendingCount() && !bus.active())++out.idleCycles;
                bus.tick(waitcnt);
                const auto& grants=bus.completed();
                while(observed<grants.size()){
                    const auto it=jobs.find(grants[observed].request.tag);
                    if(it!=jobs.end())++completed[it->second];
                    ++observed;
                }
            }
            out.elapsed=bus.cycle();out.grants=bus.completed();
            for(const auto& g:out.grants){
                if(g.request.master==Master::CpuFetch)
                    out.lastCpuFetchEnd=std::max(out.lastCpuFetchEnd,g.end);
                else if(g.request.master==Master::CpuData)
                    out.lastCpuDataEnd=std::max(out.lastCpuDataEnd,g.end);
                else if(static_cast<unsigned>(g.request.master)>=2u &&
                        static_cast<unsigned>(g.request.master)<=5u)
                    out.lastDmaEnd=std::max(out.lastDmaEnd,g.end);
            }
            return out;
        }
        // stable_sort preserves requested ordering for simultaneous CPU fetch
        // stages and same-channel DMA requests, while allowing other masters
        // to arbitrate by hardware priority.
        std::stable_sort(requests.begin(),requests.end(),
                         [](const Transfer& a,const Transfer& b){return a.ready<b.ready;});
        std::stable_sort(writes.begin(),writes.end(),
                         [](const WaitcntWrite& a,const WaitcntWrite& b){return a.at<b.at;});
        GamePakBusArbiter bus;
        std::size_t next=0,write=0;
        std::uint16_t waitcnt=initialWaitcnt;
        Result out;
        while(next<requests.size() || bus.pendingCount() || bus.active()){
            if(bus.cycle()>=maxCycles)throw std::runtime_error("Game Pak timeline cycle limit");
            const auto now=bus.cycle();
            while(write<writes.size() && writes[write].at<=now)
                waitcnt=writes[write++].value;
            while(next<requests.size() && requests[next].ready<=now){
                const auto& r=requests[next++];
                bus.request(r.master,r.address,r.bytes,r.sequential,r.tag);
            }
            if(!bus.pendingCount() && !bus.active())++out.idleCycles;
            bus.tick(waitcnt);
        }
        out.elapsed=bus.cycle();
        out.grants=bus.completed();
        for(const auto& g:out.grants){
            if(g.request.master==Master::CpuFetch)
                out.lastCpuFetchEnd=std::max(out.lastCpuFetchEnd,g.end);
            else if(g.request.master==Master::CpuData)
                out.lastCpuDataEnd=std::max(out.lastCpuDataEnd,g.end);
            else if(static_cast<unsigned>(g.request.master)>=2u &&
                    static_cast<unsigned>(g.request.master)<=5u)
                out.lastDmaEnd=std::max(out.lastDmaEnd,g.end);
        }
        return out;
    }
    // A branch request becomes eligible at the observed issue clock. The
    // subsequent fetch slots may have independent readiness timestamps. All
    // timestamps are relative to the containing instruction's start, and the
    // transfer sequence remains ordered on the CPU fetch queue. In particular,
    // this describes earliest bus eligibility, NOT an architectural promise
    // that decode/execute fully overlaps these grants.
    static Result coldBranchAt(std::uint32_t target,bool thumb,std::uint16_t waitcnt,
                               std::uint64_t issueReady,
                               std::uint64_t slot1Ready,
                               std::uint64_t slot2Ready,
                               std::vector<Transfer> competing={}){
        if(!GamePakPrefetch::isRom(target))return {};
        if(slot1Ready<issueReady || slot2Ready<slot1Ready)
            throw std::invalid_argument("Game Pak fetch stage readiness must be ordered");
        const unsigned width=thumb?2u:4u;
        target&=~(width-1u);
        competing.push_back({issueReady,Master::CpuFetch,target,width,false,0});
        competing.push_back({slot1Ready,Master::CpuFetch,target+width,width,true,1});
        competing.push_back({slot2Ready,Master::CpuFetch,target+2u*width,width,true,2});
        return schedule(std::move(competing),waitcnt);
    }
    // an independently testable pipeline-dependent variant. Unlike
    // coldBranchAt, the second instruction cannot request its external bus
    // word until the first entire instruction word is delivered; similarly
    // the third waits for the second. DMA may still interleave at each 16-bit
    // boundary. This is a physical readiness *bound*, not active CPU timing.
    static Result coldBranchDependentAt(std::uint32_t target,bool thumb,
                                        std::uint16_t waitcnt,
                                        std::uint64_t issueReady,
                                        std::uint64_t slot1Ready,
                                        std::uint64_t slot2Ready,
                                        std::vector<Transfer> competing={}){
        if(!GamePakPrefetch::isRom(target))return {};
        if(slot1Ready<issueReady || slot2Ready<slot1Ready)
            throw std::invalid_argument("Dependent refill readiness must be ordered");
        constexpr auto none=std::numeric_limits<std::uint32_t>::max();
        const std::uint32_t width=thumb?2u:4u;
        target&=~(width-1u);
        // Assign disjoint IDs even when a caller used low DMA transfer tags.
        const std::uint32_t t0=0xfffffff0u,t1=0xfffffff1u,t2=0xfffffff2u;
        for(const auto& t:competing)
            if(t.tag==t0 || t.tag==t1 || t.tag==t2 || t.tag==none)
                throw std::invalid_argument("Reserved refill dependency tag");
        competing.push_back({issueReady,Master::CpuFetch,target,width,false,t0,none});
        competing.push_back({slot1Ready,Master::CpuFetch,target+width,width,true,t1,t0});
        competing.push_back({slot2Ready,Master::CpuFetch,target+2u*width,width,true,t2,t1});
        return schedule(std::move(competing),waitcnt);
    }
    // Requests for three new instruction slots. The ready offsets describe
    // pipeline issue phases and default to zero ONLY to calculate the lower
    // bound of unconstrained physical bus demand, not CPU-visible latency.
    static Result coldBranch(std::uint32_t target,bool thumb,std::uint16_t waitcnt,
                             std::uint64_t slot1Ready=0,
                             std::uint64_t slot2Ready=0,
                             std::vector<Transfer> competing={}){
        return coldBranchAt(target,thumb,waitcnt,0,slot1Ready,slot2Ready,
                            std::move(competing));
    }
};
} // namespace pixelgba
