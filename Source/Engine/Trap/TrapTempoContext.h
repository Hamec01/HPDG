#pragma once

namespace bbg
{
struct TrapTempoContext
{
    double hostBpm = 120.0;
    double styleBpm = 120.0;
    double clockMultiplier = 1.0;
    double referenceBpm = 155.0;
    bool doubleTime = false;

    int hostBars = 1;
    int styleBars = 1;

    int styleTick64ToHostPpq(int styleTick64, int ppq = 960) const;
    int straightSubdivisionPpq(int denominator, int ppq = 960) const;
    int tripletSubdivisionPpq(int denominator, int ppq = 960) const;
};

TrapTempoContext resolveTrapTempo(double hostBpm,
                                  int hostBars,
                                  double referenceBpm = 155.0);
} // namespace bbg
