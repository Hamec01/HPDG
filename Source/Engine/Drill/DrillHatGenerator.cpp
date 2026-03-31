#include "DrillHatGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>


#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"

namespace bbg
{
namespace
{
constexpr double kWeakSubdivision = 0.34;

DrillHatGenerator::MotionMode motionModeFromWeight(double hatMotionWeightValue)
{
    if (hatMotionWeightValue < 0.92)
        return DrillHatGenerator::MotionMode::Low;
    if (hatMotionWeightValue < 1.20)
        return DrillHatGenerator::MotionMode::Mid;
    return DrillHatGenerator::MotionMode::High;
}

int barFromTick(int tick)
{
    return std::max(0, tick / HiResTiming::kTicksPerBar4_4);
}

int stepInBar32FromTick(int tick)
{
    const int local = std::max(0, tick % HiResTiming::kTicksPerBar4_4);
    return local / HiResTiming::kTicks1_32;
}

double metricalWeight32(int stepInBar32)
{
    if (stepInBar32 % 8 == 0)
        return 1.00;
    if (stepInBar32 % 4 == 0)
        return 0.82;
    if (stepInBar32 % 2 == 0)
        return 0.58;
    return kWeakSubdivision;
}

double localHatDensityWeighted(int tick, const std::vector<DrillHatGenerator::HatEvent>& hats)
{
    double score = 0.0;
    for (const auto& h : hats)
    {
        const int d = std::abs(h.tick - tick);
        if (d <= 120)
            score += 1.0;
        else if (d <= 240)
            score += 0.6;
        else if (d <= 360)
            score += 0.3;
        else if (d <= 480)
            score += 0.12;
    }
    return score;
}

double densityControlWeighted(int tick,
                             const std::vector<DrillHatGenerator::HatEvent>& hats,
                             double lambda)
{
    return 1.0 / (1.0 + std::max(0.01, lambda) * localHatDensityWeighted(tick, hats));
}

bool tickOccupied(const std::vector<DrillHatGenerator::HatEvent>& hats, int tick, int tolerance)
{
    for (const auto& h : hats)
    {
        if (std::abs(h.tick - tick) <= tolerance)
            return true;
    }
    return false;
}

int nearestForward(int tick, const std::vector<int>& targets)
{
    for (const int t : targets)
        if (t >= tick)
            return t;
    return -1;
}

double cooldownFactor(int tick, int lastTick, int cooldownTicks)
{
    if (lastTick < 0)
        return 1.0;
    return std::clamp(static_cast<double>(std::abs(tick - lastTick)) / static_cast<double>(std::max(1, cooldownTicks)),
                      0.0,
                      1.0);
}

DrillHatGenerator::BarRole barRoleFromPhrase(DrillPhraseRole role, int barIndex, int bars)
{
    if (role == DrillPhraseRole::Ending || barIndex == std::max(0, bars - 1))
        return DrillHatGenerator::BarRole::Ending;
    if (role == DrillPhraseRole::Tension)
        return DrillHatGenerator::BarRole::Transition;
    if (role == DrillPhraseRole::Response)
        return DrillHatGenerator::BarRole::Response;
    return DrillHatGenerator::BarRole::Statement;
}

double roleEnergy(DrillHatGenerator::BarRole role)
{
    switch (role)
    {
        case DrillHatGenerator::BarRole::Statement: return 1.00;
        case DrillHatGenerator::BarRole::Response: return 0.94;
        case DrillHatGenerator::BarRole::Transition: return 1.12;
        case DrillHatGenerator::BarRole::Ending: return 1.28;
    }

    return 1.0;
}

double offbeatValue(int stepInBar32)
{
    if ((stepInBar32 % 4) == 2)
        return 1.08;
    if ((stepInBar32 % 2) == 1)
        return 1.00;
    return 0.72;
}

double preSnareValue(int tick, const std::vector<int>& snareTargets)
{
    double best = 0.0;
    for (const int snare : snareTargets)
    {
        const int delta = snare - tick;
        if (delta < 0)
            continue;

        if (delta <= HiResTiming::kTicks1_24)
            best = std::max(best, 1.00);
        else if (delta <= HiResTiming::kTicks1_16)
            best = std::max(best, 0.92);
        else if (delta <= HiResTiming::kTicks1_8)
            best = std::max(best, 0.76);
    }

    return best;
}

double preKickValue(int tick, const std::vector<int>& kickTicks)
{
    double best = 0.0;
    for (const int k : kickTicks)
    {
        const int d = std::abs(k - tick);
        if (d <= HiResTiming::kTicks1_32)
            best = std::max(best, 1.0);
        else if (d <= HiResTiming::kTicks1_16)
            best = std::max(best, 0.65);
    }

    return best;
}

const DrillHatGenerator::HatGridFeature* findFeature(const std::vector<DrillHatGenerator::HatGridFeature>& features,
                                                     int tick)
{
    for (const auto& f : features)
        if (f.tick == tick)
            return &f;
    return nullptr;
}

DrillHatGenerator::HatGridFeature* findFeatureMutable(std::vector<DrillHatGenerator::HatGridFeature>& features,
                                                      int tick)
{
    for (auto& f : features)
        if (f.tick == tick)
            return &f;
    return nullptr;
}

int countByBar(const std::vector<DrillHatGenerator::HatEvent>& hats, int bar)
{
    int count = 0;
    for (const auto& h : hats)
        if (barFromTick(h.tick) == bar)
            ++count;
    return count;
}

int countRollsByBar(const std::vector<DrillHatGenerator::RollSegmentState>& segments, int bar)
{
    int count = 0;
    for (const auto& s : segments)
        if (barFromTick(s.startTick) == bar)
            ++count;
    return count;
}

int countRollsByTwoBars(const std::vector<DrillHatGenerator::RollSegmentState>& segments, int bar)
{
    const int start = (bar / 2) * 2;
    int count = 0;
    for (const auto& s : segments)
    {
        const int b = barFromTick(s.startTick);
        if (b == start || b == (start + 1))
            ++count;
    }
    return count;
}

int countMotionByBar(const std::vector<DrillHatGenerator::HatEvent>& hats, int bar)
{
    int count = 0;
    for (const auto& h : hats)
    {
        if (barFromTick(h.tick) != bar)
            continue;
        if (h.entity != DrillHatGenerator::HatEntity::Backbone)
            ++count;
    }
    return count;
}

bool isMotionEntity(DrillHatGenerator::HatEntity entity)
{
    return entity != DrillHatGenerator::HatEntity::Backbone;
}

int quantizeRelativeToBar(int tick, int gridTicks)
{
    const int bar = barFromTick(tick);
    const int barStart = bar * HiResTiming::kTicksPerBar4_4;
    const int localTick = tick - barStart;
    return barStart + HiResTiming::quantizeTicks(localTick, std::max(1, gridTicks));
}

int quantizeToSegmentGrid(int tick, const DrillHatGenerator::RollSegmentState& segment)
{
    int bestTick = segment.startTick;
    int bestDistance = std::abs(tick - bestTick);

    for (int index = 0; index < std::max(1, segment.noteCount); ++index)
    {
        const int candidateTick = segment.startTick + static_cast<int>(std::lround(segment.subdivisionTicks * static_cast<double>(index)));
        const int distance = std::abs(tick - candidateTick);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestTick = candidateTick;
        }
    }

    const int tailBase = segment.startTick + segment.lenTicks;
    if (segment.exitType == DrillHatGenerator::RollExitType::SingleTail
        || segment.exitType == DrillHatGenerator::RollExitType::DoubleTail)
    {
        const int tailDistance = std::abs(tick - tailBase);
        if (tailDistance < bestDistance)
        {
            bestDistance = tailDistance;
            bestTick = tailBase;
        }
    }

    if (segment.exitType == DrillHatGenerator::RollExitType::DoubleTail)
    {
        const int secondTail = tailBase + HiResTiming::kTicks1_32;
        const int tailDistance = std::abs(tick - secondTail);
        if (tailDistance < bestDistance)
            bestTick = secondTail;
    }

    return bestTick;
}

void quantizeGeneratedHatGrid(std::vector<DrillHatGenerator::HatEvent>& hats,
                              const std::vector<DrillHatGenerator::RollSegmentState>& segments)
{
    for (auto& hat : hats)
    {
        if (hat.entity == DrillHatGenerator::HatEntity::Backbone)
        {
            hat.tick = quantizeRelativeToBar(hat.tick, HiResTiming::kTicks1_16);
            continue;
        }

        bool snappedToRollGrid = false;
        for (const auto& segment : segments)
        {
            const int windowStart = segment.startTick - HiResTiming::kTicks1_64;
            const int windowEnd = segment.startTick + segment.lenTicks + HiResTiming::kTicks1_16;
            if (hat.tick < windowStart || hat.tick > windowEnd)
                continue;

            hat.tick = quantizeToSegmentGrid(hat.tick, segment);
            snappedToRollGrid = true;
            break;
        }

        if (!snappedToRollGrid)
            hat.tick = quantizeRelativeToBar(hat.tick, HiResTiming::kTicks1_32);
    }
}

void ensureBarStartAnchors(std::vector<DrillHatGenerator::HatEvent>& hats, int bars)
{
    for (int bar = 0; bar < bars; ++bar)
    {
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;
        bool hasStartAnchor = false;
        for (const auto& hat : hats)
        {
            if (barFromTick(hat.tick) != bar)
                continue;
            if (hat.tick == barStart)
            {
                hasStartAnchor = true;
                break;
            }
        }

        if (!hasStartAnchor)
            hats.push_back({ barStart, 0, 0, DrillHatGenerator::HatEntity::Backbone, false, false, false });
    }
}

double clampProb(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double hiHatActivityWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(static_cast<double>(laneBiasFor(styleInfluence, TrackType::HiHat).activityWeight), 0.55, 1.6);
}

double hatMotionWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(static_cast<double>(styleInfluence.hatMotionWeight), 0.65, 1.6);
}

double drillHatRollLengthWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(static_cast<double>(styleInfluence.drillHatRollLengthWeight), 0.65, 1.6);
}

double drillHatDensityVariationWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(static_cast<double>(styleInfluence.drillHatDensityVariationWeight), 0.65, 1.6);
}

double drillHatAccentPatternWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(static_cast<double>(styleInfluence.drillHatAccentPatternWeight), 0.65, 1.6);
}

double drillHatGapIntentWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(static_cast<double>(styleInfluence.drillHatGapIntentWeight), 0.65, 1.6);
}

double drillHatBurstWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(static_cast<double>(styleInfluence.drillHatBurstWeight), 0.65, 1.6);
}

double drillHatTripletWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(static_cast<double>(styleInfluence.drillHatTripletWeight), 0.65, 1.6);
}

DrillHatGenerator::MotionProfile buildMotionProfile(const StyleInfluenceState& styleInfluence)
{
    DrillHatGenerator::MotionProfile profile;
    profile.mode = motionModeFromWeight(hatMotionWeight(styleInfluence));
    profile.motionWeight = hatMotionWeight(styleInfluence);
    profile.rollLengthWeight = drillHatRollLengthWeight(styleInfluence);
    profile.densityVariationWeight = drillHatDensityVariationWeight(styleInfluence);
    profile.accentPatternWeight = drillHatAccentPatternWeight(styleInfluence);
    profile.gapIntentWeight = drillHatGapIntentWeight(styleInfluence);
    profile.burstWeight = drillHatBurstWeight(styleInfluence);
    profile.tripletWeight = drillHatTripletWeight(styleInfluence);
    profile.sparseMode = profile.mode == DrillHatGenerator::MotionMode::Low || profile.gapIntentWeight > 1.18;
    profile.expressiveMode = profile.mode == DrillHatGenerator::MotionMode::High || profile.burstWeight > 1.14 || profile.tripletWeight > 1.14;
    profile.averageRollNotes = std::clamp(2.4 + (profile.rollLengthWeight - 1.0) * 3.0 + (profile.mode == DrillHatGenerator::MotionMode::High ? 0.8 : 0.0),
                                          2.0,
                                          6.0);
    profile.rollsPerBarTarget = std::clamp(0.18
                                           + (profile.motionWeight - 0.8) * 0.42
                                           + (profile.burstWeight - 1.0) * 0.36,
                                           0.05,
                                           1.35);
    profile.densitySwing = std::clamp(std::abs(profile.densityVariationWeight - 1.0) * 0.75
                                      + (profile.mode == DrillHatGenerator::MotionMode::High ? 0.16 : (profile.mode == DrillHatGenerator::MotionMode::Mid ? 0.08 : 0.0)),
                                      0.0,
                                      0.65);
    profile.maxMotionEventsPerBar = profile.mode == DrillHatGenerator::MotionMode::Low ? 1
        : (profile.mode == DrillHatGenerator::MotionMode::Mid ? 3 : 5);
    profile.preferBurstClusters = profile.mode == DrillHatGenerator::MotionMode::High || profile.burstWeight > 1.08;
    profile.preferAccentGroups = profile.mode != DrillHatGenerator::MotionMode::Low || profile.accentPatternWeight > 1.03;
    return profile;
}

