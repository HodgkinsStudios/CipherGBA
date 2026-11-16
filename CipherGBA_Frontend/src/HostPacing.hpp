// Host-only frame deadline scheduling; intentionally independent of GBA timing.
#pragma once
#include <algorithm>
#include <chrono>
namespace pixelgba::host {
template<class Clock>
inline typename Clock::time_point pacingWakeDeadline(
    typename Clock::time_point now, typename Clock::time_point frameDeadline){
    // Wake in no more than 2ms to poll input and presentation events.
    // Unlike sleep_for(1ms), a sub-millisecond remaining interval is not
    // rounded up to a full millisecond. The next guest frame is still due
    // at the same original deadline.
    return std::min(frameDeadline,now+std::chrono::milliseconds(2));
}
} // namespace pixelgba::host
