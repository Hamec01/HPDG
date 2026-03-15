#pragma once

#include <array>
#include <random>
#include <vector>

#include "BoomBapPhrasePlanner.h"
#include "BoomBapStyleProfile.h"

namespace bbg
{
enum class KickHitRole
{
    Anchor = 0,
    Support,
    Pickup
};

struct KickTemplateDefinition
{
    std::array<int, 8> steps {};
    std::array<KickHitRole, 8> roles {};
    int substyleMask = 0;
    float minDensity = 0.0f;
    float maxDensity = 1.0f;
    int phraseMask = 0xF; // Base/Variation/Contrast/Ending bits.
};

struct SnareFeelProfile
{
    const char* name = "Straight";
    int beat2LateTicks = 10;
    int beat4LateTicks = 8;
    int beat2LateSpread = 6;
    int beat4LateSpread = 6;
    float clapLayerProbability = 1.0f;
    float ghostSnareProbability = 0.0f;
    float ghostBefore2Probability = 0.0f;
    float ghostBefore4Probability = 0.0f;
    float dragProbability = 0.0f;
    float fillHitProbability = 0.0f;
    bool avoidStraightBackbeat = false;
    bool dryBackbeat = false;
};

struct HatPatternProfile
{
    const char* name = "Eighth Straight";
    std::array<int, 16> activeSteps {};
    std::array<int, 16> accentMap {};
    std::array<int, 16> substitutionSteps {};
    int substyleMask = 0;
    float minDensity = 0.0f;
    float maxDensity = 1.0f;
};

struct GhostBehaviorPreset
{
    const char* name = "Ghost Kick Pickup";
    float pickupProbability = 0.12f;
    int maxEventsPerBar = 2;
    bool barEndBias = false;
    int substyleMask = 0;
};

struct OpenHatPreset
{
    const char* name = "End of Bar Lift";
    float eventProbability = 0.14f;
    bool barEndBias = true;
    bool afterKickBias = false;
    int substyleMask = 0;
};

struct PercDecorationPreset
{
    const char* name = "Sparse Dusty";
    float eventProbability = 0.10f;
    int maxEventsPerBar = 2;
    bool bar4Bias = false;
    int substyleMask = 0;
};

const std::vector<KickTemplateDefinition>& getBoomBapKickTemplates();
const std::vector<SnareFeelProfile>& getBoomBapSnareFeelProfiles();
const std::vector<HatPatternProfile>& getBoomBapHatPatternProfiles();
const std::vector<GhostBehaviorPreset>& getBoomBapGhostPresets();
const std::vector<OpenHatPreset>& getBoomBapOpenHatPresets();
const std::vector<PercDecorationPreset>& getBoomBapPercPresets();

const SnareFeelProfile& chooseSnareFeelProfile(BoomBapSubstyle substyle, float density, std::mt19937& rng);
const HatPatternProfile& chooseHatPatternProfile(BoomBapSubstyle substyle, float density, std::mt19937& rng);
const GhostBehaviorPreset& chooseGhostPreset(BoomBapSubstyle substyle, std::mt19937& rng);
const OpenHatPreset& chooseOpenHatPreset(BoomBapSubstyle substyle, std::mt19937& rng);
const PercDecorationPreset& choosePercPreset(BoomBapSubstyle substyle, std::mt19937& rng);

std::vector<const KickTemplateDefinition*> findMatchingKickTemplates(BoomBapSubstyle substyle,
                                                                     float density,
                                                                     PhraseRole role);

int phraseRoleMask(PhraseRole role);
} // namespace bbg