int hatEntityPriority(DrillHatGenerator::HatEntity entity)
{
    switch (entity)
    {
        case DrillHatGenerator::HatEntity::Backbone: return 3;
        case DrillHatGenerator::HatEntity::Accent: return 2;
        case DrillHatGenerator::HatEntity::Roll: return 1;
        case DrillHatGenerator::HatEntity::Fill: return 0;
    }

    return 0;
}

using RefPattern = std::vector<int>; // 1/16 steps over 1 bar, range [0..15]

using SubstyleTemplateSet = std::array<RefPattern, 3>;

const SubstyleTemplateSet& fixedHatBackboneTemplates(DrillSubstyle substyle)
{
    static const SubstyleTemplateSet uk = {{
        { 0, 2, 4, 6, 8, 10, 12, 14 },
        { 0, 2, 4, 8, 10, 12, 14 },
        { 0, 3, 4, 6, 8, 10, 14 }
    }};
    static const SubstyleTemplateSet brooklyn = {{
        { 0, 2, 4, 6, 8, 9, 11, 13 },
        { 0, 2, 3, 5, 7, 9, 11, 13 },
        { 0, 2, 4, 6, 7, 9, 11, 13 }
    }};
    static const SubstyleTemplateSet ny = {{
        { 0, 2, 4, 8, 10, 12, 14 },
        { 0, 2, 4, 6, 8, 10, 14 },
        { 0, 3, 5, 7, 9, 11, 13 }
    }};
    static const SubstyleTemplateSet dark = {{
        { 0, 4, 6, 10, 14 },
        { 0, 2, 6, 10, 12 },
        { 0, 3, 6, 8, 12 }
    }};

    switch (substyle)
    {
        case DrillSubstyle::UKDrill: return uk;
        case DrillSubstyle::BrooklynDrill: return brooklyn;
        case DrillSubstyle::NYDrill: return ny;
        case DrillSubstyle::DarkDrill: return dark;
    }

    return uk;
}

bool hasReferenceAnchorAtStep16(int absoluteStep16, const RefPattern& pattern)
{
    const int cycleStep = ((absoluteStep16 % 16) + 16) % 16;
    return std::find(pattern.begin(), pattern.end(), cycleStep) != pattern.end();
}

int backboneMinPerBar(DrillSubstyle substyle)
{
    (void)substyle;
    return 6;
}

int backboneMaxPerBar(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::UKDrill: return 8;
        case DrillSubstyle::BrooklynDrill: return 10;
        case DrillSubstyle::NYDrill: return 9;
        case DrillSubstyle::DarkDrill: return 7;
    }

    return 8;
}

int maxRollsPer2BarsHard(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::UKDrill: return 1;
        case DrillSubstyle::BrooklynDrill: return 2;
        case DrillSubstyle::NYDrill: return 2;
        case DrillSubstyle::DarkDrill: return 1;
    }

    return 1;
}

int eventWindowIndexFromTick(int tick)
{
    const int step16 = std::max(0, tick / HiResTiming::kTicks1_16);
    const int stepInBar = step16 % 16;
    return std::clamp(stepInBar / 4, 0, 3);
}

bool phraseEdgeOwnedByKick(const DrillGrooveBlueprint* blueprint, int step16)
{
    if (blueprint == nullptr)
        return false;

    const auto* slot = blueprint->slotAt(step16);
    if (slot == nullptr)
        return false;

    return slot->kickPlaced && slot->kickEventType == DrillKickEventType::PhraseEdgeKick;
}

bool hasReferenceHatSkeleton(const StyleInfluenceState& styleInfluence)
{
    return styleInfluence.referenceHatSkeleton.available
        && !styleInfluence.referenceHatSkeleton.barMaps.empty();
}

bool hasReferenceHatCorpus(const StyleInfluenceState& styleInfluence)
{
    return styleInfluence.referenceHatCorpus.available
        && !styleInfluence.referenceHatCorpus.variants.empty();
}

struct ReferenceHatFeel
{
    bool available = false;
    double averageNotesPerBar = 0.0;
    double motionRatio = 0.0;
    double tripletRatio = 0.0;
    double burstRatio = 0.0;
    double gapRatio = 0.0;
    double preSnareRatio = 0.0;
    std::array<double, 32> stepWeight {};
    std::array<double, 16> backboneWeight {};
};

ReferenceHatFeel buildReferenceHatFeel(const ReferenceHatCorpus* corpus, int bar)
{
    ReferenceHatFeel feel;
    if (corpus == nullptr || !corpus->available || corpus->variants.empty())
        return feel;

    int contributingBars = 0;
    double totalNotes = 0.0;
    double totalMotion = 0.0;
    double totalTripletClusters = 0.0;
    double totalBurstClusters = 0.0;
    double totalGapSlots = 0.0;
    double totalPreSnareSlots = 0.0;

    for (const auto& variant : corpus->variants)
    {
        if (!variant.available || variant.barMaps.empty())
            continue;

        const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barMaps.size()));
        const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
        if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barMaps.size()))
            continue;

        const auto& referenceBar = variant.barMaps[static_cast<size_t>(normalizedBar)];

        ++contributingBars;
        std::array<bool, 32> occupied32 {};
        for (const auto& note : referenceBar.notes)
        {
            const int step32 = std::clamp(HiResTiming::quantizeTicks(note.tickInBar, HiResTiming::kTicks1_32) / HiResTiming::kTicks1_32,
                                          0,
                                          31);
            occupied32[static_cast<size_t>(step32)] = true;
            feel.stepWeight[static_cast<size_t>(step32)] += 1.0;
            totalNotes += 1.0;
            if ((step32 % 2) == 1)
                totalMotion += 1.0;
        }

        for (const int step16 : referenceBar.backboneSteps16)
        {
            if (step16 >= 0 && step16 < 16)
                feel.backboneWeight[static_cast<size_t>(step16)] += 1.0;
        }

        for (int step32 = 0; step32 < 32; ++step32)
        {
            if (!occupied32[static_cast<size_t>(step32)])
                totalGapSlots += 1.0;
        }

        totalPreSnareSlots += static_cast<double>(referenceBar.preSnareZoneSteps32.size());

        for (const auto& cluster : variant.rollClusters)
        {
            if (cluster.barIndex != normalizedBar)
                continue;
            totalBurstClusters += 1.0;
        }
        for (const auto& cluster : variant.tripletClusters)
        {
            if (cluster.barIndex != normalizedBar)
                continue;
            totalTripletClusters += 1.0;
        }
    }

    if (contributingBars <= 0)
        return feel;

    feel.available = true;
    const double invBars = 1.0 / static_cast<double>(contributingBars);
    for (auto& value : feel.stepWeight)
        value *= invBars;
    for (auto& value : feel.backboneWeight)
        value *= invBars;

    feel.averageNotesPerBar = totalNotes * invBars;
    feel.motionRatio = totalNotes > 0.0 ? totalMotion / totalNotes : 0.0;
    feel.tripletRatio = totalTripletClusters * invBars;
    feel.burstRatio = totalBurstClusters * invBars;
    feel.gapRatio = totalGapSlots / (static_cast<double>(contributingBars) * 32.0);
    feel.preSnareRatio = totalPreSnareSlots / (static_cast<double>(contributingBars) * 32.0);
    return feel;
}

DrillHatGenerator::MotionProfile blendMotionProfileFromReference(DrillHatGenerator::MotionProfile profile,
                                                                 const ReferenceHatFeel& feel,
                                                                 DrillSubstyle substyle)
{
    if (!feel.available)
        return profile;

    const double substyleBlend = substyle == DrillSubstyle::UKDrill ? 0.34
        : (substyle == DrillSubstyle::BrooklynDrill ? 0.26 : 0.22);
    profile.motionWeight = std::clamp(profile.motionWeight + (feel.motionRatio - 0.36) * 0.28 * substyleBlend, 0.65, 1.75);
    profile.rollLengthWeight = std::clamp(profile.rollLengthWeight + feel.burstRatio * 0.18 * substyleBlend, 0.65, 1.75);
    profile.burstWeight = std::clamp(profile.burstWeight + feel.burstRatio * 0.32 * substyleBlend, 0.65, 1.85);
    profile.tripletWeight = std::clamp(profile.tripletWeight + feel.tripletRatio * 0.34 * substyleBlend, 0.65, 1.85);
    profile.gapIntentWeight = std::clamp(profile.gapIntentWeight + (feel.gapRatio - 0.52) * 0.24 * substyleBlend, 0.65, 1.75);
    profile.averageRollNotes = std::clamp(profile.averageRollNotes
                                          + feel.burstRatio * 0.85 * substyleBlend
                                          + feel.tripletRatio * 0.55 * substyleBlend
                                          - std::max(0.0, feel.gapRatio - 0.58) * 0.6 * substyleBlend,
                                          2.0,
                                          6.0);
    profile.rollsPerBarTarget = std::clamp(profile.rollsPerBarTarget
                                           + feel.motionRatio * 0.18 * substyleBlend
                                           + feel.burstRatio * 0.14 * substyleBlend
                                           + feel.tripletRatio * 0.12 * substyleBlend,
                                           0.05,
                                           1.35);
    if (feel.averageNotesPerBar >= 10.5)
        profile.maxMotionEventsPerBar = std::min(profile.maxMotionEventsPerBar + 1, 5);
    if (feel.burstRatio > 0.22)
        profile.preferBurstClusters = true;
    if (feel.motionRatio > 0.42 || feel.preSnareRatio > 0.08)
        profile.preferAccentGroups = true;
    if (feel.tripletRatio > 0.24 || feel.burstRatio > 0.28)
        profile.expressiveMode = true;
    if (feel.gapRatio > 0.62 && feel.averageNotesPerBar < 9.0)
        profile.sparseMode = true;
    return profile;
}

void applyReferenceHatFeel(std::vector<DrillHatGenerator::HatGridFeature>& features,
                           int bar,
                           const ReferenceHatFeel& feel)
{
    if (!feel.available)
        return;

    for (auto& feature : features)
    {
        if ((feature.step / 32) != bar)
            continue;

        const int step32 = feature.step % 32;
        const int step16 = step32 / 2;
        const double motionBias = feel.stepWeight[static_cast<size_t>(step32)];
        const double backboneBias = feel.backboneWeight[static_cast<size_t>(step16)];
        feature.referenceMotion = std::max(feature.referenceMotion, motionBias);
        feature.referenceBackbone = std::max(feature.referenceBackbone, backboneBias);
        feature.phaseAnchor = std::max(feature.phaseAnchor,
                                       std::clamp(backboneBias * 0.36 + motionBias * 0.22, 0.0, 1.0));

        if (feel.preSnareRatio > 0.06 && step32 >= 24 && motionBias >= 0.24)
            feature.referencePreSnareZone = true;
        if (feel.burstRatio > 0.16 && motionBias >= 0.34 && (step32 % 2) == 1)
            feature.referenceRollZone = true;
        if (feel.tripletRatio > 0.12 && motionBias >= 0.28 && (step32 % 3) != 1)
            feature.referenceTripletZone = true;
        if (feel.gapRatio > 0.60 && motionBias < 0.18 && backboneBias < 0.12 && feature.offbeat < 0.9)
            feature.reservedSilence = feature.reservedSilence || feature.majorEventCompetition > 0.6;
    }
}

