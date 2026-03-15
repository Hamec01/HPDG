#pragma once

#include <random>
#include <vector>

#include "../../Core/GeneratorParams.h"
#include "BoomBapPhrasePlanner.h"
#include "BoomBapStyleProfile.h"

namespace bbg
{
struct BoomBapBarBlueprint
{
    int barIndex = 0;
    PhraseRole role = PhraseRole::Base;
    bool strongBackbeat = true;
    bool allowOpenHat = true;
    bool allowRide = false;
    bool allowPerc = true;
    bool allowGhostKick = true;
    bool allowClapLayer = true;

    float kickSupportAmount = 0.5f;
    float hatActivity = 0.5f;
    float hatSyncopation = 0.5f;
    float swingStrength = 0.5f;
    float lateBackbeatAmount = 0.5f;
    float endLiftAmount = 0.4f;
    float looseness = 0.35f;
    float grit = 0.3f;

    bool stripToCore = false;
};

struct BoomBapGrooveBlueprint
{
    std::vector<BoomBapBarBlueprint> bars;
    int phraseLengthBars = 1;
    float averageEnergy = 0.5f;
    bool halfTimeReference = false;
};

BoomBapGrooveBlueprint buildBoomBapGrooveBlueprint(const GeneratorParams& params,
                                                   const BoomBapStyleProfile& style,
                                                   const std::vector<PhraseRole>& phrasePlan,
                                                   bool halfTimeReference,
                                                   std::mt19937& rng);
} // namespace bbg
