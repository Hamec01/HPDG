#include "TrapTempoContext.h"

#include <algorithm>
#include <cmath>

namespace bbg
{
namespace
{
double octaveDistance(double bpm, double referenceBpm)
{
    return std::abs(std::log2(bpm / referenceBpm));
}
}

TrapTempoContext resolveTrapTempo(double hostBpm, int hostBars, double referenceBpm)
{
    TrapTempoContext context;
    context.hostBpm = std::clamp(hostBpm, 20.0, 400.0);
    context.referenceBpm = std::clamp(referenceBpm, 80.0, 240.0);
    context.hostBars = std::clamp(hostBars, 1, 16);

    const double normalDistance = octaveDistance(context.hostBpm, context.referenceBpm);
    const double doubleDistance = octaveDistance(context.hostBpm * 2.0, context.referenceBpm);
    context.doubleTime = doubleDistance < normalDistance;
    context.clockMultiplier = context.doubleTime ? 2.0 : 1.0;
    context.styleBpm = context.hostBpm * context.clockMultiplier;
    context.styleBars = context.hostBars * static_cast<int>(context.clockMultiplier);
    return context;
}

int TrapTempoContext::styleTick64ToHostPpq(int styleTick64, int ppq) const
{
    const double normalTickPpq = static_cast<double>(std::max(1, ppq)) / 16.0;
    return static_cast<int>(std::lround(static_cast<double>(styleTick64) * normalTickPpq / clockMultiplier));
}

int TrapTempoContext::straightSubdivisionPpq(int denominator, int ppq) const
{
    const int safeDenominator = std::max(1, denominator);
    return static_cast<int>(std::lround((4.0 * static_cast<double>(std::max(1, ppq)))
                                        / static_cast<double>(safeDenominator)
                                        / clockMultiplier));
}

int TrapTempoContext::tripletSubdivisionPpq(int denominator, int ppq) const
{
    const int safeDenominator = std::max(1, denominator);
    return static_cast<int>(std::lround((8.0 * static_cast<double>(std::max(1, ppq)))
                                        / (3.0 * static_cast<double>(safeDenominator))
                                        / clockMultiplier));
}
} // namespace bbg