const ReferenceHatBarSkeleton* referenceBarFor(const ReferenceHatSkeleton* skeleton, int bar)
{
    if (skeleton == nullptr || !skeleton->available || skeleton->barMaps.empty())
        return nullptr;

    const int sourceBars = std::max(1, skeleton->sourceBars > 0 ? skeleton->sourceBars : static_cast<int>(skeleton->barMaps.size()));
    const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
    if (normalizedBar < 0 || normalizedBar >= static_cast<int>(skeleton->barMaps.size()))
        return nullptr;
    return &skeleton->barMaps[static_cast<size_t>(normalizedBar)];
}

bool noteTickExists(const std::vector<ReferenceHatNote>& notes, int tickInBar, int tolerance)
{
    for (const auto& note : notes)
    {
        if (std::abs(note.tickInBar - tickInBar) <= tolerance)
            return true;
    }

    return false;
}

int noteWeaknessScore(const ReferenceHatNote& note)
{
    const int step32 = std::clamp(HiResTiming::quantizeTicks(note.tickInBar, HiResTiming::kTicks1_32) / HiResTiming::kTicks1_32, 0, 31);
    int score = 0;
    if (step32 == 0)
        score += 100;
    if ((step32 % 8) == 0)
        score += 40;
    else if ((step32 % 4) == 0)
        score += 20;
    if (note.velocity >= 100)
        score += 10;
    return score;
}

bool sameNoteLayout(const std::vector<ReferenceHatNote>& left, const std::vector<ReferenceHatNote>& right)
{
    if (left.size() != right.size())
        return false;

    for (size_t index = 0; index < left.size(); ++index)
    {
        if (left[index].tickInBar != right[index].tickInBar)
            return false;
    }

    return true;
}

void normalizeReferenceNotes(std::vector<ReferenceHatNote>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const ReferenceHatNote& left, const ReferenceHatNote& right)
    {
        if (left.tickInBar != right.tickInBar)
            return left.tickInBar < right.tickInBar;
        return left.velocity > right.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const ReferenceHatNote& left, const ReferenceHatNote& right)
    {
        return left.tickInBar == right.tickInBar;
    }), notes.end());
}

std::vector<ReferenceHatNote> synthesizeUKReferenceBar(const ReferenceHatCorpus* corpus,
                                                       int bar,
                                                       std::mt19937& rng)
{
    std::vector<const ReferenceHatBarSkeleton*> candidates;
    if (corpus == nullptr)
        return {};

    for (const auto& variant : corpus->variants)
    {
        if (const auto* referenceBar = referenceBarFor(&variant, bar); referenceBar != nullptr && !referenceBar->notes.empty())
            candidates.push_back(referenceBar);
    }

    if (candidates.empty())
        return {};

    std::uniform_int_distribution<size_t> primaryPick(0, candidates.size() - 1);
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    auto generated = candidates[primaryPick(rng)]->notes;

    std::vector<ReferenceHatNote> pool;
    for (const auto* candidate : candidates)
        pool.insert(pool.end(), candidate->notes.begin(), candidate->notes.end());
    normalizeReferenceNotes(pool);

    if (candidates.size() > 1)
    {
        std::vector<ReferenceHatNote> missingPool;
        for (const auto& note : pool)
        {
            if (!noteTickExists(generated, note.tickInBar, HiResTiming::kTicks1_64 / 2))
                missingPool.push_back(note);
        }

        if (!missingPool.empty() && chance(rng) < 0.62)
        {
            std::uniform_int_distribution<size_t> addPick(0, missingPool.size() - 1);
            generated.push_back(missingPool[addPick(rng)]);
        }

        if (generated.size() > 5 && chance(rng) < 0.42)
        {
            auto removeIt = std::max_element(generated.begin(), generated.end(), [](const ReferenceHatNote& left, const ReferenceHatNote& right)
            {
                return noteWeaknessScore(left) > noteWeaknessScore(right);
            });
            if (removeIt != generated.end() && noteWeaknessScore(*removeIt) < 100)
                generated.erase(removeIt);
        }

        if (!missingPool.empty() && generated.size() > 4 && chance(rng) < 0.48)
        {
            auto replaceIt = std::max_element(generated.begin(), generated.end(), [](const ReferenceHatNote& left, const ReferenceHatNote& right)
            {
                return noteWeaknessScore(left) > noteWeaknessScore(right);
            });
            if (replaceIt != generated.end() && noteWeaknessScore(*replaceIt) < 100)
            {
                std::uniform_int_distribution<size_t> replacePick(0, missingPool.size() - 1);
                *replaceIt = missingPool[replacePick(rng)];
            }
        }
    }

    normalizeReferenceNotes(generated);
    for (const auto* candidate : candidates)
    {
        if (!sameNoteLayout(generated, candidate->notes))
            continue;

        std::vector<ReferenceHatNote> missingPool;
        for (const auto& note : pool)
        {
            if (!noteTickExists(generated, note.tickInBar, HiResTiming::kTicks1_64 / 2))
                missingPool.push_back(note);
        }

        if (!missingPool.empty())
        {
            std::uniform_int_distribution<size_t> replacePick(0, missingPool.size() - 1);
            auto replaceIt = std::max_element(generated.begin(), generated.end(), [](const ReferenceHatNote& left, const ReferenceHatNote& right)
            {
                return noteWeaknessScore(left) > noteWeaknessScore(right);
            });
            if (replaceIt != generated.end())
                *replaceIt = missingPool[replacePick(rng)];
        }
        else if (generated.size() > 4)
        {
            auto removeIt = std::max_element(generated.begin(), generated.end(), [](const ReferenceHatNote& left, const ReferenceHatNote& right)
            {
                return noteWeaknessScore(left) > noteWeaknessScore(right);
            });
            if (removeIt != generated.end() && noteWeaknessScore(*removeIt) < 100)
                generated.erase(removeIt);
        }

        normalizeReferenceNotes(generated);
        break;
    }

    return generated;
}

std::vector<DrillHatGenerator::HatEvent> buildUKCorpusDrivenHats(const ReferenceHatCorpus* corpus,
                                                                 int bars,
                                                                 std::mt19937& rng)
{
    std::vector<DrillHatGenerator::HatEvent> hats;
    if (corpus == nullptr)
        return hats;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto generatedNotes = synthesizeUKReferenceBar(corpus, bar, rng);
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;
        for (const auto& note : generatedNotes)
        {
            DrillHatGenerator::HatEvent event;
            event.tick = barStart + std::clamp(note.tickInBar, 0, HiResTiming::kTicksPerBar4_4 - 1);
            event.velocity = std::clamp(note.velocity, 1, 127);
            event.entity = (event.tick % HiResTiming::kTicks1_16 == 0)
                ? DrillHatGenerator::HatEntity::Backbone
                : DrillHatGenerator::HatEntity::Fill;
            event.accent = note.velocity >= 100;
            hats.push_back(event);
        }
    }

    std::sort(hats.begin(), hats.end(), [](const DrillHatGenerator::HatEvent& left, const DrillHatGenerator::HatEvent& right)
    {
        if (left.tick != right.tick)
            return left.tick < right.tick;
        return left.velocity > right.velocity;
    });

    hats.erase(std::unique(hats.begin(), hats.end(), [](const DrillHatGenerator::HatEvent& left, const DrillHatGenerator::HatEvent& right)
    {
        return std::abs(left.tick - right.tick) <= (HiResTiming::kTicks1_64 / 2);
    }), hats.end());

    return hats;
}

template <typename T>
bool containsValue(const std::vector<T>& values, T value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T>
void sortAndUnique(std::vector<T>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

double anchorPhaseValue(int tick, const std::vector<int>& anchors)
{
    double best = 0.0;
    for (const int anchor : anchors)
    {
        const int delta = std::abs(anchor - tick);
        if (delta <= HiResTiming::kTicks1_32)
            best = std::max(best, 1.0);
        else if (delta <= HiResTiming::kTicks1_16)
            best = std::max(best, 0.78);
        else if (delta <= HiResTiming::kTicks1_8)
            best = std::max(best, 0.52);
    }
    return best;
}

bool step32InsideCluster(const std::vector<ReferenceHatCluster>& clusters, int bar, int step32, bool requireTriplet)
{
    for (const auto& cluster : clusters)
    {
        if (cluster.barIndex != bar)
            continue;
        if (requireTriplet && !cluster.triplet)
            continue;
        if (step32 >= cluster.startStep32 && step32 <= cluster.endStep32)
            return true;
    }
    return false;
}
}

std::vector<int> DrillHatGenerator::collectTicks(const TrackState* track) const
{
    std::vector<int> ticks;
    if (track == nullptr)
        return ticks;

    ticks.reserve(track->notes.size());
    for (const auto& n : track->notes)
        ticks.push_back(HiResTiming::noteTick(n));

    std::sort(ticks.begin(), ticks.end());
    ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
    return ticks;
}

std::vector<int> DrillHatGenerator::collectSnareTargets(const TrackState* snareTrack,
                                                        const TrackState* clapTrack,
                                                        int bars) const
{
    std::vector<int> ticks = collectTicks(snareTrack);
    auto clap = collectTicks(clapTrack);
    ticks.insert(ticks.end(), clap.begin(), clap.end());
    std::sort(ticks.begin(), ticks.end());
    ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());

    if (!ticks.empty())
        return ticks;

    // Fallback when snare lane is absent/empty.
    for (int bar = 0; bar < bars; ++bar)
    {
        const int start = bar * HiResTiming::kTicksPerBar4_4;
        ticks.push_back(start + 4 * HiResTiming::kTicks1_16);
        ticks.push_back(start + 12 * HiResTiming::kTicks1_16);
    }
    return ticks;
}

std::vector<DrillHatGenerator::BarContext> DrillHatGenerator::buildBarContext(const std::vector<DrillPhraseRole>& phrase,
                                                                               const std::vector<int>& snareTargets,
                                                                               const std::vector<int>& kickTicks,
                                                                               const ReferenceHatSkeleton* referenceSkeleton,
                                                                               const DrillGrooveBlueprint* blueprint,
                                                                               const DrillStyleProfile& style,
                                                                               int bars) const
{
    std::vector<BarContext> ctx;
    ctx.reserve(static_cast<size_t>(bars));

    for (int bar = 0; bar < bars; ++bar)
    {
        BarContext b;
        b.barIndex = bar;
        const DrillPhraseRole phraseRole = bar < static_cast<int>(phrase.size())
            ? phrase[static_cast<size_t>(bar)]
            : DrillPhraseRole::Base;
        b.role = barRoleFromPhrase(phraseRole, bar, bars);
        b.isTransitionBar = b.role == BarRole::Transition || b.role == BarRole::Ending;

        const size_t curveIndex = static_cast<size_t>(std::clamp(bar % 4, 0, 3));
        b.phraseEnergy = roleEnergy(b.role) * style.phraseComplexityCurve[curveIndex];
        if (phraseRole == DrillPhraseRole::Tension)
            b.phraseEnergy *= 1.06;
        else if (phraseRole == DrillPhraseRole::Ending)
            b.phraseEnergy *= 1.08;

        const int start = bar * HiResTiming::kTicksPerBar4_4;
        const int end = start + HiResTiming::kTicksPerBar4_4;
        for (const int t : snareTargets)
            if (t >= start && t < end)
                b.snareTargetTicks.push_back(t);
        for (const int t : kickTicks)
            if (t >= start && t < end)
                b.kickTicks.push_back(t);

        b.phaseAnchorTicks.push_back(start);
        for (const int kickTick : b.kickTicks)
        {
            const int localTick = kickTick - start;
            if ((localTick % HiResTiming::kTicks1_8) == 0 || localTick <= HiResTiming::kTicks1_16)
                b.phaseAnchorTicks.push_back(kickTick);
        }
        for (const int snareTick : b.snareTargetTicks)
            b.phaseAnchorTicks.push_back(std::max(start, snareTick - HiResTiming::kTicks1_8));

        if (const auto* referenceBar = referenceBarFor(referenceSkeleton, bar))
        {
            for (const int step32 : referenceBar->phraseAnchorSteps32)
                b.phaseAnchorTicks.push_back(start + step32 * HiResTiming::kTicks1_32);
            if (referenceBar->hasBarStartAnchor)
                b.phaseAnchorTicks.push_back(start);
        }

        std::sort(b.phaseAnchorTicks.begin(), b.phaseAnchorTicks.end());
        b.phaseAnchorTicks.erase(std::unique(b.phaseAnchorTicks.begin(), b.phaseAnchorTicks.end()), b.phaseAnchorTicks.end());

        b.kickDensity = std::clamp(static_cast<double>(b.kickTicks.size()) / 6.0, 0.0, 1.0);
        ctx.push_back(std::move(b));
    }

    if (ctx.empty())
    {
        BarContext b;
        b.phraseEnergy = 1.0;
        ctx.push_back(b);
    }

    (void)referenceSkeleton;
    (void)blueprint;
    return ctx;
}

