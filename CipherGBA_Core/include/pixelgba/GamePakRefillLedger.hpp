// Copyright (c) HodgkinsStudios. Original CipherGBA CPU bus research.
#pragma once
#include "pixelgba/GamePakBusArbiter.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace pixelgba {
// diagnostic: physical external-bus demand of a cold ARM7TDMI branch.
// This is NOT the CPU-visible branch penalty: execution/internal cycles and
// instruction-pipeline overlap are deliberately outside this model. Importantly
// the existing CPU timing path must NEVER consume these predictions directly.
class GamePakRefillLedger {
public:
    struct Result {
        unsigned cycles=0;
        unsigned beats=0;
        std::vector<GamePakBusArbiter::Grant> grants;
    };
    static Result coldBranch(std::uint32_t target, bool thumb,
                             std::uint16_t waitcnt) {
        Result out;
        if (!GamePakPrefetch::isRom(target))return out;
        const unsigned width=thumb?2u:4u;
        target &= ~(width-1u);
        GamePakBusArbiter arbiter;
        // The ARM7TDMI pipeline needs three new instruction slots after a
        // redirect. Bus beats are grouped by instruction, NOT charged as a
        // single access with one initial S or N wait state.
        for(unsigned slot=0;slot<3u;++slot){
            arbiter.request(GamePakBusArbiter::Master::CpuFetch,
                            target+slot*width,width,slot!=0u,slot);
        }
        arbiter.drain(waitcnt);
        out.cycles=static_cast<unsigned>(arbiter.cycle());
        out.grants=arbiter.completed();
        out.beats=static_cast<unsigned>(out.grants.size());
        return out;
    }
};
} // namespace pixelgba
