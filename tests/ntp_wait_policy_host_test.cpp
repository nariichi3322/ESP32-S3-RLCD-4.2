// 验证 NTP 总超时计算在普通值和乘法溢出边界下保持有限等待。
#include "ntp_wait_policy.h"

#include <cassert>
#include <cstdint>

int main()
{
    using Tick = uint32_t;
    constexpr Tick kPollTicks = 1000;
    constexpr Tick kMaxFiniteWait = UINT32_MAX - 1;

    assert(ntp_total_wait_ticks<Tick>(0, kPollTicks, kMaxFiniteWait) == 0);
    assert(ntp_total_wait_ticks<Tick>(1, kPollTicks, kMaxFiniteWait) == 1000);
    assert(ntp_total_wait_ticks<Tick>(2, kPollTicks, kMaxFiniteWait) == 2000);
    assert(ntp_total_wait_ticks<Tick>(30, kPollTicks, kMaxFiniteWait) == 30000);
    assert(ntp_total_wait_ticks<Tick>(
               UINT32_MAX, kPollTicks, kMaxFiniteWait) == kMaxFiniteWait);
    assert(ntp_total_wait_ticks<Tick>(30, 0, kMaxFiniteWait) == 0);
    assert(ntp_total_wait_ticks<Tick>(30, kPollTicks, 0) == 0);

    using ShortTick = uint16_t;
    assert(ntp_total_wait_ticks<ShortTick>(
               65536, 1, UINT16_MAX - 1) == UINT16_MAX - 1);

    return 0;
}