std::vector<DrillHatGenerator::HatGridFeature> DrillHatGenerator::buildGridFeatures(const std::vector<BarContext>& contexts,
                                                                                     const ReferenceHatSkeleton* referenceSkeleton,
                                                                                      const DrillGrooveBlueprint* blueprint,
                                                                                      int bars) const
{
    std::vector<HatGridFeature> out;
    out.reserve(static_cast<size_t>(bars * 32));

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto& ctx = contexts[static_cast<size_t>(std::clamp(bar, 0, static_cast<int>(contexts.size()) - 1))];
        const int start = bar * HiResTiming::kTicksPerBar4_4;

        for (int step32 = 0; step32 < 32; ++step32)
        {
            HatGridFeature f;
            f.tick = start + step32 * HiResTiming::kTicks1_32;
            f.step = bar * 32 + step32;
            f.metrical = metricalWeight32(step32);
            f.phrase = ctx.phraseEnergy;
            f.offbeat = offbeatValue(step32);
            f.preSnare = preSnareValue(f.tick, ctx.snareTargetTicks);
            f.preKick = preKickValue(f.tick, ctx.kickTicks);
            f.localDensity = 0.0;
            f.isolation = 1.0;
            f.phaseAnchor = anchorPhaseValue(f.tick, ctx.phaseAnchorTicks);

            if (const auto* referenceBar = referenceBarFor(referenceSkeleton, bar))
            {
                const int step16 = step32 / 2;
                f.referenceBackbone = containsValue(referenceBar->backboneSteps16, step16) ? 1.0 : 0.0;
                f.referenceMotion = containsValue(referenceBar->motionSteps32, step32) ? 1.0 : 0.0;
                f.referencePreSnareZone = containsValue(referenceBar->preSnareZoneSteps32, step32);
                if (containsValue(referenceBar->phraseAnchorSteps32, step32))
                    f.phaseAnchor = std::max(f.phaseAnchor, 1.0);
                f.referenceRollZone = step32InsideCluster(referenceSkeleton->rollClusters, bar, step32, false);
                f.referenceTripletZone = step32InsideCluster(referenceSkeleton->tripletClusters, bar, step32, true);
            }

            const int step16 = step32 / 2;
            const int absoluteStep16 = bar * 16 + step16;
            const auto* slot = blueprint != nullptr ? blueprint->slotAt(absoluteStep16) : nullptr;
            if (slot != nullptr)
            {
                f.blueprintBackboneWeight = std::clamp(static_cast<double>(slot->hatBackboneWeight), 0.0, 1.25);
                f.blueprintFillAllowed = slot->hatFillAllowed;
                f.blueprintRollAllowed = slot->rollAllowed;
                f.blueprintAccentAllowed = slot->accentAllowed;
                f.phraseEdgeWeight = std::clamp(static_cast<double>(slot->phraseEdgeWeight), 0.0, 1.0);
                f.snareProtection = slot->snareProtection;
                const bool kickWindowOwned = slot->kickPlaced && slot->kickEventType != DrillKickEventType::AnchorKick;
                f.majorEventCompetition = slot->majorEventReserved ? 1.0 : (f.phraseEdgeWeight * 0.6);
                if (kickWindowOwned)
                    f.majorEventCompetition = std::max(f.majorEventCompetition, 0.92);

                const bool silenceByReservation = slot->majorEventReserved && f.phraseEdgeWeight >= 0.7;
                const bool kickPhraseEdgeOwned = slot->kickPlaced && slot->kickEventType == DrillKickEventType::PhraseEdgeKick;
                f.reservedSilence = silenceByReservation || slot->snareProtection || !slot->hatFillAllowed || kickPhraseEdgeOwned;
            }
            else
            {
                f.blueprintBackboneWeight = 0.5;
                f.blueprintFillAllowed = true;
                f.blueprintRollAllowed = true;
                f.blueprintAccentAllowed = true;
                f.reservedSilence = false;
                f.majorEventCompetition = 0.0;
                f.phraseEdgeWeight = 0.0;
                f.snareProtection = false;
            }
            out.push_back(f);
        }
    }

    return out;
}

