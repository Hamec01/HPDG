#pragma once

#include <random>
#include <vector>

#include "../../Core/GeneratorParams.h"
#include "BoomBapGrooveBlueprint.h"
#include "BoomBapStyleProfile.h"

namespace bbg
{
struct BoomBapLaneActivation
{
    bool useOpenHat = true;
    bool useRide = false;
    bool usePerc = true;
    bool useGhostKick = true;
    bool useClapGhostSnare = true;
    bool useCymbal = false;
};

struct BoomBapLaneActivationPlan
{
    std::vector<BoomBapLaneActivation> bars;
    bool anyRide = false;
    bool anyCymbal = false;
};

BoomBapLaneActivation decideLaneActivation(const GeneratorParams& params,
                                           const BoomBapStyleProfile& style,
                                           const BoomBapBarBlueprint& bar,
                                           std::mt19937& rng);

BoomBapLaneActivationPlan buildBoomBapLaneActivation(const GeneratorParams& params,
                                                     const BoomBapStyleProfile& style,
                                                     const BoomBapGrooveBlueprint& blueprint,
                                                     std::mt19937& rng);
} // namespace bbg