void DrillHatGenerator::generateBackboneHats(std::vector<HatEvent>& hats,
                                             const std::vector<BarContext>& contexts,
                                             std::vector<HatGridFeature>& features,
                                             const DrillStyleProfile& style,
                                             const DrillHatBaseSpec& spec,
                                             const ReferenceHatSkeleton* referenceSkeleton,
                                             const DrillGrooveBlueprint* blueprint,
                                             int bars,
                                             std::mt19937& rng) const
{
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    const bool useReferenceBackbone = referenceSkeleton != nullptr
        && referenceSkeleton->available
        && !referenceSkeleton->barMaps.empty();
    const auto& templates = fixedHatBackboneTemplates(style.substyle);

    for (int bar = 0; bar < bars; ++bar)
    {
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;
        const auto* referenceBar = referenceBarFor(referenceSkeleton, bar);
        std::vector<int> backboneSteps16;

        if (useReferenceBackbone && referenceBar != nullptr)
        {
            backboneSteps16 = referenceBar->backboneSteps16;
            if (referenceBar->hasBarStartAnchor)
                backboneSteps16.push_back(0);
            for (const int step32 : referenceBar->phraseAnchorSteps32)
                backboneSteps16.push_back(step32 / 2);
            sortAndUnique(backboneSteps16);
        }
        else
        {
            if (!tickOccupied(hats, barStart, 0))
                hats.push_back({ barStart, 0, 0, HatEntity::Backbone, false, false, false });

            const auto& refPattern = templates[static_cast<size_t>(bar % static_cast<int>(templates.size()))];
            for (int step16 = 0; step16 < 16; ++step16)
            {
                const int absoluteStep16 = bar * 16 + step16;
                if (hasReferenceAnchorAtStep16(absoluteStep16, refPattern))
                    backboneSteps16.push_back(step16);
            }
        }

        for (const int step16 : backboneSteps16)
        {
            const int absoluteStep16 = bar * 16 + step16;
            const bool strongAnchor = (step16 == 0 || step16 == 4 || step16 == 8 || step16 == 12);
            if (useReferenceBackbone && !strongAnchor && chance(rng) < 0.12)
                continue;

            const int tick = barStart + step16 * HiResTiming::kTicks1_16;
            const int tolerance = HiResTiming::kTicks1_64;
            if (tickOccupied(hats, tick, tolerance) || tickOccupied(hats, tick, HiResTiming::kTicks1_16 - 10))
                continue;

            const auto* f = findFeature(features, tick);
            if (f != nullptr)
            {
                if (f->reservedSilence && !strongAnchor)
                    continue;
                if (f->snareProtection && !strongAnchor)
                    continue;
                if (f->majorEventCompetition >= 0.95 && !strongAnchor)
                    continue;
            }
            if (phraseEdgeOwnedByKick(blueprint, absoluteStep16))
                continue;
            const double preKick = f != nullptr ? f->preKick : 0.0;
            if (preKick > 0.95)
                continue;

            hats.push_back({ tick, 0, 0, HatEntity::Backbone, false, false, false });
        }
    }

    // Keep only tiny controlled variation beyond the template backbone.
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto& ctx = contexts[static_cast<size_t>(std::clamp(bar, 0, static_cast<int>(contexts.size()) - 1))];
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;
        const int barEnd = barStart + HiResTiming::kTicksPerBar4_4;
        const int maxExtraBackbone = ctx.role == BarRole::Response ? 0 : 1;
        int addedExtraBackbone = 0;

        std::vector<std::pair<double, int>> ranked;
        for (auto& f : features)
        {
            if (f.tick < barStart || f.tick >= barEnd)
                continue;
            if ((stepInBar32FromTick(f.tick) % 2) != 0)
                continue;
            if (tickOccupied(hats, f.tick, HiResTiming::kTicks1_16 - 6))
                continue;
            if (f.reservedSilence && f.metrical < 0.98)
                continue;
            if (f.snareProtection && f.metrical < 1.0)
                continue;

            f.localDensity = localHatDensityWeighted(f.tick, hats);
            const double densityCtl = densityControlWeighted(f.tick, hats, style.lambdaDensity);
            const double kickPenalty = 1.0 - 0.28 * f.preKick;
            const double silencePenalty = f.reservedSilence ? 0.22 : 1.0;
            const double snarePenalty = f.snareProtection ? 0.35 : 1.0;
            const double majorPenalty = 1.0 - std::clamp(f.majorEventCompetition * 0.52, 0.0, 0.72);
            const double referenceBias = useReferenceBackbone
                ? std::clamp(0.72 + f.referenceBackbone * 0.40 + f.phaseAnchor * 0.18, 0.55, 1.35)
                : 1.0;
            const double pHat = clampProb(style.baseHat * 0.62 * f.metrical * f.phrase * (0.72 + 0.28 * f.offbeat)
                                          * densityCtl * kickPenalty
                                          * std::clamp(f.blueprintBackboneWeight, 0.15, 1.25)
                                          * silencePenalty * snarePenalty * majorPenalty * referenceBias);

            ranked.push_back({ pHat, f.tick });
            if (addedExtraBackbone < maxExtraBackbone
                && chance(rng) < pHat
                && !tickOccupied(hats, f.tick, HiResTiming::kTicks1_64))
            {
                hats.push_back({ f.tick, 0, 0, HatEntity::Backbone, false, false, false });
                ++addedExtraBackbone;
            }
        }

        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        int current = countByBar(hats, bar);
        const int minTarget = std::max(backboneMinPerBar(style.substyle), style.minHatsPerBar - (ctx.kickDensity > 0.66 ? 1 : 0));
        const int maxTarget = backboneMaxPerBar(style.substyle);
        for (const auto& it : ranked)
        {
            if (current >= minTarget)
                break;
            if (current >= maxTarget)
                break;
            if (!tickOccupied(hats, it.second, HiResTiming::kTicks1_64)
                && !tickOccupied(hats, it.second, HiResTiming::kTicks1_16 - 6))
            {
                hats.push_back({ it.second, 0, 0, HatEntity::Backbone, false, false, false });
                ++current;
            }
        }

        if (current > maxTarget)
        {
            for (auto it = hats.begin(); it != hats.end() && current > maxTarget;)
            {
                if (barFromTick(it->tick) == bar && it->entity == HatEntity::Backbone && (stepInBar32FromTick(it->tick) % 8) != 0)
                {
                    it = hats.erase(it);
                    --current;
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    (void)spec;
    (void)referenceSkeleton;
    (void)blueprint;
}

void DrillHatGenerator::generateFillHats(std::vector<HatEvent>& hats,
                                         const std::vector<BarContext>& contexts,
                                         std::vector<HatGridFeature>& features,
                                         const DrillStyleProfile& style,
                                         const MotionProfile& motion,
                                         const DrillHatBaseSpec& spec,
                                         const DrillGrooveBlueprint* blueprint,
                                         int bars,
                                         std::mt19937& rng) const
{
    std::uniform_real_distribution<double> chance(0.0, 1.0);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto& ctx = contexts[static_cast<size_t>(std::clamp(bar, 0, static_cast<int>(contexts.size()) - 1))];
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;
        const int barEnd = barStart + HiResTiming::kTicksPerBar4_4;
        const int dynamicMax = std::max(style.minHatsPerBar,
                                        style.maxHatsPerBar - static_cast<int>(std::round(ctx.kickDensity * 2.0)));
        int fillSoftCap = (ctx.role == BarRole::Transition || ctx.role == BarRole::Ending) ? 2 : 1;
        if (motion.expressiveMode)
            fillSoftCap += 1;
        if (motion.sparseMode)
            fillSoftCap = std::max(0, fillSoftCap - 1);
        if (motion.mode == MotionMode::Low)
            fillSoftCap = std::min(fillSoftCap, 1);
        if (style.cleanHatMode)
            fillSoftCap = ctx.isTransitionBar ? 1 : 0;
        if (ctx.role == BarRole::Response)
            fillSoftCap = std::min(fillSoftCap, 1);

        int effectiveDynamicMax = (style.substyle == DrillSubstyle::BrooklynDrill)
            ? std::max(style.minHatsPerBar, dynamicMax - 1)
            : dynamicMax;
        const double barDensityBias = ((bar % 2) == 0 ? -1.0 : 1.0) * motion.densitySwing;
        effectiveDynamicMax += static_cast<int>(std::round(barDensityBias * (ctx.isTransitionBar ? 2.0 : 1.0)));
        effectiveDynamicMax = std::clamp(effectiveDynamicMax, style.minHatsPerBar, style.maxHatsPerBar + 1);
        const int windowLimitedFillCap = 1;
        std::array<int, 4> fillPerWindow { 0, 0, 0, 0 };
        std::array<double, 4> gapWindowScore { 0.0, 0.0, 0.0, 0.0 };
        int fillCount = 0;
        for (const auto& h : hats)
            if (barFromTick(h.tick) == bar && h.entity == HatEntity::Fill)
                ++fillCount;

        for (const auto& f : features)
        {
            if (f.tick < barStart || f.tick >= barEnd)
                continue;
            const int window = eventWindowIndexFromTick(f.tick);
            gapWindowScore[static_cast<size_t>(window)] += (1.0 - f.metrical) + f.majorEventCompetition * 0.8 + (f.preKick * 0.25);
        }

        int reservedGapWindow = -1;
        if (!style.cleanHatMode && motion.gapIntentWeight > 1.02 && !ctx.isTransitionBar)
        {
            reservedGapWindow = static_cast<int>(std::distance(gapWindowScore.begin(),
                std::max_element(gapWindowScore.begin(), gapWindowScore.end())));
        }

        std::vector<std::pair<double, int>> ranked;
        for (auto& f : features)
        {
            if (f.tick < barStart || f.tick >= barEnd)
                continue;
            if ((stepInBar32FromTick(f.tick) % 2) == 0)
                continue;
            if (tickOccupied(hats, f.tick, HiResTiming::kTicks1_64))
                continue;
            if (tickOccupied(hats, f.tick, HiResTiming::kTicks1_16 - 8))
                continue;
            if (!f.blueprintFillAllowed)
                continue;
            if (f.reservedSilence)
                continue;
            if (f.snareProtection)
                continue;
            if (f.majorEventCompetition > 0.72)
                continue;
            if (reservedGapWindow >= 0 && eventWindowIndexFromTick(f.tick) == reservedGapWindow)
                continue;
            const int step16 = f.tick / HiResTiming::kTicks1_16;
            if (phraseEdgeOwnedByKick(blueprint, step16))
                continue;
            if (countMotionByBar(hats, bar) >= motion.maxMotionEventsPerBar)
                continue;

            f.localDensity = localHatDensityWeighted(f.tick, hats);
            const double densityCtl = densityControlWeighted(f.tick, hats, style.lambdaDensity);
            const double kickConflictPenalty = 1.0 - 0.24 * f.preKick;
            const double postSnarePenalty = f.preSnare > 0.95 ? 0.86 : 1.0;
            const double competitionPenalty = 1.0 - std::clamp(f.majorEventCompetition * 0.75, 0.0, 0.85);
            const double variationBias = motion.expressiveMode
                ? (ctx.isTransitionBar ? 1.10 : 1.0 + (motion.densityVariationWeight - 1.0) * 0.18)
                : 1.0;
            const double modeBias = motion.mode == MotionMode::Low ? 0.78
                : (motion.mode == MotionMode::Mid ? 1.0 : 1.12);
            const double referenceBias = std::clamp(0.72
                                                    + f.referenceMotion * 0.42
                                                    + f.phaseAnchor * 0.16
                                                    + (f.referencePreSnareZone ? 0.12 : 0.0),
                                                    0.5,
                                                    1.45);
            const double pFill = clampProb(style.baseFill * f.offbeat * f.phrase * densityCtl
                                           * kickConflictPenalty * postSnarePenalty * competitionPenalty * variationBias * modeBias * referenceBias);

            if (style.cleanHatMode)
            {
                const bool phraseEdge = stepInBar32FromTick(f.tick) >= 24;
                const bool cleanEligible = (f.preSnare > 0.90) || phraseEdge;
                if (!cleanEligible)
                    continue;
            }

            ranked.push_back({ pFill, f.tick });
            const int window = eventWindowIndexFromTick(f.tick);
            if (fillCount < fillSoftCap
                && fillPerWindow[static_cast<size_t>(window)] < windowLimitedFillCap
                && countByBar(hats, bar) < effectiveDynamicMax
                && chance(rng) < pFill
                && !tickOccupied(hats, f.tick, HiResTiming::kTicks1_64))
            {
                hats.push_back({ f.tick, 0, 0, HatEntity::Fill, false, false, false });
                ++fillCount;
                ++fillPerWindow[static_cast<size_t>(window)];
            }
        }

        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        int current = countByBar(hats, bar);
        for (const auto& it : ranked)
        {
            if (current >= style.minHatsPerBar)
                break;
            if (fillCount >= fillSoftCap)
                break;
            const int window = eventWindowIndexFromTick(it.second);
            if (fillPerWindow[static_cast<size_t>(window)] >= windowLimitedFillCap)
                continue;
            if (!tickOccupied(hats, it.second, HiResTiming::kTicks1_64)
                && !tickOccupied(hats, it.second, HiResTiming::kTicks1_16 - 8))
            {
                hats.push_back({ it.second, 0, 0, HatEntity::Fill, false, false, false });
                ++current;
                ++fillCount;
                ++fillPerWindow[static_cast<size_t>(window)];
            }
        }
    }

    (void)spec;
    (void)blueprint;
}

std::vector<DrillHatGenerator::RollSegmentState> DrillHatGenerator::createRollSegments(const std::vector<HatEvent>& hats,
                                                                                        const std::vector<BarContext>& contexts,
                                                                                        std::vector<HatGridFeature>& features,
                                                                                        const DrillStyleProfile& style,
                                                                                        const MotionProfile& motion,
                                                                                        const DrillHatBaseSpec& spec,
                                                                                        const DrillGrooveBlueprint* blueprint,
                                                                                        int bars,
                                                                                        std::mt19937& rng) const
{
    struct Candidate
    {
        int bar = 0;
        int tick = 0;
        double score = 0.0;
        double preSnare = 0.0;
        bool endBar = false;
    };

    std::uniform_real_distribution<double> chance(0.0, 1.0);
    std::uniform_int_distribution<int> notesShort(2, 3);
    std::uniform_int_distribution<int> notesMid(2, 4);
    std::uniform_int_distribution<int> notesLong(3, 5);
    std::uniform_int_distribution<int> notesBurst(2, 6);
    std::uniform_int_distribution<int> shapePick(0, 5);

    std::vector<Candidate> candidates;
    for (auto& f : features)
    {
        const int bar = barFromTick(f.tick);
        const auto& ctx = contexts[static_cast<size_t>(std::clamp(bar, 0, static_cast<int>(contexts.size()) - 1))];
        const int step32 = stepInBar32FromTick(f.tick);

        const bool preSnare = f.preSnare > 0.90;
        const bool endBar = step32 >= 24;
        const bool midBar = step32 >= 8 && step32 <= 20;
        const bool offGrid = (step32 % 2) == 1;

        if (!f.blueprintRollAllowed)
            continue;
        if (f.reservedSilence)
            continue;
        if (f.snareProtection && !preSnare)
            continue;
        if (f.majorEventCompetition > 0.88 && f.phraseEdgeWeight >= 0.7)
            continue;

        if (!style.allowMidBarRoll && midBar)
            continue;
        if (style.preferPreSnareRoll && !preSnare && !endBar)
            continue;
        if (!preSnare && !endBar && !offGrid)
            continue;
        if (tickOccupied(hats, f.tick, HiResTiming::kTicks1_32))
            continue;

        const double densityCtl = densityControlWeighted(f.tick, hats, style.lambdaDensity);
        double score = style.baseRoll * f.phrase * f.metrical * densityCtl;
        score *= std::clamp(0.72 + f.phaseAnchor * 0.18 + (f.referenceRollZone ? 0.34 : 0.0), 0.45, 1.5);
        if (preSnare)
            score *= style.preSnareBoostRoll;
        if (endBar && style.preferEndBarRoll)
            score *= style.endBarBoostRoll;
        if (ctx.isTransitionBar)
            score *= 1.08;
        if (offGrid)
            score *= std::clamp(0.92 + (motion.burstWeight - 1.0) * 0.30, 0.8, 1.18);
        if (f.referenceTripletZone)
            score *= std::clamp(0.95 + (motion.tripletWeight - 1.0) * 0.28, 0.84, 1.22);
        if (preSnare || endBar)
            score *= std::clamp(0.95 + (motion.rollLengthWeight - 1.0) * 0.20, 0.84, 1.16);
        score *= std::clamp(1.0 - f.majorEventCompetition * 0.55, 0.2, 1.0);

        candidates.push_back({ bar, f.tick, score, f.preSnare, endBar });
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
    {
        return a.score > b.score;
    });

    int totalMin = std::max(0, static_cast<int>(std::floor(static_cast<double>(bars) * motion.rollsPerBarTarget * 0.45)));
    int totalMax = std::max(totalMin, static_cast<int>(std::ceil(static_cast<double>(bars) * motion.rollsPerBarTarget)));
    totalMin = std::max(totalMin, static_cast<int>(std::ceil((static_cast<double>(bars) / 4.0)
                                                              * static_cast<double>(style.minRollsPer4Bars))));
    totalMax = std::max(totalMax,
                        static_cast<int>(std::ceil((static_cast<double>(bars) / 4.0)
                                                   * static_cast<double>(style.maxRollsPer4Bars))));
    const int hardRollsPer2Bars = maxRollsPer2BarsHard(style.substyle);

    if (style.cleanHatMode)
    {
        totalMin = 0;
        totalMax = std::min(totalMax, std::max(1, bars / 4));
    }

    std::vector<RollSegmentState> segments;
    std::vector<int> rollBars;
    std::vector<std::array<int, 4>> rollWindowBudget(static_cast<size_t>(bars));
    for (auto& windows : rollWindowBudget)
        windows = { 0, 0, 0, 0 };

    for (const auto& c : candidates)
    {
        if (static_cast<int>(segments.size()) >= totalMax)
            break;

        const auto& ctx = contexts[static_cast<size_t>(c.bar)];
        int rollSegmentsPerBarCap = style.maxRollSegmentsPerBar;
        if (motion.mode == MotionMode::Low)
            rollSegmentsPerBarCap = std::min(rollSegmentsPerBarCap, 1);
        else if (motion.mode == MotionMode::High && (ctx.isTransitionBar || c.endBar || c.preSnare > 0.90))
            rollSegmentsPerBarCap = std::min(rollSegmentsPerBarCap + 1, 2);

        if (countRollsByBar(segments, c.bar) >= rollSegmentsPerBarCap)
            continue;
        if (countRollsByTwoBars(segments, c.bar) >= std::min(style.maxRollSegmentsPer2Bars, hardRollsPer2Bars))
            continue;

        const int rollWindow = eventWindowIndexFromTick(c.tick);
        if (rollWindowBudget[static_cast<size_t>(c.bar)][static_cast<size_t>(rollWindow)] >= 1)
            continue;

        const int rollStep16 = c.tick / HiResTiming::kTicks1_16;
        if (phraseEdgeOwnedByKick(blueprint, rollStep16))
            continue;

        const auto role = ctx.role;
        if (role == BarRole::Response && !(c.preSnare > 0.92 && c.endBar))
            continue;

        bool tooClose = false;
        for (const auto& s : segments)
            if (std::abs(s.startTick - c.tick) < style.minDistanceRollTicks)
                tooClose = true;
        if (tooClose)
            continue;

        int consecutive = 1;
        if (!rollBars.empty())
        {
            int last = rollBars.back();
            if (c.bar == last + 1)
            {
                consecutive = 2;
                for (int i = static_cast<int>(rollBars.size()) - 2; i >= 0; --i)
                {
                    if (rollBars[static_cast<size_t>(i)] == rollBars[static_cast<size_t>(i + 1)] - 1)
                        ++consecutive;
                    else
                        break;
                }
            }
        }
        if (consecutive > style.maxConsecutiveRollBars)
            continue;

        if (style.substyle == DrillSubstyle::UKDrill)
        {
            const bool ukEligible = c.preSnare > 0.90 || c.endBar;
            if (!ukEligible)
                continue;
        }

        const bool forceBecauseMin = static_cast<int>(segments.size()) < totalMin;
        if (!forceBecauseMin && chance(rng) > clampProb(c.score))
            continue;

        if (style.cleanHatMode && !(c.preSnare > 0.90 || c.endBar))
            continue;

        RollSegmentState seg;
        seg.startTick = c.tick;
        seg.targetSnareTick = nearestForward(c.tick, ctx.snareTargetTicks);
        seg.energy = std::clamp(c.score * 1.5, 0.65, 1.45);

        const bool burstPreferred = motion.preferBurstClusters && (c.preSnare > 0.90 || c.endBar || ctx.isTransitionBar);
        const double tripletBias = std::max(0.01, style.tripletSubpulseWeight * motion.tripletWeight);
        const double straightBias = std::max(0.01, style.straight32SubpulseWeight);
        const double burstBias = burstPreferred ? std::max(0.01, motion.burstWeight * 0.95) : 0.01;
        const double totalBias = tripletBias + straightBias + burstBias;
        const double pick = chance(rng) * totalBias;
        if (pick < burstBias)
        {
            seg.subdivisionTicks = static_cast<double>(HiResTiming::kTicks1_64);
            seg.burstCluster = true;
        }
        else if (pick < (burstBias + tripletBias))
        {
            seg.subdivisionTicks = static_cast<double>(HiResTiming::kTicks1_24);
            seg.tripletGroup = true;
        }
        else
        {
            seg.subdivisionTicks = static_cast<double>(HiResTiming::kTicks1_32);
        }

        if (ctx.role == BarRole::Ending)
            seg.noteCount = notesLong(rng);
        else if (ctx.role == BarRole::Transition)
            seg.noteCount = notesMid(rng);
        else
            seg.noteCount = notesShort(rng);

        seg.noteCount = std::max(seg.noteCount,
                                 static_cast<int>(std::lround(motion.averageRollNotes)));
        if (motion.expressiveMode && (c.preSnare > 0.90 || c.endBar))
            seg.noteCount += 1;
        if (motion.sparseMode)
            seg.noteCount = std::max(2, seg.noteCount - 1);
        if (seg.burstCluster)
            seg.noteCount = std::max(seg.noteCount, notesBurst(rng));

        if (style.substyle == DrillSubstyle::UKDrill)
            seg.noteCount = std::clamp(seg.noteCount, 2, 3);
        else
            seg.noteCount = std::clamp(seg.noteCount, 2, 6);

        seg.groupSize = seg.noteCount;

        seg.lenTicks = static_cast<int>(std::round(seg.subdivisionTicks * static_cast<double>(seg.noteCount)));
        seg.shape = static_cast<RollShape>(shapePick(rng));

        if (contexts[static_cast<size_t>(c.bar)].role == BarRole::Ending)
            seg.exitType = RollExitType::DoubleTail;
        else if (c.preSnare > 0.90)
            seg.exitType = RollExitType::SingleTail;
        else
            seg.exitType = chance(rng) < 0.25 ? RollExitType::PauseAfter : RollExitType::None;

        if (seg.burstCluster && seg.noteCount >= 4 && chance(rng) < 0.45)
            seg.shape = chance(rng) < 0.5 ? RollShape::AccentFirst : RollShape::AccentLast;

        segments.push_back(seg);
        rollBars.push_back(c.bar);
        ++rollWindowBudget[static_cast<size_t>(c.bar)][static_cast<size_t>(rollWindow)];
    }

    (void)spec;
    (void)blueprint;
    return segments;
}

void DrillHatGenerator::renderRollSegments(std::vector<HatEvent>& hats,
                                           const std::vector<RollSegmentState>& segments,
                                           const DrillStyleProfile& style,
                                           int bars) const
{
    const int totalTicks = bars * HiResTiming::kTicksPerBar4_4;

    for (const auto& seg : segments)
    {
        const int notes = std::max(1, seg.noteCount);
        const int peakIndex = notes / 2;
        for (int i = 0; i < notes; ++i)
        {
            const int tick = seg.startTick + static_cast<int>(std::lround(seg.subdivisionTicks * static_cast<double>(i)));
            if (tick < 0 || tick >= totalTicks)
                continue;

            double shapeValue = 0.70;
            switch (seg.shape)
            {
                case RollShape::Flat:
                    shapeValue = 0.78;
                    break;
                case RollShape::Crescendo:
                    shapeValue = 0.55 + 0.45 * static_cast<double>(i) / static_cast<double>(std::max(1, notes - 1));
                    break;
                case RollShape::Decrescendo:
                    shapeValue = 1.00 - 0.35 * static_cast<double>(i) / static_cast<double>(std::max(1, notes - 1));
                    break;
                case RollShape::Arch:
                {
                    const double x = static_cast<double>(i - peakIndex) / static_cast<double>(std::max(1, peakIndex));
                    shapeValue = 0.58 + 0.38 * (1.0 - x * x);
                    break;
                }
                case RollShape::AccentFirst:
                    shapeValue = i == 0 ? 1.00 : 0.62;
                    break;
                case RollShape::AccentLast:
                    shapeValue = i == (notes - 1) ? 1.00 : 0.62;
                    break;
            }

            const int baseMin = style.hatVelocityMin;
            const int baseMax = style.hatVelocityMax;
            const int velocity = std::clamp(static_cast<int>(std::lround(baseMin + (baseMax - baseMin) * shapeValue * seg.energy)),
                                            1,
                                            127);

            HatEvent e;
            e.tick = tick;
            e.velocity = velocity;
            e.entity = HatEntity::Roll;
            e.rollPeak = i == peakIndex;
            e.rollExit = i == notes - 1;
            hats.push_back(e);
        }

        const int tailBase = seg.startTick + seg.lenTicks;
        if (seg.exitType == RollExitType::SingleTail)
            hats.push_back({ tailBase, std::clamp(style.hatVelocityMax - 8, 1, 127), 0, HatEntity::Fill, false, false, true });
        else if (seg.exitType == RollExitType::DoubleTail)
        {
            hats.push_back({ tailBase, std::clamp(style.hatVelocityMax - 10, 1, 127), 0, HatEntity::Fill, false, false, true });
            hats.push_back({ tailBase + HiResTiming::kTicks1_32, std::clamp(style.hatVelocityMax - 14, 1, 127), 0, HatEntity::Fill, false, false, true });
        }
    }
}

void DrillHatGenerator::assignHatAccents(std::vector<HatEvent>& hats,
                                         const std::vector<RollSegmentState>& segments,
                                         const std::vector<BarContext>& contexts,
                                         std::vector<HatGridFeature>& features,
                                         const DrillStyleProfile& style,
                                         const MotionProfile& motion,
                                         const DrillGrooveBlueprint* blueprint,
                                         int bars,
                                         std::mt19937& rng) const
{
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    std::sort(hats.begin(), hats.end(), [](const HatEvent& a, const HatEvent& b) { return a.tick < b.tick; });

    auto promoteAccentGroup = [&](size_t seedIndex, int desiredGroupSize)
    {
        if (!isMotionEntity(hats[seedIndex].entity))
            return std::vector<size_t> { seedIndex };

        std::vector<size_t> group;
        const int seedBar = barFromTick(hats[seedIndex].tick);
        const int seedTick = hats[seedIndex].tick;
        const int groupWindow = motion.mode == MotionMode::High ? HiResTiming::kTicks1_8 : HiResTiming::kTicks1_16;

        for (int direction = -1; direction <= 1; direction += 2)
        {
            for (int offset = 1; offset <= desiredGroupSize; ++offset)
            {
                const int signedIndex = static_cast<int>(seedIndex) + direction * offset;
                if (signedIndex < 0 || signedIndex >= static_cast<int>(hats.size()))
                    break;

                const size_t candidateIndex = static_cast<size_t>(signedIndex);
                if (barFromTick(hats[candidateIndex].tick) != seedBar)
                    break;
                if (!isMotionEntity(hats[candidateIndex].entity))
                    break;
                if (std::abs(hats[candidateIndex].tick - seedTick) > groupWindow)
                    break;

                group.push_back(candidateIndex);
                if (static_cast<int>(group.size()) >= desiredGroupSize - 1)
                    break;
            }
        }

        group.push_back(seedIndex);
        std::sort(group.begin(), group.end());
        group.erase(std::unique(group.begin(), group.end()), group.end());
        return group;
    };

    std::vector<int> accentCount(static_cast<size_t>(bars), 0);
    std::vector<std::array<int, 4>> accentPerWindow(static_cast<size_t>(bars));
    for (auto& windows : accentPerWindow)
        windows = { 0, 0, 0, 0 };
    int lastAccentTick = -1;

    for (size_t i = 0; i < hats.size(); ++i)
    {
        auto& h = hats[i];
        const int bar = std::clamp(barFromTick(h.tick), 0, bars - 1);
        auto* f = findFeatureMutable(features, h.tick);
        if (f == nullptr)
            continue;
        if (!f->blueprintAccentAllowed)
            continue;
        if (f->reservedSilence)
            continue;
        if (f->snareProtection && !h.rollPeak && !h.rollExit)
            continue;
        if (f->majorEventCompetition > 0.74 && !h.rollPeak)
            continue;
        const int step16 = h.tick / HiResTiming::kTicks1_16;
        if (phraseEdgeOwnedByKick(blueprint, step16))
            continue;

        const bool phraseEdge = stepInBar32FromTick(h.tick) >= 24;
        const bool strongBackbone = f->metrical >= 0.82;
        const bool alternatingSlot = (i > 0)
            && ((stepInBar32FromTick(h.tick) + stepInBar32FromTick(hats[i - 1].tick)) % 2 == 1);

        int nearestGap = std::numeric_limits<int>::max();
        if (i > 0)
            nearestGap = std::min(nearestGap, std::abs(h.tick - hats[i - 1].tick));
        if (i + 1 < hats.size())
            nearestGap = std::min(nearestGap, std::abs(hats[i + 1].tick - h.tick));
        if (nearestGap == std::numeric_limits<int>::max())
            nearestGap = HiResTiming::kTicksPerQuarter;
        f->isolation = std::clamp(static_cast<double>(nearestGap) / static_cast<double>(HiResTiming::kTicks1_8), 0.55, 1.45);
        f->cooldownAccent = cooldownFactor(h.tick, lastAccentTick, style.accentCooldownTicks);

        const bool firstMiniGroup = (i == 0) || (std::abs(h.tick - hats[i - 1].tick) > HiResTiming::kTicks1_8);
        const bool eligible = (f->preSnare > 0.90)
            || (f->isolation > 1.00)
            || phraseEdge
            || strongBackbone
            || h.rollPeak
            || h.rollExit
            || firstMiniGroup;

        if (!eligible)
            continue;

        bool denseRollZone = false;
        for (const auto& seg : segments)
        {
            const int start = seg.startTick;
            const int end = seg.startTick + seg.lenTicks;
            if (h.tick >= start && h.tick <= end)
            {
                denseRollZone = true;
                break;
            }
        }
        if (denseRollZone && !(h.rollPeak || h.rollExit))
            continue;

        if (style.substyle == DrillSubstyle::UKDrill)
        {
            const bool ukAccentAllowed = (f->preSnare > 0.90) || phraseEdge || h.rollPeak || h.rollExit;
            if (!ukAccentAllowed)
                continue;
        }
        else if (style.substyle == DrillSubstyle::BrooklynDrill)
        {
            const bool brooklynAccentAllowed = (f->preSnare > 0.90)
                || phraseEdge
                || h.rollPeak
                || h.rollExit
                || (f->isolation > 1.20 && strongBackbone);
            if (!brooklynAccentAllowed)
                continue;
        }

        if (style.cleanHatMode)
        {
            const bool cleanAccentAllowed = (f->preSnare > 0.90) || phraseEdge || h.rollPeak || h.rollExit;
            if (!cleanAccentAllowed)
                continue;
        }

        const auto role = contexts[static_cast<size_t>(bar)].role;
        const int roleAccentCap = role == BarRole::Ending ? 2 : 1;
        if (accentCount[static_cast<size_t>(bar)] >= std::min(style.maxAccentsPerBar, roleAccentCap))
            continue;
        if (accentCount[static_cast<size_t>(bar)] >= std::min(style.maxAccentsPerBarHard, roleAccentCap))
            continue;
        const int window = eventWindowIndexFromTick(h.tick);
        if (accentPerWindow[static_cast<size_t>(bar)][static_cast<size_t>(window)] >= 1)
            continue;
        if (lastAccentTick >= 0 && std::abs(h.tick - lastAccentTick) < style.minDistanceAccentTicks)
            continue;

        const double competitionPenalty = 1.0 - std::clamp(f->majorEventCompetition * 0.7, 0.0, 0.85);
        const bool wantsGroupAccent = motion.preferAccentGroups && isMotionEntity(h.entity);
        const int desiredGroupSize = wantsGroupAccent
            ? (motion.mode == MotionMode::High ? 3 : 2)
            : 1;
        const double pAccent = clampProb(style.baseAccent
                                         * f->metrical
                                         * f->phrase
                                         * f->isolation
                                         * f->cooldownAccent
                                         * competitionPenalty
                                         * std::clamp(0.8 + f->phaseAnchor * 0.18 + (f->referencePreSnareZone ? 0.12 : 0.0), 0.6, 1.3)
                                         * (alternatingSlot ? std::clamp(motion.accentPatternWeight, 0.9, 1.28) : 1.0)
                                         * (f->preSnare > 0.90 ? style.preSnareBoostAccent : 1.0));

        if (chance(rng) < pAccent)
        {
            auto group = promoteAccentGroup(i, desiredGroupSize);
            if (desiredGroupSize > 1 && static_cast<int>(group.size()) < 2 && motion.mode == MotionMode::High)
                continue;

            for (size_t groupOffset = 0; groupOffset < group.size(); ++groupOffset)
            {
                auto& groupedHat = hats[group[groupOffset]];
                groupedHat.accent = true;
                groupedHat.entity = HatEntity::Accent;
                if (motion.mode == MotionMode::High && (groupOffset % 2) == 1)
                    groupedHat.rollPeak = true;
            }

            ++accentCount[static_cast<size_t>(bar)];
            ++accentPerWindow[static_cast<size_t>(bar)][static_cast<size_t>(window)];
            lastAccentTick = hats[group.back()].tick;
        }
    }

    (void)blueprint;
}

void DrillHatGenerator::applyHardCapsCleanup(std::vector<HatEvent>& hats,
                                             const std::vector<RollSegmentState>& segments,
                                             const std::vector<BarContext>& contexts,
                                             const DrillStyleProfile& style,
                                             const MotionProfile& motion,
                                             const DrillGrooveBlueprint* blueprint,
                                             int bars) const
{
    std::sort(hats.begin(), hats.end(), [](const HatEvent& a, const HatEvent& b) { return a.tick < b.tick; });

    if (blueprint != nullptr)
    {
        hats.erase(std::remove_if(hats.begin(), hats.end(), [&](const HatEvent& h)
        {
            const int step16 = std::max(0, h.tick / HiResTiming::kTicks1_16);
            const auto* slot = blueprint->slotAt(step16);
            if (slot == nullptr)
                return false;

            const bool reservedSilence = slot->snareProtection || (slot->majorEventReserved && slot->phraseEdgeWeight >= 0.7f);
            if (!reservedSilence)
                return false;

            const bool keepAnchorBackbone = (h.entity == HatEntity::Backbone) && ((step16 % 16) % 4 == 0);
            return !keepAnchorBackbone;
        }), hats.end());
    }

    // Roll pause zone: avoid immediate refill around roll tails.
    hats.erase(std::remove_if(hats.begin(), hats.end(), [&](const HatEvent& h)
    {
        if (h.entity != HatEntity::Fill)
            return false;

        for (const auto& seg : segments)
        {
            const int start = seg.startTick - HiResTiming::kTicks1_32;
            const int end = seg.startTick + seg.lenTicks + HiResTiming::kTicks1_16;
            if (h.tick >= start && h.tick <= end)
                return true;
        }

        return false;
    }), hats.end());

    // If a bar has a roll, fill hats are removed to keep the bar readable.
    std::vector<bool> barHasRoll(static_cast<size_t>(bars), false);
    for (const auto& seg : segments)
    {
        const int bar = std::clamp(barFromTick(seg.startTick), 0, bars - 1);
        barHasRoll[static_cast<size_t>(bar)] = true;
    }
    hats.erase(std::remove_if(hats.begin(), hats.end(), [&](const HatEvent& h)
    {
        if (h.entity != HatEntity::Fill)
            return false;
        const int bar = std::clamp(barFromTick(h.tick), 0, bars - 1);
        return barHasRoll[static_cast<size_t>(bar)] ? true : false;
    }), hats.end());

    // One major hat event per 1/4-bar window.
    for (int bar = 0; bar < bars; ++bar)
    {
        std::array<bool, 4> majorWindowUsed { false, false, false, false };
        hats.erase(std::remove_if(hats.begin(), hats.end(), [&](const HatEvent& h)
        {
            if (barFromTick(h.tick) != bar)
                return false;
            if (h.entity == HatEntity::Backbone)
                return false;

            const int window = eventWindowIndexFromTick(h.tick);
            if (!majorWindowUsed[static_cast<size_t>(window)])
            {
                majorWindowUsed[static_cast<size_t>(window)] = true;
                return false;
            }
            return true;
        }), hats.end());
    }

    std::vector<int> accentCount(static_cast<size_t>(bars), 0);
    int lastAccentTick = -1;
    hats.erase(std::remove_if(hats.begin(), hats.end(), [&](const HatEvent& h)
    {
        const int bar = std::clamp(barFromTick(h.tick), 0, bars - 1);
        if (h.entity != HatEntity::Accent)
            return false;
        const auto role = contexts[static_cast<size_t>(bar)].role;
        const int hardAccentCap = role == BarRole::Ending ? 2 : 1;
        if (accentCount[static_cast<size_t>(bar)] >= std::min(style.maxAccentsPerBarHard, hardAccentCap))
            return true;
        if (lastAccentTick >= 0 && std::abs(h.tick - lastAccentTick) < style.minDistanceAccentTicks)
            return true;

        ++accentCount[static_cast<size_t>(bar)];
        lastAccentTick = h.tick;
        return false;
    }), hats.end());

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto& ctx = contexts[static_cast<size_t>(std::clamp(bar, 0, static_cast<int>(contexts.size()) - 1))];
        int dynamicMax = std::max(style.minHatsPerBar,
                                        style.maxHatsPerBar - static_cast<int>(std::round(ctx.kickDensity * 2.0)));
        if (motion.expressiveMode && ctx.isTransitionBar)
            dynamicMax += 1;
        if (motion.sparseMode && !ctx.isTransitionBar)
            dynamicMax = std::max(style.minHatsPerBar, dynamicMax - 1);
        dynamicMax = std::min(dynamicMax, backboneMaxPerBar(style.substyle) + motion.maxMotionEventsPerBar);
        if (style.cleanHatMode)
            dynamicMax = std::min(dynamicMax, std::max(style.minHatsPerBar, 7));
        int count = countByBar(hats, bar);

        if (count > dynamicMax)
        {
            for (auto it = hats.begin(); it != hats.end() && count > dynamicMax;)
            {
                if (barFromTick(it->tick) == bar && it->entity == HatEntity::Fill)
                {
                    it = hats.erase(it);
                    --count;
                }
                else
                {
                    ++it;
                }
            }
        }

        while (count < style.minHatsPerBar)
        {
            static constexpr int kFallbackSteps[] = { 0, 4, 8, 12, 16, 20, 24, 28 };
            bool inserted = false;
            for (const int step32 : kFallbackSteps)
            {
                const int tick = bar * HiResTiming::kTicksPerBar4_4 + step32 * HiResTiming::kTicks1_32;
                if (!tickOccupied(hats, tick, HiResTiming::kTicks1_64)
                    && !tickOccupied(hats, tick, HiResTiming::kTicks1_16 - 6))
                {
                    hats.push_back({ tick, 0, 0, HatEntity::Backbone, false, false, false });
                    ++count;
                    inserted = true;
                    break;
                }
            }

            if (!inserted)
            {
                break;
            }
        }
    }

    (void)blueprint;
}

void DrillHatGenerator::applyVelocityShape(std::vector<HatEvent>& hats,
                                           const std::vector<RollSegmentState>& segments,
                                           const DrillStyleProfile& style,
                                           const DrillHatBaseSpec& spec,
                                           int bars,
                                           std::mt19937& rng) const
{
    auto asVel = [](float v)
    {
        return std::clamp(static_cast<int>(std::lround(v)), 1, 127);
    };

    std::uniform_int_distribution<int> baseVel(std::max(1, style.hatVelocityMin), std::max(style.hatVelocityMin + 1, style.hatVelocityMax));
    std::uniform_int_distribution<int> accentVel(asVel(spec.accentVelocityMin), asVel(spec.accentVelocityMax + 4.0f));
    int previousAccentTick = -HiResTiming::kTicksPerBar4_4;

    for (auto& h : hats)
    {
        if (h.velocity <= 0)
            h.velocity = baseVel(rng);

        if (h.entity == HatEntity::Fill)
            h.velocity = std::max(1, h.velocity - 8);
        else if (h.entity == HatEntity::Roll)
            h.velocity = std::max(1, h.velocity - 4);
        else if (h.entity == HatEntity::Accent)
            h.velocity = accentVel(rng);

        if (h.accent)
        {
            h.velocity = std::min(127, h.velocity + 8);
            if (std::abs(h.tick - previousAccentTick) <= HiResTiming::kTicks1_8)
                h.velocity = std::max(1, h.velocity - 10);
            previousAccentTick = h.tick;
        }
        h.velocity = std::clamp(h.velocity, 1, 127);
    }

    (void)segments;
    (void)style;
    (void)bars;
}

void DrillHatGenerator::applyMicroTiming(std::vector<HatEvent>& hats,
                                         const std::vector<RollSegmentState>& segments,
                                         const DrillStyleProfile& style,
                                         const MotionProfile& motion,
                                         const DrillHatBaseSpec& spec,
                                         const std::vector<BarContext>& contexts,
                                         int bars,
                                         std::mt19937& rng) const
{
    const int combinedMin = static_cast<int>(std::lround(spec.microTimingMin));
    const int combinedMax = static_cast<int>(std::lround(spec.microTimingMax));
    const double motionScale = std::clamp(0.48 + (motion.motionWeight - 0.65) * 0.58
                                          + (motion.mode == MotionMode::High ? 0.22 : (motion.mode == MotionMode::Low ? -0.08 : 0.06)),
                                          0.3,
                                          1.25);
    const int swingTicks = static_cast<int>(std::lround((style.swingPercent - 50.0f) * 1.2f * motionScale));

    for (auto& h : hats)
    {
        const int bar = std::clamp(barFromTick(h.tick), 0, std::max(0, bars - 1));
        const auto& context = contexts[static_cast<size_t>(bar)];
        const int step32 = stepInBar32FromTick(h.tick);
        const bool strongAnchor = h.entity == HatEntity::Backbone && (step32 % 8) == 0;

        if (strongAnchor)
        {
            h.microOffset = 0;
            continue;
        }

        double entityScale = 0.34;
        switch (h.entity)
        {
            case HatEntity::Backbone: entityScale = 0.34; break;
            case HatEntity::Fill: entityScale = 0.62; break;
            case HatEntity::Roll: entityScale = 0.84; break;
            case HatEntity::Accent: entityScale = 0.5; break;
        }

        const double roleScale = context.role == BarRole::Ending ? 1.15
            : (context.role == BarRole::Transition ? 1.08 : 1.0);
        const int scaledMin = std::min(-1, static_cast<int>(std::floor(static_cast<double>(combinedMin) * motionScale * entityScale * roleScale)));
        const int scaledMax = std::max(1, static_cast<int>(std::ceil(static_cast<double>(combinedMax) * motionScale * entityScale * roleScale)));
        std::uniform_int_distribution<int> jitter(scaledMin, scaledMax);

        int offset = jitter(rng);
        if ((step32 % 2) == 1)
            offset += swingTicks;

        if (h.entity == HatEntity::Roll)
            offset += ((step32 % 2) == 0 ? -1 : 1) * static_cast<int>(std::lround(1.0 + motionScale * 2.0));
        else if (h.entity == HatEntity::Accent && (step32 % 4) == 2)
            offset += static_cast<int>(std::lround(1.0 + motionScale));

        const int nextSnare = nearestForward(h.tick, context.snareTargetTicks);
        if (nextSnare >= 0 && (nextSnare - h.tick) <= HiResTiming::kTicks1_16 && h.entity != HatEntity::Backbone)
            offset -= static_cast<int>(std::lround(1.0 + motionScale * 2.0));

        if (offset == 0 && motion.mode != MotionMode::Low && h.entity != HatEntity::Backbone)
            offset = (step32 % 2) == 0 ? -1 : 1;

        h.microOffset = std::clamp(offset,
                                   std::min(combinedMin, -HiResTiming::kTicks1_32 / 3),
                                   std::max(combinedMax, HiResTiming::kTicks1_32 / 3));
    }

    (void)segments;
}

void DrillHatGenerator::renderMidi(TrackState& track,
                                   const std::vector<HatEvent>& hats,
                                   int pitch,
                                   int bars) const
{
    std::vector<HatEvent> sorted = hats;
    std::sort(sorted.begin(), sorted.end(), [](const HatEvent& a, const HatEvent& b)
    {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        if (a.accent != b.accent)
            return a.accent;
        return a.velocity > b.velocity;
    });

    sorted.erase(std::unique(sorted.begin(), sorted.end(), [](const HatEvent& a, const HatEvent& b)
    {
        return std::abs(a.tick - b.tick) <= (HiResTiming::kTicks1_64 / 2);
    }), sorted.end());

    for (const auto& h : sorted)
        HiResTiming::addNoteAtTick(track, pitch, h.tick + h.microOffset, h.velocity, false, bars);
}

void DrillHatGenerator::generate(TrackState& track,
                                 const GeneratorParams& params,
                                 const DrillStyleProfile& style,
                                 const StyleInfluenceState& styleInfluence,
                                 const std::vector<DrillPhraseRole>& phrase,
                                 const DrillGrooveBlueprint* blueprint,
                                 const TrackState* snareTrack,
                                 const TrackState* clapTrack,
                                 const TrackState* kickTrack,
                                 std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;
    const int bars = std::max(1, params.bars);

    const auto spec = getDrillHatBaseSpec(style.substyle);
    const double activity = hiHatActivityWeight(styleInfluence);
    const auto snareTargets = collectSnareTargets(snareTrack, clapTrack, bars);
    const auto kickTicks = collectTicks(kickTrack);
    const ReferenceHatSkeleton* referenceSkeleton = hasReferenceHatSkeleton(styleInfluence)
        ? &styleInfluence.referenceHatSkeleton
        : nullptr;
    const ReferenceHatCorpus* referenceCorpus = hasReferenceHatCorpus(styleInfluence)
        ? &styleInfluence.referenceHatCorpus
        : nullptr;

    std::vector<ReferenceHatFeel> referenceFeels(static_cast<size_t>(bars));
    ReferenceHatFeel aggregateFeel;
    int aggregateFeelCount = 0;
    if (referenceCorpus != nullptr)
    {
        for (int bar = 0; bar < bars; ++bar)
        {
            referenceFeels[static_cast<size_t>(bar)] = buildReferenceHatFeel(referenceCorpus, bar);
            const auto& feel = referenceFeels[static_cast<size_t>(bar)];
            if (!feel.available)
                continue;

            aggregateFeel.available = true;
            aggregateFeel.averageNotesPerBar += feel.averageNotesPerBar;
            aggregateFeel.motionRatio += feel.motionRatio;
            aggregateFeel.tripletRatio += feel.tripletRatio;
            aggregateFeel.burstRatio += feel.burstRatio;
            aggregateFeel.gapRatio += feel.gapRatio;
            aggregateFeel.preSnareRatio += feel.preSnareRatio;
            ++aggregateFeelCount;
        }
    }

    auto motion = buildMotionProfile(styleInfluence);
    if (aggregateFeelCount > 0)
    {
        const double invCount = 1.0 / static_cast<double>(aggregateFeelCount);
        aggregateFeel.averageNotesPerBar *= invCount;
        aggregateFeel.motionRatio *= invCount;
        aggregateFeel.tripletRatio *= invCount;
        aggregateFeel.burstRatio *= invCount;
        aggregateFeel.gapRatio *= invCount;
        aggregateFeel.preSnareRatio *= invCount;
        motion = blendMotionProfileFromReference(motion, aggregateFeel, style.substyle);
    }

    auto barContext = buildBarContext(phrase, snareTargets, kickTicks, referenceSkeleton, blueprint, style, bars);
    auto features = buildGridFeatures(barContext, referenceSkeleton, blueprint, bars);
    for (int bar = 0; bar < bars; ++bar)
        applyReferenceHatFeel(features, bar, referenceFeels[static_cast<size_t>(bar)]);

    std::vector<HatEvent> backboneHats;
    backboneHats.reserve(static_cast<size_t>(bars * 10));
    std::vector<HatEvent> hats;
    hats.reserve(static_cast<size_t>(bars * 18));

    // Backbone stays 1/16-grid anchored. Motion is layered on top without overwriting it.
    generateBackboneHats(backboneHats, barContext, features, style, spec, referenceSkeleton, blueprint, bars, rng);
    hats = backboneHats;
    generateFillHats(hats, barContext, features, style, motion, spec, blueprint, bars, rng);
    const auto segments = createRollSegments(hats, barContext, features, style, motion, spec, blueprint, bars, rng);
    renderRollSegments(hats, segments, style, bars);
    assignHatAccents(hats, segments, barContext, features, style, motion, blueprint, bars, rng);
    applyHardCapsCleanup(hats, segments, barContext, style, motion, blueprint, bars);
    applyVelocityShape(hats, segments, style, spec, bars, rng);
    applyMicroTiming(hats, segments, style, motion, spec, barContext, bars, rng);

    {
        std::vector<HatEvent> prioritized = hats;
        std::stable_sort(prioritized.begin(), prioritized.end(), [](const HatEvent& left, const HatEvent& right)
        {
            const int leftBar = barFromTick(left.tick);
            const int rightBar = barFromTick(right.tick);
            if (leftBar != rightBar)
                return leftBar < rightBar;

            const int leftPriority = hatEntityPriority(left.entity);
            const int rightPriority = hatEntityPriority(right.entity);
            if (leftPriority != rightPriority)
                return leftPriority > rightPriority;

            return left.tick < right.tick;
        });

        std::vector<HatEvent> filtered;
        filtered.reserve(prioritized.size());
        std::vector<int> nonBackboneCounts(static_cast<size_t>(bars), 0);
        for (const auto& hat : prioritized)
        {
            if (hat.entity == HatEntity::Backbone)
            {
                filtered.push_back(hat);
                continue;
            }

            const int bar = std::clamp(barFromTick(hat.tick), 0, bars - 1);
            int nonBackboneCap = motion.maxMotionEventsPerBar;
            nonBackboneCap = std::max(nonBackboneCap, std::clamp(static_cast<int>(std::round(1.0 + activity * 1.6)), 1, 5));
            if (barContext[static_cast<size_t>(bar)].role != BarRole::Response)
                nonBackboneCap += 1;
            const double variationBias = ((bar % 2) == 0 ? -1.0 : 1.0) * motion.densitySwing;
            nonBackboneCap += static_cast<int>(std::round(variationBias));
            if (motion.sparseMode && barContext[static_cast<size_t>(bar)].role == BarRole::Response)
                nonBackboneCap = std::max(1, nonBackboneCap - 1);
            if (motion.expressiveMode && barContext[static_cast<size_t>(bar)].isTransitionBar)
                nonBackboneCap += 1;
            nonBackboneCap = std::clamp(nonBackboneCap, 1, 6);
            if (nonBackboneCounts[static_cast<size_t>(bar)] >= nonBackboneCap)
                continue;

            ++nonBackboneCounts[static_cast<size_t>(bar)];
            filtered.push_back(hat);
        }

        hats = std::move(filtered);
        std::sort(hats.begin(), hats.end(), [](const HatEvent& left, const HatEvent& right)
        {
            if (left.tick != right.tick)
                return left.tick < right.tick;
            return hatEntityPriority(left.entity) > hatEntityPriority(right.entity);
        });
    }

    if (activity < 0.98)
    {
        std::uniform_real_distribution<double> trimChance(0.0, 1.0);
        hats.erase(std::remove_if(hats.begin(), hats.end(), [&](const HatEvent& event)
        {
            if (event.entity == HatEntity::Backbone)
                return false;

            double keep = std::clamp(activity * (event.entity == HatEntity::Accent ? 1.02 : 0.9), 0.25, 1.0);
            if (event.entity == HatEntity::Roll)
                keep = std::clamp(keep * motion.burstWeight * 0.88, 0.25, 1.0);
            else if (event.entity == HatEntity::Accent)
                keep = std::clamp(keep * motion.accentPatternWeight * 0.92, 0.25, 1.0);
            return trimChance(rng) > keep;
        }), hats.end());
    }

    quantizeGeneratedHatGrid(hats, segments);
    ensureBarStartAnchors(hats, bars);

    renderMidi(track, hats, pitch, bars);
}
} // namespace bbg