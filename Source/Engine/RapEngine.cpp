#include "RapEngine.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "../Core/TrackSemantics.h"
#include "../Core/TrackRegistry.h"
#include "HiResTiming.h"
#include "PatternPerformanceTransformEngine.h"
#include "Rap/LofiRapStyleSpec.h"
#include "Rap/RapStyleSpec.h"
#include "StyleInfluence.h"
#include "StyleDefaults.h"
#include "../Analysis/StepHintWeighter.h"

namespace bbg
{
namespace
{
TrackState* findTrack(PatternProject& project, TrackType type)
{
    for (auto& t : project.tracks)
    {
        if (t.type == type)
            return &t;
    }

    return nullptr;
}

void applyResolvedStyleInfluence(PatternProject& project)
{
    juce::String applyError;
    RapStyleInfluence::apply(project, &applyError);
    juce::ignoreUnused(applyError);
}

float laneActivityWeight(const PatternProject& project, TrackType type)
{
    return std::clamp(laneBiasFor(project.styleInfluence, type).activityWeight, 0.55f, 1.5f);
}

float laneBalanceWeight(const PatternProject& project, TrackType type)
{
    return std::clamp(laneBiasFor(project.styleInfluence, type).balanceWeight, 0.55f, 1.5f);
}

float supportAccentWeight(const PatternProject& project)
{
    return std::clamp(project.styleInfluence.supportAccentWeight, 0.65f, 1.5f);
}

float lowEndCouplingWeight(const PatternProject& project)
{
    return std::clamp(project.styleInfluence.lowEndCouplingWeight, 0.65f, 1.6f);
}

struct ReferenceRapKickFeel
{
    bool available = false;
    float density = 0.0f;
    float anchorRatio = 0.0f;
    float supportRatio = 0.0f;
    float tailRatio = 0.0f;
    std::array<float, 16> presence {};
};

struct ReferenceRapHatFeel
{
    bool available = false;
    float density = 0.0f;
    float supportRatio = 0.0f;
    float gapRatio = 0.0f;
};

ReferenceRapKickFeel buildReferenceRapKickFeel(const PatternProject& project, int bar)
{
    ReferenceRapKickFeel feel;
    const auto& corpus = project.styleInfluence.referenceKickCorpus;
    if (!corpus.available || corpus.variants.empty())
        return feel;

    int contributingBars = 0;
    float totalNotes = 0.0f;
    float anchors = 0.0f;
    float supports = 0.0f;
    float tails = 0.0f;

    for (const auto& variant : corpus.variants)
    {
        if (!variant.available || variant.barPatterns.empty())
            continue;

        const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barPatterns.size()));
        const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
        if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barPatterns.size()))
            continue;

        const auto& pattern = variant.barPatterns[static_cast<size_t>(normalizedBar)];
        ++contributingBars;
        totalNotes += static_cast<float>(pattern.notes.size());
        for (const auto& note : pattern.notes)
        {
            const int step = std::clamp(note.step16, 0, 15);
            feel.presence[static_cast<size_t>(step)] += 1.0f;
            if (step == 0 || step == 8)
                anchors += 1.0f;
            else if (step >= 14)
                tails += 1.0f;
            else
                supports += 1.0f;
        }
    }

    if (contributingBars <= 0)
        return feel;

    feel.available = true;
    const float invBars = 1.0f / static_cast<float>(contributingBars);
    for (auto& value : feel.presence)
        value *= invBars;
    feel.density = std::clamp((totalNotes * invBars) / 4.0f, 0.0f, 1.0f);
    feel.anchorRatio = totalNotes > 0.0f ? anchors / totalNotes : 0.0f;
    feel.supportRatio = totalNotes > 0.0f ? supports / totalNotes : 0.0f;
    feel.tailRatio = totalNotes > 0.0f ? tails / totalNotes : 0.0f;
    return feel;
}

ReferenceRapHatFeel buildReferenceRapHatFeel(const PatternProject& project, int bar)
{
    ReferenceRapHatFeel feel;
    int contributingBars = 0;
    float totalNotes = 0.0f;
    float supportNotes = 0.0f;
    float emptySlots = 0.0f;

    const auto& corpus = project.styleInfluence.referenceHatCorpus;
    if (corpus.available && !corpus.variants.empty())
    {
        for (const auto& variant : corpus.variants)
        {
            if (!variant.available || variant.barMaps.empty())
                continue;

            const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barMaps.size()));
            const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
            if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barMaps.size()))
                continue;

            const auto& barMap = variant.barMaps[static_cast<size_t>(normalizedBar)];
            ++contributingBars;
            std::array<bool, 8> occupiedSlots {};
            for (const auto& note : barMap.notes)
            {
                const int step32 = std::clamp(HiResTiming::quantizeTicks(note.tickInBar, HiResTiming::kTicks1_32) / HiResTiming::kTicks1_32,
                                              0,
                                              31);
                const int step16 = std::clamp(step32 / 2, 0, 15);
                occupiedSlots[static_cast<size_t>(step16 / 2)] = true;
                totalNotes += 1.0f;
                if ((step16 % 2) == 1)
                    supportNotes += 1.0f;
            }
            for (size_t i = 0; i < occupiedSlots.size(); ++i)
                if (!occupiedSlots[i])
                    emptySlots += 1.0f;
        }
    }

    if (contributingBars <= 0)
        return feel;

    feel.available = true;
    feel.density = totalNotes / static_cast<float>(contributingBars);
    feel.supportRatio = totalNotes > 0.0f ? supportNotes / totalNotes : 0.0f;
    feel.gapRatio = emptySlots / (static_cast<float>(contributingBars) * 8.0f);
    return feel;
}

bool containsStep(const std::vector<NoteEvent>& notes, int step)
{
    return std::any_of(notes.begin(), notes.end(), [step](const NoteEvent& n)
    {
        return n.step == step;
    });
}

const StepFeature* featureAtStep(const AudioFeatureMap& map, int step)
{
    if (map.steps.empty() || map.stepsPerBar <= 0)
        return nullptr;

    const int normalized = std::max(0, step);
    const size_t idx = static_cast<size_t>(normalized) % map.steps.size();
    return &map.steps[idx];
}

bool isRapHatFamily(TrackType type)
{
    const auto role = roleFromTrackType(type);
    const auto family = familyFromTrackType(type);

    return role == TrackRole::HiHat
        || role == TrackRole::OpenHat
        || role == TrackRole::Perc
        || family == TrackFamily::CymbalFamily;
}

void applySampleAwareRapFlavor(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks)
{
    const auto& ctx = project.sampleContext;
    if (!ctx.enabled || ctx.featureMap.steps.empty())
        return;

    const float react = std::clamp(ctx.reactivity, 0.0f, 1.0f);
    const float contrast = std::clamp(ctx.supportVsContrast, 0.0f, 1.0f);
    const float support = 1.0f - contrast;

    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0)
            continue;

        for (auto& note : track.notes)
        {
            const auto* f = featureAtStep(ctx.featureMap, note.step);
            if (f == nullptr)
                continue;

            float guide = 0.45f * f->accent + 0.35f * f->onset + 0.20f * f->energy;
            if (track.type == TrackType::Kick || track.type == TrackType::GhostKick || track.type == TrackType::Sub808)
                guide = 0.42f * f->low + 0.33f * f->accent + 0.25f * f->onset;
            else if (isRapHatFamily(track.type))
                guide = 0.44f * f->high + 0.34f * f->energy + 0.22f * f->onset;

            const float gain = std::clamp(1.0f
                                              + 0.24f * react * support * (guide - 0.5f)
                                              + 0.14f * react * contrast * (0.5f - guide),
                                          0.70f,
                                          1.40f);
            note.velocity = std::clamp(static_cast<int>(static_cast<float>(note.velocity) * gain), 1, 127);
        }

        if (isRapHatFamily(track.type) && support * react > 0.40f)
        {
            track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
            {
                const auto* f = featureAtStep(ctx.featureMap, note.step);
                if (f == nullptr)
                    return false;

                const int phase = (note.step + note.velocity + static_cast<int>(track.type)) % 8;
                return phase == 0 && f->high > 0.80f && f->onset < 0.28f && !f->isStrongBeat;
            }), track.notes.end());
        }
    }

    StepHintWeighter::applyToProject(project, mutableTracks);
}

bool isAnchorStep(TrackType type, int stepInBar)
{
    if (type == TrackType::Kick)
        return stepInBar == 0 || stepInBar == 8;
    if (type == TrackType::Snare)
        return stepInBar == 4 || stepInBar == 12;
    return false;
}

bool isLofiRapStyle(const RapStyleProfile& style)
{
    return style.substyle == RapSubstyle::LofiRap;
}

bool isEastCoastRapStyle(const RapStyleProfile& style)
{
    return style.substyle == RapSubstyle::EastCoast;
}

bool isWestCoastRapStyle(const RapStyleProfile& style)
{
    return style.substyle == RapSubstyle::WestCoast;
}

bool isDirtySouthRapStyle(const RapStyleProfile& style)
{
    return style.substyle == RapSubstyle::DirtySouthClassic;
}

bool isGermanStreetRapStyle(const RapStyleProfile& style)
{
    return style.substyle == RapSubstyle::GermanStreetRap;
}

std::vector<RapPhraseRole> createPhrasePlanForStyle(int bars,
                                                    float density,
                                                    std::mt19937& rng,
                                                    const RapStyleProfile& style)
{
    if (!isLofiRapStyle(style))
        return RapPhrasePlanner::createPlan(bars, density, rng);

    const int count = std::max(1, bars);
    std::vector<RapPhraseRole> plan(static_cast<size_t>(count), RapPhraseRole::Base);
    if (count == 1)
        return plan;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const float subtleVarChance = std::clamp(0.16f + density * 0.20f, 0.14f, 0.34f);

    for (int bar = 1; bar < count; ++bar)
    {
        const bool secondBarOfPair = (bar % 2) == 1;
        if (secondBarOfPair)
            plan[static_cast<size_t>(bar)] = chance(rng) < subtleVarChance ? RapPhraseRole::Variation : RapPhraseRole::Base;
    }

    if (count >= 4)
        plan[static_cast<size_t>(count - 1)] = RapPhraseRole::Ending;

    return plan;
}

int sampleVelocityInRange(std::mt19937& rng, int minV, int maxV)
{
    std::uniform_int_distribution<int> dist(minV, maxV);
    return dist(rng);
}

int normalizedStepInBar(int step)
{
    return ((step % 16) + 16) % 16;
}

int deterministicRapDrift(int seed, int bar, int step, int salt, int spread)
{
    if (spread <= 0)
        return 0;

    unsigned int x = static_cast<unsigned int>(seed * 2654435761u
                                               + bar * 2246822519u
                                               + step * 3266489917u
                                               + salt * 668265263u);
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;

    const int width = spread * 2 + 1;
    return static_cast<int>(x % static_cast<unsigned int>(width)) - spread;
}

float deterministicRapUnit(int seed, int bar, int step, int salt)
{
    unsigned int x = static_cast<unsigned int>(seed * 374761393u
                                               + bar * 668265263u
                                               + step * 2246822519u
                                               + salt * 3266489917u);
    x ^= x >> 13;
    x *= 1274126177u;
    x ^= x >> 16;
    return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

int eastCoastOffbeatDelayTicks(const PatternProject& project, const RapStyleProfile& style)
{
    const float requested = std::clamp(project.params.swingPercent, 51.0f, 58.0f);
    const float profiled = std::clamp(style.swingPercent, 52.0f, 57.0f);
    const float swingPoint = requested * 0.48f + profiled * 0.52f;
    const float delayedEighthTicks = 480.0f * (swingPoint / 100.0f - 0.5f);
    return std::clamp(static_cast<int>(std::round(delayedEighthTicks)), 8, 34);
}

int eastCoastControlSpread(const PatternProject& project)
{
    const float timing = std::clamp(project.params.timingAmount, 0.0f, 1.0f);
    const float human = std::clamp(project.params.humanizeAmount, 0.0f, 1.0f);
    return std::clamp(1 + static_cast<int>(std::round(timing * 3.0f + human * 5.0f)), 1, 8);
}

int eastCoastVelocityLift(const PatternProject& project, int low, int high)
{
    return std::clamp(static_cast<int>(std::round(project.params.velocityAmount * static_cast<float>(high - low))) + low, low, high);
}

int eastCoastPocketOffsetFor(TrackType type,
                             const NoteEvent& note,
                             const PatternProject& project,
                             int offbeatDelayTicks)
{
    const int step = normalizedStepInBar(note.step);
    const int bar = std::max(0, note.step / 16);
    const int phase = step % 4;
    const int spread = eastCoastControlSpread(project);
    const int drift = deterministicRapDrift(project.params.seed + 1103, bar, step, static_cast<int>(type) + 211, spread);
    const int tiny = deterministicRapDrift(project.params.seed + 1129, bar, step, static_cast<int>(type) + 223, std::max(1, spread / 2));

    switch (type)
    {
        case TrackType::HiHat:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + drift, 8, 42);
            if ((step % 2) == 1)
                return std::clamp(3 + drift, -8, 18);
            if (step == 4 || step == 12)
                return std::clamp(2 + tiny, -3, 9);
            return std::clamp(tiny, -5, 7);

        case TrackType::Kick:
            if (step == 0)
                return std::clamp(-2 + tiny, -9, 5);
            if (step == 8 || step == 10 || step == 14 || step == 15)
                return std::clamp(1 + drift, -8, 14);
            if (step == 3 || step == 6 || step == 7)
                return std::clamp(-4 + drift, -14, 8);
            return std::clamp(drift, -10, 12);

        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            if (!note.isGhost && (step == 4 || step == 12))
                return std::clamp((step == 12 ? 7 : 5) + tiny, 2, 15);
            if (step == 3 || step == 11 || step == 15)
                return std::clamp(-4 + drift, -14, 8);
            return std::clamp(2 + drift, -8, 14);

        case TrackType::GhostKick:
            return std::clamp(-4 + drift, -14, 8);

        case TrackType::OpenHat:
            return std::clamp(offbeatDelayTicks + drift, 10, 46);

        case TrackType::Perc:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks / 2 + drift, 2, 28);
            return std::clamp(-1 + drift, -10, 14);

        default:
            break;
    }

    return note.microOffset;
}

int westCoastOffbeatDelayTicks(const PatternProject& project, const RapStyleProfile& style)
{
    const float requested = std::clamp(project.params.swingPercent, 52.0f, 61.0f);
    const float profiled = std::clamp(style.swingPercent, 54.0f, 60.0f);
    const float swingPoint = requested * 0.46f + profiled * 0.54f;
    const float delayedEighthTicks = 480.0f * (swingPoint / 100.0f - 0.5f);
    return std::clamp(static_cast<int>(std::round(delayedEighthTicks)), 18, 58);
}

int westCoastControlSpread(const PatternProject& project)
{
    const float timing = std::clamp(project.params.timingAmount, 0.0f, 1.0f);
    const float human = std::clamp(project.params.humanizeAmount, 0.0f, 1.0f);
    return std::clamp(2 + static_cast<int>(std::round(timing * 4.0f + human * 6.0f)), 2, 10);
}

int westCoastPocketOffsetFor(TrackType type,
                             const NoteEvent& note,
                             const PatternProject& project,
                             int offbeatDelayTicks)
{
    const int step = normalizedStepInBar(note.step);
    const int bar = std::max(0, note.step / 16);
    const int phase = step % 4;
    const int spread = westCoastControlSpread(project);
    const int drift = deterministicRapDrift(project.params.seed + 1301, bar, step, static_cast<int>(type) + 241, spread);
    const int tiny = deterministicRapDrift(project.params.seed + 1327, bar, step, static_cast<int>(type) + 257, std::max(1, spread / 2));

    switch (type)
    {
        case TrackType::HiHat:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + drift, 18, 66);
            if ((step % 2) == 1)
                return std::clamp(8 + drift, -4, 28);
            if (step == 4 || step == 12)
                return std::clamp(6 + tiny, 0, 16);
            return std::clamp(2 + tiny, -5, 12);

        case TrackType::Kick:
            if (step == 0)
                return std::clamp(-1 + tiny, -8, 8);
            if (step == 5 || step == 7 || step == 13 || step == 15)
                return std::clamp(-3 + drift, -14, 10);
            if (step == 8 || step == 10)
                return std::clamp(5 + drift, -4, 20);
            return std::clamp(2 + drift, -9, 18);

        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            if (!note.isGhost && (step == 4 || step == 12))
                return std::clamp((step == 12 ? 16 : 12) + tiny, 7, 26);
            if (step == 3 || step == 11 || step == 15)
                return std::clamp(-3 + drift, -14, 10);
            return std::clamp(5 + drift, -6, 20);

        case TrackType::GhostKick:
            return std::clamp(-3 + drift, -14, 10);

        case TrackType::OpenHat:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + 4 + drift, 24, 72);
            return std::clamp(12 + drift, 0, 34);

        case TrackType::Perc:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks / 2 + 4 + drift, 8, 38);
            return std::clamp(3 + drift, -8, 20);

        case TrackType::Sub808:
            return std::clamp(1 + tiny, -6, 12);

        default:
            break;
    }

    return note.microOffset;
}

int sampleLofiVelocity(TrackType trackType, const NoteEvent& note, std::mt19937& rng)
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const float roll = chance(rng);

    if (trackType == TrackType::HiHat)
    {
        if (note.isGhost)
            return sampleVelocityInRange(rng, 28, 52);
        if (roll < 0.60f)
            return sampleVelocityInRange(rng, 58, 72);
        if (roll < 0.85f)
            return sampleVelocityInRange(rng, 73, 82);
        return sampleVelocityInRange(rng, 83, 92);
    }

    if (trackType == TrackType::Snare || trackType == TrackType::ClapGhostSnare)
    {
        if (note.isGhost)
            return sampleVelocityInRange(rng, 28, 52);
        if (roll < 0.70f)
            return sampleVelocityInRange(rng, 78, 96);
        if (roll < 0.90f)
            return sampleVelocityInRange(rng, 68, 77);
        return sampleVelocityInRange(rng, 97, 108);
    }

    if (trackType == TrackType::Kick || trackType == TrackType::GhostKick)
    {
        if (note.isGhost || trackType == TrackType::GhostKick)
            return sampleVelocityInRange(rng, 26, 64);
        if (roll < 0.50f)
            return sampleVelocityInRange(rng, 78, 96);
        if (roll < 0.80f)
            return sampleVelocityInRange(rng, 65, 77);
        return sampleVelocityInRange(rng, 26, 64);
    }

    if (trackType == TrackType::OpenHat)
        return sampleVelocityInRange(rng, 58, 84);
    if (trackType == TrackType::Perc)
        return sampleVelocityInRange(rng, 46, 76);

    return sampleVelocityInRange(rng, 52, 86);
}

std::pair<int, int> lofiTimingWindow(TrackType trackType, const NoteEvent& note)
{
    switch (trackType)
    {
        case TrackType::HiHat:
            return note.isGhost ? std::make_pair(-12, 14) : std::make_pair(-6, 8);
        case TrackType::Snare:
            return note.isGhost ? std::make_pair(0, 10) : std::make_pair(6, 18);
        case TrackType::Kick:
            return isAnchorStep(TrackType::Kick, note.step % 16) ? std::make_pair(-10, 12) : std::make_pair(-16, 18);
        case TrackType::GhostKick:
            return std::make_pair(-16, 18);
        case TrackType::OpenHat:
            return std::make_pair(-4, 10);
        case TrackType::Perc:
            return std::make_pair(-8, 12);
        case TrackType::ClapGhostSnare:
            return note.isGhost ? std::make_pair(2, 10) : std::make_pair(6, 16);
        default:
            return std::make_pair(-6, 8);
    }
}

std::pair<int, int> rapTimingWindow(RapSubstyle substyle, TrackType trackType, const NoteEvent& note)
{
    switch (trackType)
    {
        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            switch (substyle)
            {
                case RapSubstyle::GermanStreetRap: return { -1, 5 };
                case RapSubstyle::HardcoreRap: return { -2, 6 };
                case RapSubstyle::DirtySouthClassic: return { 0, 6 };
                case RapSubstyle::EastCoast: return { 0, 8 };
                case RapSubstyle::WestCoast: return { 2, 10 };
                case RapSubstyle::RussianRap: return { 0, 10 };
                case RapSubstyle::RnBRap: return { 4, 14 };
                default: return { 0, 8 };
            }
        case TrackType::Kick:
            switch (substyle)
            {
                case RapSubstyle::GermanStreetRap: return { -4, 5 };
                case RapSubstyle::HardcoreRap: return { -4, 5 };
                case RapSubstyle::DirtySouthClassic: return { -4, 8 };
                case RapSubstyle::EastCoast: return { -6, 8 };
                case RapSubstyle::WestCoast: return { -8, 10 };
                case RapSubstyle::RussianRap: return { -6, 8 };
                case RapSubstyle::RnBRap: return { -8, 10 };
                default: return { -6, 8 };
            }
        case TrackType::HiHat:
            switch (substyle)
            {
                case RapSubstyle::GermanStreetRap: return { -2, 4 };
                case RapSubstyle::HardcoreRap: return { -3, 5 };
                case RapSubstyle::DirtySouthClassic: return { -3, 5 };
                case RapSubstyle::EastCoast: return { -4, 6 };
                case RapSubstyle::WestCoast: return { -6, 8 };
                case RapSubstyle::RussianRap: return { -4, 6 };
                case RapSubstyle::RnBRap: return { -6, 8 };
                default: return { -4, 6 };
            }
        case TrackType::OpenHat:
            return substyle == RapSubstyle::HardcoreRap ? std::make_pair(-3, 5) : std::make_pair(-4, 10);
        case TrackType::Perc:
            return substyle == RapSubstyle::HardcoreRap ? std::make_pair(-6, 8) : std::make_pair(-6, 10);
        case TrackType::GhostKick:
            return substyle == RapSubstyle::HardcoreRap ? std::make_pair(-6, 8) : std::make_pair(-10, 14);
        case TrackType::Sub808:
            return { -4, 8 };
        default:
            return note.isGhost ? std::make_pair(-8, 10) : std::make_pair(-4, 6);
    }
}

int sampleRapVelocity(RapSubstyle substyle, TrackType type, const NoteEvent& note, std::mt19937& rng)
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    auto range = [&rng](int lo, int hi)
    {
        std::uniform_int_distribution<int> d(lo, hi);
        return d(rng);
    };

    if (type == TrackType::HiHat)
    {
        if (note.isGhost)
            return range(30, 54);
        if (substyle == RapSubstyle::HardcoreRap)
            return range(54, 84);
        return range(substyle == RapSubstyle::GermanStreetRap ? 56 : 58, substyle == RapSubstyle::DirtySouthClassic ? 92 : 86);
    }

    if (type == TrackType::Snare || type == TrackType::ClapGhostSnare)
    {
        if (note.isGhost)
            return range(32, 58);
        if (substyle == RapSubstyle::HardcoreRap)
            return range(96, 122);
        if (substyle == RapSubstyle::RnBRap)
            return range(74, 102);
        if (substyle == RapSubstyle::GermanStreetRap)
            return range(88, 114);
        return range(78, 108);
    }

    if (type == TrackType::Kick || type == TrackType::GhostKick)
    {
        if (note.isGhost)
            return range(30, 64);
        if (substyle == RapSubstyle::HardcoreRap)
        {
            const float roll = chance(rng);
            if (roll < 0.62f)
                return range(88, 116);
            return range(74, 98);
        }
        const float roll = chance(rng);
        if (roll < 0.55f)
            return range(78, 98);
        return range(66, 90);
    }

    if (type == TrackType::Sub808)
    {
        if (substyle == RapSubstyle::DirtySouthClassic)
            return range(70, 104);
        if (substyle == RapSubstyle::RnBRap)
            return range(62, 90);
        return range(58, 88);
    }

    return range(48, 88);
}

juce::String roleForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick: return "core_pulse";
        case TrackType::Snare: return "backbeat";
        case TrackType::HiHat: return "carrier";
        case TrackType::OpenHat: return "accent";
        case TrackType::ClapGhostSnare: return "support";
        case TrackType::GhostKick: return "support";
        case TrackType::Ride: return "support";
        case TrackType::Cymbal: return "crash";
        case TrackType::Perc: return "texture";
        default: return "lane";
    }
}

void dedupeAndSort(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.velocity > b.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        return a.step == b.step;
    }), notes.end());
}

NoteEvent* findNoteAtStep(TrackState& track, int step)
{
    auto it = std::find_if(track.notes.begin(), track.notes.end(), [step](const NoteEvent& note)
    {
        return note.step == step;
    });
    return it != track.notes.end() ? &(*it) : nullptr;
}

void upsertEastCoastNote(TrackState& track,
                         int pitch,
                         int step,
                         int velocity,
                         int microOffset,
                         bool ghost)
{
    if (auto* existing = findNoteAtStep(track, step); existing != nullptr)
    {
        existing->pitch = pitch;
        existing->length = std::max(1, existing->length);
        existing->velocity = velocity;
        existing->microOffset = microOffset;
        existing->isGhost = ghost;
        return;
    }

    track.notes.push_back({ pitch, step, 1, velocity, microOffset, ghost });
}

int eastCoastPriority(TrackType type, const NoteEvent& note)
{
    const int step = normalizedStepInBar(note.step);
    int score = note.velocity;

    switch (type)
    {
        case TrackType::HiHat:
            if ((step % 2) == 0)
                score += 150;
            if (step == 0 || step == 4 || step == 8 || step == 12)
                score += 40;
            if (step == 14 || step == 15)
                score += 20;
            break;
        case TrackType::Kick:
            if (step == 0)
                score += 260;
            else if (step == 8 || step == 10)
                score += 160;
            else if (step == 3 || step == 6 || step == 7 || step == 14 || step == 15)
                score += 90;
            break;
        case TrackType::Snare:
            if (!note.isGhost && (step == 4 || step == 12))
                score += 280;
            else if (step == 3 || step == 11 || step == 15)
                score += 80;
            break;
        case TrackType::ClapGhostSnare:
            if (step == 12)
                score += 140;
            break;
        case TrackType::GhostKick:
        case TrackType::Perc:
            if (step == 3 || step == 7 || step == 11 || step == 14 || step == 15)
                score += 90;
            break;
        default:
            break;
    }

    return score;
}

void pruneEastCoastBarLimit(TrackState& track, int bars, int maxPerBar, int endingMaxPerBar)
{
    std::vector<NoteEvent> filtered;
    filtered.reserve(track.notes.size());

    for (int bar = 0; bar < bars; ++bar)
    {
        std::vector<NoteEvent> barNotes;
        for (const auto& note : track.notes)
            if (note.step / 16 == bar)
                barNotes.push_back(note);

        std::stable_sort(barNotes.begin(), barNotes.end(), [&track](const NoteEvent& left, const NoteEvent& right)
        {
            const int leftScore = eastCoastPriority(track.type, left);
            const int rightScore = eastCoastPriority(track.type, right);
            if (leftScore != rightScore)
                return leftScore > rightScore;
            return left.step < right.step;
        });

        const int limit = bar == bars - 1 ? endingMaxPerBar : maxPerBar;
        if (static_cast<int>(barNotes.size()) > limit)
            barNotes.resize(static_cast<size_t>(limit));

        filtered.insert(filtered.end(), barNotes.begin(), barNotes.end());
    }

    track.notes = std::move(filtered);
    dedupeAndSort(track.notes);
}

void shapeEastCoastHatCarrier(TrackState& hat,
                              const PatternProject& project,
                              const RapStyleProfile& style,
                              int offbeatDelayTicks)
{
    static constexpr std::array<std::array<int, 10>, 6> kHatMotifs {{
        {{ 0, 2, 4, 6, 8, 10, 12, 14, -1, -1 }},
        {{ 0, 2, 4, 8, 10, 12, 14, -1, -1, -1 }},
        {{ 0, 2, 4, 6, 8, 12, 14, -1, -1, -1 }},
        {{ 0, 2, 4, 6, 8, 10, 12, -1, -1, -1 }},
        {{ 0, 2, 4, 7, 8, 10, 12, 14, -1, -1 }},
        {{ 0, 2, 4, 6, 8, 10, 12, 14, 15, -1 }}
    }};

    const auto* info = TrackRegistry::find(TrackType::HiHat);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 8);

    hat.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        int motifIndex = static_cast<int>(deterministicRapUnit(project.params.seed, bar, 0, 1201) * static_cast<float>(kHatMotifs.size()))
            % static_cast<int>(kHatMotifs.size());
        if (density < 0.36f)
            motifIndex = std::min(motifIndex, 2);
        else if (density > 0.68f && bar == bars - 1)
            motifIndex = 5;

        const auto& motif = kHatMotifs[static_cast<size_t>(motifIndex)];
        for (const int stepInBar : motif)
        {
            if (stepInBar < 0)
                continue;
            if ((stepInBar % 2) == 1 && density < 0.62f)
                continue;

            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool down = stepInBar == 0 || stepInBar == 8;
            const bool backbeat = stepInBar == 4 || stepInBar == 12;
            const bool offbeat = (stepInBar % 4) == 2;
            const int dust = static_cast<int>(deterministicRapUnit(project.params.seed, bar, stepInBar, 1203) * 5.0f);
            note.velocity = std::clamp(style.hatVelocityMin + (down ? 26 : backbeat ? 21 : offbeat ? 15 : 7) + dust + velocityLift,
                                       style.hatVelocityMin,
                                       style.hatVelocityMax);
            note.microOffset = eastCoastPocketOffsetFor(TrackType::HiHat, note, project, offbeatDelayTicks);
            note.isGhost = false;
            upsertEastCoastNote(hat, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneEastCoastBarLimit(hat, bars, density > 0.68f ? 9 : 8, density > 0.55f ? 9 : 8);
}

void shapeEastCoastKickPocket(TrackState& kick,
                              const PatternProject& project,
                              const RapStyleProfile& style,
                              int offbeatDelayTicks)
{
    static constexpr std::array<std::array<int, 6>, 7> kKickMotifs {{
        {{ 0, 6, 10, -1, -1, -1 }},
        {{ 0, 3, 8, 10, -1, -1 }},
        {{ 0, 6, 8, 14, -1, -1 }},
        {{ 0, 7, 10, 14, -1, -1 }},
        {{ 0, 3, 10, 15, -1, -1 }},
        {{ 0, 8, 10, -1, -1, -1 }},
        {{ 0, 6, 10, 14, 15, -1 }}
    }};

    const auto* info = TrackRegistry::find(TrackType::Kick);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 10);

    kick.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        int motifIndex = static_cast<int>(deterministicRapUnit(project.params.seed, bar, 0, 1211) * static_cast<float>(kKickMotifs.size()))
            % static_cast<int>(kKickMotifs.size());
        if (density < 0.34f)
            motifIndex = 0;
        else if (density > 0.70f && bar == bars - 1)
            motifIndex = 6;

        const auto& motif = kKickMotifs[static_cast<size_t>(motifIndex)];
        for (const int stepInBar : motif)
        {
            if (stepInBar < 0 || stepInBar == 4 || stepInBar == 12)
                continue;
            if (stepInBar == 15 && density < 0.52f && bar != bars - 1)
                continue;

            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool root = stepInBar == 0;
            const bool anchor = stepInBar == 8 || stepInBar == 10;
            note.velocity = std::clamp(style.kickVelocityMin + (root ? 24 : anchor ? 18 : 10) + velocityLift,
                                       style.kickVelocityMin,
                                       style.kickVelocityMax);
            note.microOffset = eastCoastPocketOffsetFor(TrackType::Kick, note, project, offbeatDelayTicks);
            note.isGhost = false;
            upsertEastCoastNote(kick, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneEastCoastBarLimit(kick, bars, density > 0.68f ? 5 : 4, density > 0.50f ? 5 : 4);
}

void shapeEastCoastSnarePocket(TrackState& snare,
                               const PatternProject& project,
                               const RapStyleProfile& style,
                               int offbeatDelayTicks)
{
    juce::ignoreUnused(offbeatDelayTicks);

    const auto* info = TrackRegistry::find(TrackType::Snare);
    const int pitch = info != nullptr ? info->defaultMidiNote : 38;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 9);
    int ghostCount = 0;

    snare.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : { 4, 12 })
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.snareVelocityMin + (stepInBar == 12 ? 17 : 13) + velocityLift,
                                       style.snareVelocityMin,
                                       style.snareVelocityMax);
            note.microOffset = eastCoastPocketOffsetFor(TrackType::Snare, note, project, 0);
            note.isGhost = false;
            upsertEastCoastNote(snare, note.pitch, note.step, note.velocity, note.microOffset, false);
        }

        const float ghostPick = deterministicRapUnit(project.params.seed, bar, 0, 1221);
        const float ghostGate = std::clamp(0.10f + density * 0.16f + project.params.humanizeAmount * 0.08f, 0.08f, 0.28f);
        if (ghostPick < ghostGate || (bar == bars - 1 && ghostPick < ghostGate + 0.26f))
        {
            const int stepInBar = ghostPick < 0.34f ? 11 : (ghostPick < 0.58f ? 3 : 15);
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.ghostVelocityMin + 4 + static_cast<int>(deterministicRapUnit(project.params.seed, bar, stepInBar, 1223) * 9.0f),
                                       style.ghostVelocityMin,
                                       style.ghostVelocityMax);
            note.microOffset = eastCoastPocketOffsetFor(TrackType::Snare, note, project, 0);
            note.isGhost = true;
            upsertEastCoastNote(snare, note.pitch, note.step, note.velocity, note.microOffset, true);
            ++ghostCount;
        }
    }

    if (ghostCount == 0 && bars > 1)
    {
        NoteEvent note;
        note.pitch = pitch;
        note.step = (bars - 1) * 16 + 11;
        note.length = 1;
        note.velocity = std::clamp(style.ghostVelocityMin + 6, style.ghostVelocityMin, style.ghostVelocityMax);
        note.microOffset = eastCoastPocketOffsetFor(TrackType::Snare, note, project, 0);
        note.isGhost = true;
        upsertEastCoastNote(snare, note.pitch, note.step, note.velocity, note.microOffset, true);
    }

    pruneEastCoastBarLimit(snare, bars, 3, 3);
}

void shapeEastCoastSupportTrack(TrackState& track,
                                const PatternProject& project,
                                const RapStyleProfile& style,
                                int offbeatDelayTicks)
{
    const int bars = std::max(1, project.params.bars);

    for (auto& note : track.notes)
    {
        const int step = normalizedStepInBar(note.step);
        const int bar = std::max(0, note.step / 16);
        const bool ending = bar == bars - 1;
        note.microOffset = eastCoastPocketOffsetFor(track.type, note, project, offbeatDelayTicks);

        switch (track.type)
        {
            case TrackType::ClapGhostSnare:
                if (!ending || step != 12)
                    note.step = -1;
                else
                {
                    note.isGhost = false;
                    note.velocity = std::clamp(note.velocity - 24, style.ghostVelocityMin, std::min(style.snareVelocityMax, 82));
                }
                break;
            case TrackType::GhostKick:
                if (!ending || (step != 7 && step != 11 && step != 15))
                    note.step = -1;
                else
                    note.velocity = std::clamp(note.velocity - 16, style.ghostVelocityMin, style.ghostVelocityMax);
                break;
            case TrackType::OpenHat:
                note.step = -1;
                break;
            case TrackType::Perc:
                if (!(step == 7 || step == 11 || step == 15))
                    note.step = -1;
                else
                    note.velocity = std::clamp(note.velocity - 14, style.percVelocityMin, style.percVelocityMax);
                break;
            case TrackType::Ride:
            case TrackType::Cymbal:
            case TrackType::Sub808:
                note.step = -1;
                break;
            default:
                break;
        }
    }

    track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [](const NoteEvent& note)
    {
        return note.step < 0;
    }), track.notes.end());

    switch (track.type)
    {
        case TrackType::ClapGhostSnare: pruneEastCoastBarLimit(track, bars, 0, 1); break;
        case TrackType::GhostKick: pruneEastCoastBarLimit(track, bars, 0, 1); break;
        case TrackType::Perc: pruneEastCoastBarLimit(track, bars, 1, 2); break;
        default: dedupeAndSort(track.notes); break;
    }
}

void applyEastCoastPocketRules(PatternProject& project,
                               const RapStyleProfile& style,
                               const std::unordered_set<TrackType>& mutableTracks)
{
    const int offbeatDelayTicks = eastCoastOffbeatDelayTicks(project, style);

    if (auto* hat = findTrack(project, TrackType::HiHat);
        hat != nullptr && hat->enabled && !hat->locked && mutableTracks.count(hat->type) != 0)
    {
        shapeEastCoastHatCarrier(*hat, project, style, offbeatDelayTicks);
    }

    if (auto* kick = findTrack(project, TrackType::Kick);
        kick != nullptr && kick->enabled && !kick->locked && mutableTracks.count(kick->type) != 0)
    {
        shapeEastCoastKickPocket(*kick, project, style, offbeatDelayTicks);
    }

    if (auto* snare = findTrack(project, TrackType::Snare);
        snare != nullptr && snare->enabled && !snare->locked && mutableTracks.count(snare->type) != 0)
    {
        shapeEastCoastSnarePocket(*snare, project, style, offbeatDelayTicks);
    }

    for (auto& track : project.tracks)
    {
        if (track.locked || mutableTracks.count(track.type) == 0)
            continue;

        if (track.type == TrackType::ClapGhostSnare
            || track.type == TrackType::GhostKick
            || track.type == TrackType::OpenHat
            || track.type == TrackType::Perc
            || track.type == TrackType::Ride
            || track.type == TrackType::Cymbal
            || track.type == TrackType::Sub808)
        {
            shapeEastCoastSupportTrack(track, project, style, offbeatDelayTicks);
        }
    }
}

int westCoastPriority(TrackType type, const NoteEvent& note)
{
    const int step = normalizedStepInBar(note.step);
    int score = note.velocity;

    switch (type)
    {
        case TrackType::HiHat:
            if ((step % 2) == 0)
                score += 140;
            if (step == 0 || step == 4 || step == 8 || step == 12)
                score += 34;
            if (step == 7 || step == 15)
                score += 30;
            break;
        case TrackType::Kick:
            if (step == 0)
                score += 260;
            else if (step == 8 || step == 10)
                score += 150;
            else if (step == 5 || step == 7 || step == 13 || step == 15)
                score += 105;
            break;
        case TrackType::Snare:
            if (!note.isGhost && (step == 4 || step == 12))
                score += 270;
            else if (step == 3 || step == 11 || step == 15)
                score += 80;
            break;
        case TrackType::ClapGhostSnare:
            if (step == 4 || step == 12)
                score += 220;
            break;
        case TrackType::OpenHat:
            if (step == 6 || step == 14 || step == 15)
                score += 130;
            break;
        case TrackType::GhostKick:
        case TrackType::Perc:
            if (step == 7 || step == 11 || step == 13 || step == 15)
                score += 90;
            break;
        case TrackType::Sub808:
            if (step == 0 || step == 8 || step == 10)
                score += 180;
            break;
        default:
            break;
    }

    return score;
}

void pruneWestCoastBarLimit(TrackState& track, int bars, int maxPerBar, int endingMaxPerBar)
{
    std::vector<NoteEvent> filtered;
    filtered.reserve(track.notes.size());

    for (int bar = 0; bar < bars; ++bar)
    {
        std::vector<NoteEvent> barNotes;
        for (const auto& note : track.notes)
            if (note.step / 16 == bar)
                barNotes.push_back(note);

        std::stable_sort(barNotes.begin(), barNotes.end(), [&track](const NoteEvent& left, const NoteEvent& right)
        {
            const int leftScore = westCoastPriority(track.type, left);
            const int rightScore = westCoastPriority(track.type, right);
            if (leftScore != rightScore)
                return leftScore > rightScore;
            return left.step < right.step;
        });

        const int limit = bar == bars - 1 ? endingMaxPerBar : maxPerBar;
        if (static_cast<int>(barNotes.size()) > limit)
            barNotes.resize(static_cast<size_t>(limit));

        filtered.insert(filtered.end(), barNotes.begin(), barNotes.end());
    }

    track.notes = std::move(filtered);
    dedupeAndSort(track.notes);
}

void shapeWestCoastHatCarrier(TrackState& hat,
                              const PatternProject& project,
                              const RapStyleProfile& style,
                              int offbeatDelayTicks)
{
    static constexpr std::array<std::array<int, 11>, 7> kHatMotifs {{
        {{ 0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1 }},
        {{ 0, 2, 4, 6, 7, 8, 10, 12, 14, -1, -1 }},
        {{ 0, 2, 4, 6, 8, 10, 12, 14, 15, -1, -1 }},
        {{ 0, 2, 4, 6, 8, 10, 12, 13, 14, -1, -1 }},
        {{ 0, 2, 4, 5, 6, 8, 10, 12, 14, -1, -1 }},
        {{ 0, 2, 4, 6, 8, 10, 11, 12, 14, 15, -1 }},
        {{ 0, 2, 4, 6, 7, 8, 10, 12, 14, 15, -1 }}
    }};

    const auto* info = TrackRegistry::find(TrackType::HiHat);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 7);

    hat.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        int motifIndex = static_cast<int>(deterministicRapUnit(project.params.seed, bar, 0, 1331) * static_cast<float>(kHatMotifs.size()))
            % static_cast<int>(kHatMotifs.size());
        if (density < 0.34f)
            motifIndex = 0;
        else if (density > 0.68f && bar == bars - 1)
            motifIndex = 5 + static_cast<int>(deterministicRapUnit(project.params.seed, bar, 15, 1333) * 2.0f);

        const auto& motif = kHatMotifs[static_cast<size_t>(std::clamp(motifIndex, 0, static_cast<int>(kHatMotifs.size() - 1)))];
        for (const int stepInBar : motif)
        {
            if (stepInBar < 0)
                continue;
            if ((stepInBar % 2) == 1 && density < 0.48f)
                continue;

            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool down = stepInBar == 0 || stepInBar == 8;
            const bool backbeat = stepInBar == 4 || stepInBar == 12;
            const bool offbeat = (stepInBar % 4) == 2;
            const int dust = static_cast<int>(deterministicRapUnit(project.params.seed, bar, stepInBar, 1335) * 5.0f);
            note.velocity = std::clamp(style.hatVelocityMin + (down ? 24 : backbeat ? 19 : offbeat ? 14 : 7) + dust + velocityLift,
                                       style.hatVelocityMin,
                                       style.hatVelocityMax);
            note.microOffset = westCoastPocketOffsetFor(TrackType::HiHat, note, project, offbeatDelayTicks);
            note.isGhost = false;
            upsertEastCoastNote(hat, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneWestCoastBarLimit(hat, bars, density > 0.60f ? 9 : 8, density > 0.52f ? 10 : 9);
}

void shapeWestCoastKickPocket(TrackState& kick,
                              const PatternProject& project,
                              const RapStyleProfile& style,
                              int offbeatDelayTicks)
{
    static constexpr std::array<std::array<int, 6>, 7> kKickMotifs {{
        {{ 0, 5, 8, 13, -1, -1 }},
        {{ 0, 5, 8, 10, 15, -1 }},
        {{ 0, 7, 10, 13, -1, -1 }},
        {{ 0, 5, 8, 10, -1, -1 }},
        {{ 0, 3, 8, 13, 15, -1 }},
        {{ 0, 5, 10, 15, -1, -1 }},
        {{ 0, 5, 8, 10, 13, 15 }}
    }};

    const auto* info = TrackRegistry::find(TrackType::Kick);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 8);

    kick.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        int motifIndex = static_cast<int>(deterministicRapUnit(project.params.seed, bar, 0, 1341) * static_cast<float>(kKickMotifs.size()))
            % static_cast<int>(kKickMotifs.size());
        if (density < 0.34f)
            motifIndex = 0;
        else if (density > 0.70f && bar == bars - 1)
            motifIndex = 6;

        const auto& motif = kKickMotifs[static_cast<size_t>(motifIndex)];
        for (const int stepInBar : motif)
        {
            if (stepInBar < 0 || stepInBar == 4 || stepInBar == 12)
                continue;
            if (stepInBar == 15 && density < 0.42f && bar != bars - 1)
                continue;

            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool root = stepInBar == 0;
            const bool weight = stepInBar == 8 || stepInBar == 10;
            note.velocity = std::clamp(style.kickVelocityMin + (root ? 21 : weight ? 15 : 8) + velocityLift,
                                       style.kickVelocityMin,
                                       style.kickVelocityMax);
            note.microOffset = westCoastPocketOffsetFor(TrackType::Kick, note, project, offbeatDelayTicks);
            note.isGhost = false;
            upsertEastCoastNote(kick, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneWestCoastBarLimit(kick, bars, density > 0.64f ? 5 : 4, density > 0.48f ? 6 : 5);
}

void shapeWestCoastSnarePocket(TrackState& snare,
                               const PatternProject& project,
                               const RapStyleProfile& style,
                               int offbeatDelayTicks)
{
    juce::ignoreUnused(offbeatDelayTicks);

    const auto* info = TrackRegistry::find(TrackType::Snare);
    const int pitch = info != nullptr ? info->defaultMidiNote : 38;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 7);
    int ghostCount = 0;

    snare.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : { 4, 12 })
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.snareVelocityMin + (stepInBar == 12 ? 14 : 10) + velocityLift,
                                       style.snareVelocityMin,
                                       style.snareVelocityMax);
            note.microOffset = westCoastPocketOffsetFor(TrackType::Snare, note, project, 0);
            note.isGhost = false;
            upsertEastCoastNote(snare, note.pitch, note.step, note.velocity, note.microOffset, false);
        }

        const float ghostPick = deterministicRapUnit(project.params.seed, bar, 0, 1351);
        const float ghostGate = std::clamp(0.08f + density * 0.12f + project.params.humanizeAmount * 0.10f, 0.08f, 0.26f);
        if (ghostPick < ghostGate || (bar == bars - 1 && ghostPick < ghostGate + 0.20f))
        {
            const int stepInBar = ghostPick < 0.34f ? 11 : (ghostPick < 0.62f ? 3 : 15);
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.ghostVelocityMin + 5 + static_cast<int>(deterministicRapUnit(project.params.seed, bar, stepInBar, 1353) * 10.0f),
                                       style.ghostVelocityMin,
                                       style.ghostVelocityMax);
            note.microOffset = westCoastPocketOffsetFor(TrackType::Snare, note, project, 0);
            note.isGhost = true;
            upsertEastCoastNote(snare, note.pitch, note.step, note.velocity, note.microOffset, true);
            ++ghostCount;
        }
    }

    if (ghostCount == 0 && bars > 1)
    {
        NoteEvent note;
        note.pitch = pitch;
        note.step = (bars - 1) * 16 + 11;
        note.length = 1;
        note.velocity = std::clamp(style.ghostVelocityMin + 7, style.ghostVelocityMin, style.ghostVelocityMax);
        note.microOffset = westCoastPocketOffsetFor(TrackType::Snare, note, project, 0);
        note.isGhost = true;
        upsertEastCoastNote(snare, note.pitch, note.step, note.velocity, note.microOffset, true);
    }

    pruneWestCoastBarLimit(snare, bars, 3, 3);
}

void shapeWestCoastClapLayer(TrackState& clap,
                             const PatternProject& project,
                             const RapStyleProfile& style,
                             int offbeatDelayTicks)
{
    const auto* info = TrackRegistry::find(TrackType::ClapGhostSnare);
    const int pitch = info != nullptr ? info->defaultMidiNote : 39;
    const int bars = std::max(1, project.params.bars);

    clap.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : { 4, 12 })
        {
            if (stepInBar == 4 && deterministicRapUnit(project.params.seed, bar, stepInBar, 1361) < 0.18f)
                continue;

            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.snareVelocityMin - 18 + (stepInBar == 12 ? 7 : 4),
                                       style.ghostVelocityMin,
                                       style.snareVelocityMax - 10);
            note.microOffset = westCoastPocketOffsetFor(TrackType::ClapGhostSnare, note, project, offbeatDelayTicks) + 2;
            note.isGhost = false;
            upsertEastCoastNote(clap, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneWestCoastBarLimit(clap, bars, 2, 2);
}

void shapeWestCoastOpenHatTrack(TrackState& openHat,
                                const PatternProject& project,
                                const RapStyleProfile& style,
                                int offbeatDelayTicks)
{
    const auto* info = TrackRegistry::find(TrackType::OpenHat);
    const int pitch = info != nullptr ? info->defaultMidiNote : 46;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);

    openHat.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool ending = bar == bars - 1;
        const float gate = ending ? 0.72f : std::clamp(0.22f + density * 0.28f, 0.18f, 0.48f);
        if (deterministicRapUnit(project.params.seed, bar, 14, 1371) > gate)
            continue;

        const int stepInBar = deterministicRapUnit(project.params.seed, bar, 6, 1373) < 0.42f ? 6 : 14;
        NoteEvent note;
        note.pitch = pitch;
        note.step = bar * 16 + stepInBar;
        note.length = ending ? 2 : 1;
        note.velocity = std::clamp(style.hatVelocityMin + 20 + static_cast<int>(density * 8.0f), style.hatVelocityMin, std::min(96, style.hatVelocityMax + 8));
        note.microOffset = westCoastPocketOffsetFor(TrackType::OpenHat, note, project, offbeatDelayTicks);
        note.isGhost = false;
        upsertEastCoastNote(openHat, note.pitch, note.step, note.velocity, note.microOffset, false);

        if (ending && density > 0.55f)
        {
            NoteEvent pickup = note;
            pickup.step = bar * 16 + 15;
            pickup.velocity = std::max(style.hatVelocityMin, note.velocity - 8);
            pickup.microOffset = westCoastPocketOffsetFor(TrackType::OpenHat, pickup, project, offbeatDelayTicks);
            upsertEastCoastNote(openHat, pickup.pitch, pickup.step, pickup.velocity, pickup.microOffset, false);
        }
    }

    pruneWestCoastBarLimit(openHat, bars, 1, 2);
}

void shapeWestCoastPercTrack(TrackState& perc,
                             const PatternProject& project,
                             const RapStyleProfile& style,
                             int offbeatDelayTicks)
{
    const auto* info = TrackRegistry::find(TrackType::Perc);
    const int pitch = info != nullptr ? info->defaultMidiNote : 50;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);

    perc.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool ending = bar == bars - 1;
        const float gate = std::clamp(0.18f + density * 0.24f + (ending ? 0.18f : 0.0f), 0.14f, 0.62f);
        if (deterministicRapUnit(project.params.seed, bar, 13, 1381) > gate)
            continue;

        const int stepInBar = deterministicRapUnit(project.params.seed, bar, 5, 1383) < 0.52f ? 13 : 7;
        NoteEvent note;
        note.pitch = pitch;
        note.step = bar * 16 + stepInBar;
        note.length = 1;
        note.velocity = std::clamp(style.percVelocityMin + 9 + static_cast<int>(density * 7.0f), style.percVelocityMin, style.percVelocityMax);
        note.microOffset = westCoastPocketOffsetFor(TrackType::Perc, note, project, offbeatDelayTicks);
        note.isGhost = false;
        upsertEastCoastNote(perc, note.pitch, note.step, note.velocity, note.microOffset, false);

        if (ending && density > 0.62f)
        {
            NoteEvent pickup = note;
            pickup.step = bar * 16 + 15;
            pickup.velocity = std::max(style.percVelocityMin, note.velocity - 6);
            pickup.microOffset = westCoastPocketOffsetFor(TrackType::Perc, pickup, project, offbeatDelayTicks);
            upsertEastCoastNote(perc, pickup.pitch, pickup.step, pickup.velocity, pickup.microOffset, false);
        }
    }

    pruneWestCoastBarLimit(perc, bars, 1, 2);
}

void shapeWestCoastSubBass(TrackState& sub,
                           const PatternProject& project,
                           const RapStyleProfile& style,
                           const TrackState* kick)
{
    const int bars = std::max(1, project.params.bars);
    const int basePitch = std::clamp(36 + project.params.keyRoot, 24, 84);

    sub.notes.clear();
    if (kick == nullptr)
        return;

    for (const auto& kickNote : kick->notes)
    {
        const int stepInBar = normalizedStepInBar(kickNote.step);
        if (!(stepInBar == 0 || stepInBar == 8 || stepInBar == 10))
            continue;

        NoteEvent note;
        note.pitch = basePitch + (stepInBar == 10 ? -2 : 0);
        note.step = kickNote.step;
        note.length = stepInBar == 0 ? 3 : 2;
        note.velocity = std::clamp(style.kickVelocityMin - 16 + static_cast<int>(project.params.velocityAmount * 10.0f), 58, 94);
        note.microOffset = westCoastPocketOffsetFor(TrackType::Sub808, note, project, 0);
        note.isGhost = false;
        upsertEastCoastNote(sub, note.pitch, note.step, note.velocity, note.microOffset, false);
    }

    pruneWestCoastBarLimit(sub, bars, 2, 2);
}

void applyWestCoastPocketRules(PatternProject& project,
                               const RapStyleProfile& style,
                               const std::unordered_set<TrackType>& mutableTracks)
{
    const int offbeatDelayTicks = westCoastOffbeatDelayTicks(project, style);

    if (auto* hat = findTrack(project, TrackType::HiHat);
        hat != nullptr && hat->enabled && !hat->locked && mutableTracks.count(hat->type) != 0)
    {
        shapeWestCoastHatCarrier(*hat, project, style, offbeatDelayTicks);
    }

    if (auto* kick = findTrack(project, TrackType::Kick);
        kick != nullptr && kick->enabled && !kick->locked && mutableTracks.count(kick->type) != 0)
    {
        shapeWestCoastKickPocket(*kick, project, style, offbeatDelayTicks);
    }

    if (auto* snare = findTrack(project, TrackType::Snare);
        snare != nullptr && snare->enabled && !snare->locked && mutableTracks.count(snare->type) != 0)
    {
        shapeWestCoastSnarePocket(*snare, project, style, offbeatDelayTicks);
    }

    if (auto* clap = findTrack(project, TrackType::ClapGhostSnare);
        clap != nullptr && clap->enabled && !clap->locked && mutableTracks.count(clap->type) != 0)
    {
        shapeWestCoastClapLayer(*clap, project, style, offbeatDelayTicks);
    }

    if (auto* openHat = findTrack(project, TrackType::OpenHat);
        openHat != nullptr && openHat->enabled && !openHat->locked && mutableTracks.count(openHat->type) != 0)
    {
        shapeWestCoastOpenHatTrack(*openHat, project, style, offbeatDelayTicks);
    }

    if (auto* perc = findTrack(project, TrackType::Perc);
        perc != nullptr && perc->enabled && !perc->locked && mutableTracks.count(perc->type) != 0)
    {
        shapeWestCoastPercTrack(*perc, project, style, offbeatDelayTicks);
    }

    if (auto* sub = findTrack(project, TrackType::Sub808);
        sub != nullptr && sub->enabled && !sub->locked && mutableTracks.count(sub->type) != 0)
    {
        shapeWestCoastSubBass(*sub, project, style, findTrack(project, TrackType::Kick));
    }

    for (auto& track : project.tracks)
    {
        if (track.locked || mutableTracks.count(track.type) == 0)
            continue;

        if (track.type == TrackType::Ride || track.type == TrackType::Cymbal)
            track.notes.clear();
    }
}

int dirtySouthOffbeatDelayTicks(const PatternProject& project, const RapStyleProfile& style)
{
    const float requested = std::clamp(project.params.swingPercent, 51.0f, 59.0f);
    const float profiled = std::clamp(style.swingPercent, 53.0f, 58.0f);
    const float swingPoint = requested * 0.50f + profiled * 0.50f;
    const float delayedEighthTicks = 480.0f * (swingPoint / 100.0f - 0.5f);
    return std::clamp(static_cast<int>(std::round(delayedEighthTicks)), 14, 50);
}

int dirtySouthControlSpread(const PatternProject& project)
{
    const float timing = std::clamp(project.params.timingAmount, 0.0f, 1.0f);
    const float human = std::clamp(project.params.humanizeAmount, 0.0f, 1.0f);
    return std::clamp(2 + static_cast<int>(std::round(timing * 4.0f + human * 5.0f)), 2, 10);
}

int dirtySouthPocketOffsetFor(TrackType type,
                              const NoteEvent& note,
                              const PatternProject& project,
                              int offbeatDelayTicks)
{
    const int step = normalizedStepInBar(note.step);
    const int bar = std::max(0, note.step / 16);
    const int phase = step % 4;
    const int spread = dirtySouthControlSpread(project);
    const int drift = deterministicRapDrift(project.params.seed + 1501, bar, step, static_cast<int>(type) + 271, spread);
    const int tiny = deterministicRapDrift(project.params.seed + 1523, bar, step, static_cast<int>(type) + 283, std::max(1, spread / 2));

    switch (type)
    {
        case TrackType::HiHat:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + drift, 14, 58);
            if ((step % 2) == 1)
                return std::clamp(6 + drift, -6, 28);
            if (step == 4 || step == 12)
                return std::clamp(4 + tiny, -2, 14);
            return std::clamp(1 + tiny, -6, 10);

        case TrackType::Kick:
            if (step == 0)
                return std::clamp(-2 + tiny, -10, 6);
            if (step == 8 || step == 10)
                return std::clamp(3 + drift, -6, 18);
            if (step == 3 || step == 6 || step == 14 || step == 15)
                return std::clamp(-3 + drift, -14, 12);
            return std::clamp(drift, -10, 16);

        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            if (!note.isGhost && (step == 4 || step == 12))
                return std::clamp((step == 12 ? 15 : 11) + tiny, 6, 26);
            if (step == 3 || step == 11 || step == 15)
                return std::clamp(-2 + drift, -12, 12);
            return std::clamp(5 + drift, -6, 20);

        case TrackType::GhostKick:
            return std::clamp(-3 + drift, -14, 12);

        case TrackType::OpenHat:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + 2 + drift, 18, 64);
            return std::clamp(8 + drift, -2, 30);

        case TrackType::Perc:
            if (phase == 1 || phase == 3)
                return std::clamp(4 + drift, -8, 22);
            if (phase == 2)
                return std::clamp(offbeatDelayTicks / 2 + drift, 4, 34);
            return std::clamp(tiny, -8, 12);

        case TrackType::Cymbal:
            return std::clamp(2 + tiny, -6, 12);

        case TrackType::Sub808:
            return std::clamp(1 + tiny, -6, 12);

        default:
            break;
    }

    return note.microOffset;
}

int dirtySouthPriority(TrackType type, const NoteEvent& note)
{
    const int step = normalizedStepInBar(note.step);
    int score = note.velocity;

    switch (type)
    {
        case TrackType::HiHat:
            if ((step % 2) == 0)
                score += 145;
            if (step == 0 || step == 8)
                score += 36;
            if (step == 7 || step == 11 || step == 15)
                score += 28;
            break;
        case TrackType::Kick:
            if (step == 0)
                score += 280;
            else if (step == 8 || step == 10)
                score += 170;
            else if (step == 3 || step == 6 || step == 14 || step == 15)
                score += 110;
            break;
        case TrackType::Snare:
            if (!note.isGhost && (step == 4 || step == 12))
                score += 285;
            else if (step == 3 || step == 11 || step == 15)
                score += 75;
            break;
        case TrackType::ClapGhostSnare:
            if (step == 4 || step == 12)
                score += 245;
            break;
        case TrackType::OpenHat:
            if (step == 6 || step == 14 || step == 15)
                score += 145;
            break;
        case TrackType::Sub808:
            if (step == 0 || step == 8 || step == 10 || step == 14)
                score += 185;
            break;
        case TrackType::GhostKick:
        case TrackType::Perc:
            if (step == 5 || step == 7 || step == 11 || step == 13 || step == 15)
                score += 90;
            break;
        case TrackType::Cymbal:
            if (step == 0 || step == 15)
                score += 120;
            break;
        default:
            break;
    }

    return score;
}

void pruneDirtySouthBarLimit(TrackState& track, int bars, int maxPerBar, int endingMaxPerBar)
{
    std::vector<NoteEvent> filtered;
    filtered.reserve(track.notes.size());

    for (int bar = 0; bar < bars; ++bar)
    {
        std::vector<NoteEvent> barNotes;
        for (const auto& note : track.notes)
            if (note.step / 16 == bar)
                barNotes.push_back(note);

        std::stable_sort(barNotes.begin(), barNotes.end(), [&track](const NoteEvent& left, const NoteEvent& right)
        {
            const int leftScore = dirtySouthPriority(track.type, left);
            const int rightScore = dirtySouthPriority(track.type, right);
            if (leftScore != rightScore)
                return leftScore > rightScore;
            return left.step < right.step;
        });

        const int limit = bar == bars - 1 ? endingMaxPerBar : maxPerBar;
        if (static_cast<int>(barNotes.size()) > limit)
            barNotes.resize(static_cast<size_t>(limit));

        filtered.insert(filtered.end(), barNotes.begin(), barNotes.end());
    }

    track.notes = std::move(filtered);
    dedupeAndSort(track.notes);
}

void shapeDirtySouthHatCarrier(TrackState& hat,
                               const PatternProject& project,
                               const RapStyleProfile& style,
                               int offbeatDelayTicks)
{
    static constexpr std::array<std::array<int, 11>, 8> kHatMotifs {{
        {{ 0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1 }},
        {{ 0, 2, 3, 4, 6, 8, 10, 12, 14, -1, -1 }},
        {{ 0, 2, 4, 6, 7, 8, 10, 12, 14, -1, -1 }},
        {{ 0, 2, 4, 6, 8, 9, 10, 12, 14, -1, -1 }},
        {{ 0, 2, 4, 5, 6, 8, 10, 12, 14, 15, -1 }},
        {{ 0, 2, 4, 6, 8, 10, 11, 12, 14, -1, -1 }},
        {{ 0, 2, 4, 6, 7, 8, 10, 12, 13, 14, -1 }},
        {{ 0, 2, 4, 6, 8, 10, 12, 13, 14, 15, -1 }}
    }};

    const auto* info = TrackRegistry::find(TrackType::HiHat);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 8);

    hat.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        int motifIndex = static_cast<int>(deterministicRapUnit(project.params.seed, bar, 0, 1531) * static_cast<float>(kHatMotifs.size()))
            % static_cast<int>(kHatMotifs.size());
        if (density < 0.34f)
            motifIndex = 0;
        else if (density > 0.68f && bar == bars - 1)
            motifIndex = 7;

        const auto& motif = kHatMotifs[static_cast<size_t>(motifIndex)];
        for (const int stepInBar : motif)
        {
            if (stepInBar < 0)
                continue;
            if ((stepInBar % 2) == 1 && density < 0.46f)
                continue;

            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool down = stepInBar == 0 || stepInBar == 8;
            const bool backbeat = stepInBar == 4 || stepInBar == 12;
            const bool offbeat = (stepInBar % 4) == 2;
            const int dust = static_cast<int>(deterministicRapUnit(project.params.seed, bar, stepInBar, 1533) * 5.0f);
            note.velocity = std::clamp(style.hatVelocityMin + (down ? 25 : backbeat ? 18 : offbeat ? 14 : 6) + dust + velocityLift,
                                       style.hatVelocityMin,
                                       style.hatVelocityMax);
            note.microOffset = dirtySouthPocketOffsetFor(TrackType::HiHat, note, project, offbeatDelayTicks);
            note.isGhost = (stepInBar % 2) == 1;
            if (note.isGhost)
                note.velocity = std::min(note.velocity, style.hatVelocityMin + 18);
            upsertEastCoastNote(hat, note.pitch, note.step, note.velocity, note.microOffset, note.isGhost);
        }
    }

    pruneDirtySouthBarLimit(hat, bars, density > 0.58f ? 10 : 9, density > 0.50f ? 10 : 9);
}

void shapeDirtySouthKickPocket(TrackState& kick,
                               const PatternProject& project,
                               const RapStyleProfile& style,
                               int offbeatDelayTicks)
{
    static constexpr std::array<std::array<int, 7>, 8> kKickMotifs {{
        {{ 0, 6, 8, 10, -1, -1, -1 }},
        {{ 0, 3, 8, 10, 14, -1, -1 }},
        {{ 0, 6, 10, 14, -1, -1, -1 }},
        {{ 0, 3, 6, 8, 14, -1, -1 }},
        {{ 0, 8, 10, 11, 15, -1, -1 }},
        {{ 0, 6, 8, 10, 14, 15, -1 }},
        {{ 0, 3, 6, 10, 14, 15, -1 }},
        {{ 0, 6, 8, 10, 11, 14, 15 }}
    }};

    const auto* info = TrackRegistry::find(TrackType::Kick);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 10);

    kick.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        int motifIndex = static_cast<int>(deterministicRapUnit(project.params.seed, bar, 0, 1541) * static_cast<float>(kKickMotifs.size()))
            % static_cast<int>(kKickMotifs.size());
        if (density < 0.34f)
            motifIndex = 0;
        else if (density > 0.70f && bar == bars - 1)
            motifIndex = 7;

        const auto& motif = kKickMotifs[static_cast<size_t>(motifIndex)];
        for (const int stepInBar : motif)
        {
            if (stepInBar < 0 || stepInBar == 4 || stepInBar == 12)
                continue;
            if (stepInBar == 15 && density < 0.44f && bar != bars - 1)
                continue;

            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool root = stepInBar == 0;
            const bool weight = stepInBar == 8 || stepInBar == 10;
            const bool pickup = stepInBar == 14 || stepInBar == 15;
            note.velocity = std::clamp(style.kickVelocityMin + (root ? 24 : weight ? 18 : pickup ? 13 : 9) + velocityLift,
                                       style.kickVelocityMin,
                                       style.kickVelocityMax);
            note.microOffset = dirtySouthPocketOffsetFor(TrackType::Kick, note, project, offbeatDelayTicks);
            note.isGhost = false;
            upsertEastCoastNote(kick, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneDirtySouthBarLimit(kick, bars, density > 0.62f ? 5 : 4, density > 0.50f ? 6 : 5);
}

void shapeDirtySouthSnarePocket(TrackState& snare,
                                const PatternProject& project,
                                const RapStyleProfile& style)
{
    const auto* info = TrackRegistry::find(TrackType::Snare);
    const int pitch = info != nullptr ? info->defaultMidiNote : 38;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 8);
    int ghostCount = 0;

    snare.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : { 4, 12 })
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.snareVelocityMin + (stepInBar == 12 ? 14 : 10) + velocityLift,
                                       style.snareVelocityMin,
                                       style.snareVelocityMax);
            note.microOffset = dirtySouthPocketOffsetFor(TrackType::Snare, note, project, 0);
            note.isGhost = false;
            upsertEastCoastNote(snare, note.pitch, note.step, note.velocity, note.microOffset, false);
        }

        const float ghostPick = deterministicRapUnit(project.params.seed, bar, 0, 1551);
        const float ghostGate = std::clamp(0.05f + density * 0.08f + project.params.humanizeAmount * 0.06f, 0.04f, 0.18f);
        if (ghostPick < ghostGate || (bar == bars - 1 && ghostPick < ghostGate + 0.16f))
        {
            const int stepInBar = ghostPick < 0.42f ? 11 : 3;
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.ghostVelocityMin + 5 + static_cast<int>(deterministicRapUnit(project.params.seed, bar, stepInBar, 1553) * 9.0f),
                                       style.ghostVelocityMin,
                                       style.ghostVelocityMax);
            note.microOffset = dirtySouthPocketOffsetFor(TrackType::Snare, note, project, 0);
            note.isGhost = true;
            upsertEastCoastNote(snare, note.pitch, note.step, note.velocity, note.microOffset, true);
            ++ghostCount;
        }
    }

    if (ghostCount == 0 && bars > 1)
    {
        NoteEvent note;
        note.pitch = pitch;
        note.step = (bars - 1) * 16 + 11;
        note.length = 1;
        note.velocity = std::clamp(style.ghostVelocityMin + 6, style.ghostVelocityMin, style.ghostVelocityMax);
        note.microOffset = dirtySouthPocketOffsetFor(TrackType::Snare, note, project, 0);
        note.isGhost = true;
        upsertEastCoastNote(snare, note.pitch, note.step, note.velocity, note.microOffset, true);
    }

    pruneDirtySouthBarLimit(snare, bars, 3, 3);
}

void shapeDirtySouthClapLayer(TrackState& clap,
                              const PatternProject& project,
                              const RapStyleProfile& style,
                              int offbeatDelayTicks)
{
    const auto* info = TrackRegistry::find(TrackType::ClapGhostSnare);
    const int pitch = info != nullptr ? info->defaultMidiNote : 39;
    const int bars = std::max(1, project.params.bars);

    clap.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : { 4, 12 })
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.snareVelocityMin - 12 + (stepInBar == 12 ? 7 : 4),
                                       style.ghostVelocityMin,
                                       style.snareVelocityMax - 6);
            note.microOffset = dirtySouthPocketOffsetFor(TrackType::ClapGhostSnare, note, project, offbeatDelayTicks) + 2;
            note.isGhost = false;
            upsertEastCoastNote(clap, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneDirtySouthBarLimit(clap, bars, 2, 2);
}

void shapeDirtySouthOpenHatTrack(TrackState& openHat,
                                 const PatternProject& project,
                                 const RapStyleProfile& style,
                                 int offbeatDelayTicks)
{
    const auto* info = TrackRegistry::find(TrackType::OpenHat);
    const int pitch = info != nullptr ? info->defaultMidiNote : 46;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);

    openHat.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool ending = bar == bars - 1;
        const float gate = ending ? 0.82f : std::clamp(0.28f + density * 0.30f, 0.22f, 0.56f);
        if (deterministicRapUnit(project.params.seed, bar, 14, 1561) > gate)
            continue;

        const int stepInBar = deterministicRapUnit(project.params.seed, bar, 6, 1563) < 0.48f ? 6 : 14;
        NoteEvent note;
        note.pitch = pitch;
        note.step = bar * 16 + stepInBar;
        note.length = ending ? 2 : 1;
        note.velocity = std::clamp(style.hatVelocityMin + 22 + static_cast<int>(density * 9.0f),
                                   style.hatVelocityMin,
                                   std::min(104, style.hatVelocityMax + 10));
        note.microOffset = dirtySouthPocketOffsetFor(TrackType::OpenHat, note, project, offbeatDelayTicks);
        note.isGhost = false;
        upsertEastCoastNote(openHat, note.pitch, note.step, note.velocity, note.microOffset, false);

        if (ending && density > 0.54f)
        {
            NoteEvent pickup = note;
            pickup.step = bar * 16 + 15;
            pickup.velocity = std::max(style.hatVelocityMin, note.velocity - 9);
            pickup.microOffset = dirtySouthPocketOffsetFor(TrackType::OpenHat, pickup, project, offbeatDelayTicks);
            upsertEastCoastNote(openHat, pickup.pitch, pickup.step, pickup.velocity, pickup.microOffset, false);
        }
    }

    pruneDirtySouthBarLimit(openHat, bars, 1, 2);
}

void shapeDirtySouthPercTrack(TrackState& perc,
                              const PatternProject& project,
                              const RapStyleProfile& style,
                              int offbeatDelayTicks)
{
    const auto* info = TrackRegistry::find(TrackType::Perc);
    const int pitch = info != nullptr ? info->defaultMidiNote : 50;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);

    perc.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool ending = bar == bars - 1;
        const float gate = std::clamp(0.22f + density * 0.22f + (ending ? 0.18f : 0.0f), 0.16f, 0.62f);
        if (deterministicRapUnit(project.params.seed, bar, 13, 1571) > gate)
            continue;

        const int stepInBar = deterministicRapUnit(project.params.seed, bar, 5, 1573) < 0.45f ? 5 : 13;
        NoteEvent note;
        note.pitch = pitch;
        note.step = bar * 16 + stepInBar;
        note.length = 1;
        note.velocity = std::clamp(style.percVelocityMin + 10 + static_cast<int>(density * 7.0f), style.percVelocityMin, style.percVelocityMax);
        note.microOffset = dirtySouthPocketOffsetFor(TrackType::Perc, note, project, offbeatDelayTicks);
        note.isGhost = false;
        upsertEastCoastNote(perc, note.pitch, note.step, note.velocity, note.microOffset, false);

        if (ending && density > 0.62f)
        {
            NoteEvent pickup = note;
            pickup.step = bar * 16 + 15;
            pickup.velocity = std::max(style.percVelocityMin, note.velocity - 6);
            pickup.microOffset = dirtySouthPocketOffsetFor(TrackType::Perc, pickup, project, offbeatDelayTicks);
            upsertEastCoastNote(perc, pickup.pitch, pickup.step, pickup.velocity, pickup.microOffset, false);
        }
    }

    pruneDirtySouthBarLimit(perc, bars, 1, 2);
}

void shapeDirtySouthCymbalTrack(TrackState& cymbal,
                                const PatternProject& project,
                                const RapStyleProfile& style)
{
    const auto* info = TrackRegistry::find(TrackType::Cymbal);
    const int pitch = info != nullptr ? info->defaultMidiNote : 49;
    const int bars = std::max(1, project.params.bars);

    cymbal.notes.clear();
    if (bars <= 1)
        return;

    const bool firstHit = deterministicRapUnit(project.params.seed, 0, 0, 1581) < 0.22f;
    const bool endingHit = deterministicRapUnit(project.params.seed, bars - 1, 15, 1583) < 0.44f;

    if (firstHit)
    {
        NoteEvent note { pitch, 0, 2, std::clamp(style.snareVelocityMin - 18, 58, 96), 0, false };
        note.microOffset = dirtySouthPocketOffsetFor(TrackType::Cymbal, note, project, 0);
        upsertEastCoastNote(cymbal, note.pitch, note.step, note.velocity, note.microOffset, false);
    }

    if (endingHit)
    {
        NoteEvent note { pitch, (bars - 1) * 16 + 15, 2, std::clamp(style.snareVelocityMin - 14, 62, 100), 0, false };
        note.microOffset = dirtySouthPocketOffsetFor(TrackType::Cymbal, note, project, 0);
        upsertEastCoastNote(cymbal, note.pitch, note.step, note.velocity, note.microOffset, false);
    }

    pruneDirtySouthBarLimit(cymbal, bars, 1, 1);
}

void shapeDirtySouthSubBass(TrackState& sub,
                            const PatternProject& project,
                            const RapStyleProfile& style,
                            const TrackState* kick)
{
    const int bars = std::max(1, project.params.bars);
    const int basePitch = std::clamp(36 + project.params.keyRoot, 24, 84);

    sub.notes.clear();
    if (kick == nullptr)
        return;

    for (const auto& kickNote : kick->notes)
    {
        const int stepInBar = normalizedStepInBar(kickNote.step);
        if (!(stepInBar == 0 || stepInBar == 8 || stepInBar == 10 || stepInBar == 14 || stepInBar == 15))
            continue;

        NoteEvent note;
        note.pitch = basePitch;
        if (stepInBar == 10)
            note.pitch = std::clamp(basePitch - 2, 24, 84);
        else if (stepInBar == 14 || stepInBar == 15)
            note.pitch = std::clamp(basePitch + 3, 24, 88);
        note.step = kickNote.step;
        note.length = stepInBar == 0 ? 4 : (stepInBar == 8 ? 3 : 2);
        note.velocity = std::clamp(style.kickVelocityMin - 12 + static_cast<int>(project.params.velocityAmount * 12.0f), 62, 104);
        note.microOffset = dirtySouthPocketOffsetFor(TrackType::Sub808, note, project, 0);
        note.isGhost = false;
        upsertEastCoastNote(sub, note.pitch, note.step, note.velocity, note.microOffset, false);
    }

    pruneDirtySouthBarLimit(sub, bars, 3, 3);
}

void applyDirtySouthPocketRules(PatternProject& project,
                                const RapStyleProfile& style,
                                const std::unordered_set<TrackType>& mutableTracks)
{
    const int offbeatDelayTicks = dirtySouthOffbeatDelayTicks(project, style);

    if (auto* hat = findTrack(project, TrackType::HiHat);
        hat != nullptr && hat->enabled && !hat->locked && mutableTracks.count(hat->type) != 0)
    {
        shapeDirtySouthHatCarrier(*hat, project, style, offbeatDelayTicks);
    }

    if (auto* kick = findTrack(project, TrackType::Kick);
        kick != nullptr && kick->enabled && !kick->locked && mutableTracks.count(kick->type) != 0)
    {
        shapeDirtySouthKickPocket(*kick, project, style, offbeatDelayTicks);
    }

    if (auto* snare = findTrack(project, TrackType::Snare);
        snare != nullptr && snare->enabled && !snare->locked && mutableTracks.count(snare->type) != 0)
    {
        shapeDirtySouthSnarePocket(*snare, project, style);
    }

    if (auto* clap = findTrack(project, TrackType::ClapGhostSnare);
        clap != nullptr && clap->enabled && !clap->locked && mutableTracks.count(clap->type) != 0)
    {
        shapeDirtySouthClapLayer(*clap, project, style, offbeatDelayTicks);
    }

    if (auto* openHat = findTrack(project, TrackType::OpenHat);
        openHat != nullptr && openHat->enabled && !openHat->locked && mutableTracks.count(openHat->type) != 0)
    {
        shapeDirtySouthOpenHatTrack(*openHat, project, style, offbeatDelayTicks);
    }

    if (auto* perc = findTrack(project, TrackType::Perc);
        perc != nullptr && perc->enabled && !perc->locked && mutableTracks.count(perc->type) != 0)
    {
        shapeDirtySouthPercTrack(*perc, project, style, offbeatDelayTicks);
    }

    if (auto* cymbal = findTrack(project, TrackType::Cymbal);
        cymbal != nullptr && cymbal->enabled && !cymbal->locked && mutableTracks.count(cymbal->type) != 0)
    {
        shapeDirtySouthCymbalTrack(*cymbal, project, style);
    }

    for (auto& track : project.tracks)
    {
        if (track.locked || mutableTracks.count(track.type) == 0)
            continue;

        if (track.type == TrackType::Ride || track.type == TrackType::HatFX)
            track.notes.clear();
        if (track.type == TrackType::Sub808)
        {
            track.enabled = false;
            track.notes.clear();
            track.sub808Notes.clear();
            track.baseNotes.clear();
            track.baseSub808Notes.clear();
        }
        if (track.type == TrackType::GhostKick)
            pruneDirtySouthBarLimit(track, std::max(1, project.params.bars), 1, 1);
    }
}

int germanStreetOffbeatDelayTicks(const PatternProject& project, const RapStyleProfile& style)
{
    const float requested = std::clamp(project.params.swingPercent, 50.0f, 56.0f);
    const float profiled = std::clamp(style.swingPercent, 50.5f, 54.0f);
    const float swingPoint = requested * 0.58f + profiled * 0.42f;
    const float delayedEighthTicks = 480.0f * (swingPoint / 100.0f - 0.5f);
    return std::clamp(static_cast<int>(std::round(delayedEighthTicks)), 3, 24);
}

int germanStreetControlSpread(const PatternProject& project)
{
    const float timing = std::clamp(project.params.timingAmount, 0.0f, 1.0f);
    const float human = std::clamp(project.params.humanizeAmount, 0.0f, 1.0f);
    return std::clamp(1 + static_cast<int>(std::round(timing * 3.0f + human * 4.0f)), 1, 7);
}

int germanStreetPocketOffsetFor(TrackType type,
                                const NoteEvent& note,
                                const PatternProject& project,
                                int offbeatDelayTicks)
{
    const int step = normalizedStepInBar(note.step);
    const int bar = std::max(0, note.step / 16);
    const int phase = step % 4;
    const int spread = germanStreetControlSpread(project);
    const int drift = deterministicRapDrift(project.params.seed + 1701, bar, step, static_cast<int>(type) + 281, spread);
    const int tiny = deterministicRapDrift(project.params.seed + 1723, bar, step, static_cast<int>(type) + 293, std::max(1, spread / 2));

    switch (type)
    {
        case TrackType::HiHat:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + drift, 3, 30);
            if ((step % 2) == 1)
                return std::clamp(1 + drift, -6, 14);
            if (step == 4 || step == 12)
                return std::clamp(1 + tiny, -3, 7);
            return std::clamp(tiny - 1, -5, 6);

        case TrackType::Kick:
            if (step == 0)
                return std::clamp(-3 + tiny, -10, 4);
            if (step == 3 || step == 6 || step == 14 || step == 15)
                return std::clamp(-5 + drift, -16, 8);
            if (step == 8 || step == 10)
                return std::clamp(-1 + drift, -10, 10);
            return std::clamp(-2 + drift, -12, 10);

        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            if (!note.isGhost && (step == 4 || step == 12))
                return std::clamp((step == 12 ? 5 : 3) + tiny, -1, 10);
            if (step == 3 || step == 11 || step == 15)
                return std::clamp(-4 + drift, -14, 8);
            return std::clamp(1 + drift, -8, 10);

        case TrackType::GhostKick:
            return std::clamp(-5 + drift, -16, 8);

        case TrackType::OpenHat:
            return std::clamp(offbeatDelayTicks + drift, 4, 32);

        case TrackType::Perc:
            return std::clamp(-3 + drift, -12, 10);

        default:
            break;
    }

    return note.microOffset;
}

int germanStreetPriority(TrackType type, const NoteEvent& note)
{
    const int step = normalizedStepInBar(note.step);
    int score = note.velocity;

    switch (type)
    {
        case TrackType::HiHat:
            if ((step % 2) == 0)
                score += 150;
            if (step == 0 || step == 4 || step == 8 || step == 12)
                score += 32;
            if (step == 2 || step == 6 || step == 10 || step == 14)
                score += 18;
            break;
        case TrackType::Kick:
            if (step == 0)
                score += 270;
            else if (step == 8 || step == 10)
                score += 160;
            else if (step == 6 || step == 14 || step == 15)
                score += 110;
            else if (step == 3)
                score += 75;
            break;
        case TrackType::Snare:
            if (!note.isGhost && (step == 4 || step == 12))
                score += 290;
            else if (step == 3 || step == 11)
                score += 60;
            break;
        case TrackType::ClapGhostSnare:
            if (step == 12)
                score += 210;
            break;
        case TrackType::OpenHat:
            if (step == 14 || step == 15)
                score += 120;
            break;
        case TrackType::GhostKick:
        case TrackType::Perc:
            if (step == 7 || step == 13 || step == 15)
                score += 80;
            break;
        default:
            break;
    }

    return score;
}

void pruneGermanStreetBarLimit(TrackState& track, int bars, int maxPerBar, int endingMaxPerBar)
{
    std::vector<NoteEvent> filtered;
    filtered.reserve(track.notes.size());

    for (int bar = 0; bar < bars; ++bar)
    {
        std::vector<NoteEvent> barNotes;
        for (const auto& note : track.notes)
            if (note.step / 16 == bar)
                barNotes.push_back(note);

        std::stable_sort(barNotes.begin(), barNotes.end(), [&track](const NoteEvent& left, const NoteEvent& right)
        {
            const int leftScore = germanStreetPriority(track.type, left);
            const int rightScore = germanStreetPriority(track.type, right);
            if (leftScore != rightScore)
                return leftScore > rightScore;
            return left.step < right.step;
        });

        const int limit = bar == bars - 1 ? endingMaxPerBar : maxPerBar;
        if (static_cast<int>(barNotes.size()) > limit)
            barNotes.resize(static_cast<size_t>(limit));

        filtered.insert(filtered.end(), barNotes.begin(), barNotes.end());
    }

    track.notes = std::move(filtered);
    dedupeAndSort(track.notes);
}

void shapeGermanStreetHatCarrier(TrackState& hat,
                                 const PatternProject& project,
                                 const RapStyleProfile& style,
                                 int offbeatDelayTicks)
{
    static constexpr std::array<std::array<int, 10>, 6> kHatMotifs {{
        {{ 0, 2, 4, 6, 8, 10, 12, 14, -1, -1 }},
        {{ 0, 2, 4, 6, 8, 12, 14, -1, -1, -1 }},
        {{ 0, 2, 4, 8, 10, 12, 14, -1, -1, -1 }},
        {{ 0, 2, 4, 6, 8, 10, 12, -1, -1, -1 }},
        {{ 0, 2, 4, 6, 8, 10, 12, 14, 15, -1 }},
        {{ 0, 2, 4, 6, 8, 10, 11, 12, 14, -1 }}
    }};

    const auto* info = TrackRegistry::find(TrackType::HiHat);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 7);

    hat.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto referenceFeel = buildReferenceRapHatFeel(project, bar);
        int motifIndex = static_cast<int>(deterministicRapUnit(project.params.seed, bar, 0, 1711) * static_cast<float>(kHatMotifs.size()))
            % static_cast<int>(kHatMotifs.size());
        if (density < 0.34f || (referenceFeel.available && referenceFeel.gapRatio > 0.55f))
            motifIndex = std::min(motifIndex, 3);
        if (density > 0.68f && bar == bars - 1)
            motifIndex = 4;

        const auto& motif = kHatMotifs[static_cast<size_t>(motifIndex)];
        for (const int stepInBar : motif)
        {
            if (stepInBar < 0)
                continue;
            if ((stepInBar % 2) == 1 && !(bar == bars - 1 || density > 0.62f))
                continue;

            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool down = stepInBar == 0 || stepInBar == 8;
            const bool backbeat = stepInBar == 4 || stepInBar == 12;
            const bool offbeat = (stepInBar % 4) == 2;
            const int dust = static_cast<int>(deterministicRapUnit(project.params.seed, bar, stepInBar, 1713) * 4.0f);
            note.velocity = std::clamp(style.hatVelocityMin + (down ? 20 : backbeat ? 16 : offbeat ? 11 : 5) + dust + velocityLift,
                                       style.hatVelocityMin,
                                       std::min(86, style.hatVelocityMax));
            note.microOffset = germanStreetPocketOffsetFor(TrackType::HiHat, note, project, offbeatDelayTicks);
            note.isGhost = false;
            upsertEastCoastNote(hat, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneGermanStreetBarLimit(hat, bars, 8, density > 0.60f ? 9 : 8);
}

void shapeGermanStreetKickPocket(TrackState& kick,
                                 const PatternProject& project,
                                 const RapStyleProfile& style,
                                 int offbeatDelayTicks)
{
    static constexpr std::array<std::array<int, 6>, 7> kKickMotifs {{
        {{ 0, 8, 10, -1, -1, -1 }},
        {{ 0, 6, 10, 14, -1, -1 }},
        {{ 0, 3, 8, 14, -1, -1 }},
        {{ 0, 6, 8, 10, -1, -1 }},
        {{ 0, 10, 14, -1, -1, -1 }},
        {{ 0, 8, 14, -1, -1, -1 }},
        {{ 0, 6, 8, 10, 14, -1 }}
    }};

    const auto* info = TrackRegistry::find(TrackType::Kick);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 10);

    kick.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto referenceFeel = buildReferenceRapKickFeel(project, bar);
        int motifIndex = static_cast<int>(deterministicRapUnit(project.params.seed, bar, 0, 1721) * static_cast<float>(kKickMotifs.size()))
            % static_cast<int>(kKickMotifs.size());
        if (density < 0.32f)
            motifIndex = deterministicRapUnit(project.params.seed, bar, 1, 1722) < 0.5f ? 0 : 5;
        else if (referenceFeel.available && referenceFeel.supportRatio > 0.55f)
            motifIndex = std::max(1, motifIndex);
        if (density > 0.70f && bar == bars - 1)
            motifIndex = 6;

        const auto& motif = kKickMotifs[static_cast<size_t>(motifIndex)];
        for (const int stepInBar : motif)
        {
            if (stepInBar < 0 || stepInBar == 4 || stepInBar == 12)
                continue;
            if (stepInBar == 15 && density < 0.62f && bar != bars - 1)
                continue;

            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool root = stepInBar == 0;
            const bool anchor = stepInBar == 8 || stepInBar == 10;
            note.velocity = std::clamp(style.kickVelocityMin + (root ? 24 : anchor ? 16 : 9) + velocityLift,
                                       style.kickVelocityMin,
                                       style.kickVelocityMax);
            note.microOffset = germanStreetPocketOffsetFor(TrackType::Kick, note, project, offbeatDelayTicks);
            note.isGhost = false;
            upsertEastCoastNote(kick, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneGermanStreetBarLimit(kick, bars, 4, density > 0.62f ? 5 : 4);
}

void shapeGermanStreetSnarePocket(TrackState& snare,
                                  const PatternProject& project,
                                  const RapStyleProfile& style,
                                  int offbeatDelayTicks)
{
    juce::ignoreUnused(offbeatDelayTicks);

    const auto* info = TrackRegistry::find(TrackType::Snare);
    const int pitch = info != nullptr ? info->defaultMidiNote : 38;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const int velocityLift = eastCoastVelocityLift(project, 0, 9);

    snare.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : { 4, 12 })
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.snareVelocityMin + (stepInBar == 12 ? 9 : 6) + velocityLift,
                                       style.snareVelocityMin,
                                       style.snareVelocityMax);
            note.microOffset = germanStreetPocketOffsetFor(TrackType::Snare, note, project, 0);
            note.isGhost = false;
            upsertEastCoastNote(snare, note.pitch, note.step, note.velocity, note.microOffset, false);
        }

        const float ghostPick = deterministicRapUnit(project.params.seed, bar, 0, 1731);
        float ghostGate = std::clamp(0.015f + density * 0.035f + project.params.humanizeAmount * 0.025f, 0.01f, 0.075f);
        if (bar == bars - 1)
            ghostGate += 0.14f;

        if (ghostPick < ghostGate)
        {
            const int stepInBar = ghostPick < 0.48f ? 11 : 3;
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.ghostVelocityMin + 4 + static_cast<int>(deterministicRapUnit(project.params.seed, bar, stepInBar, 1733) * 8.0f),
                                       style.ghostVelocityMin,
                                       style.ghostVelocityMax);
            note.microOffset = germanStreetPocketOffsetFor(TrackType::Snare, note, project, 0);
            note.isGhost = true;
            upsertEastCoastNote(snare, note.pitch, note.step, note.velocity, note.microOffset, true);
        }
    }

    pruneGermanStreetBarLimit(snare, bars, 3, 3);
}

void shapeGermanStreetClapLayer(TrackState& clap,
                                const PatternProject& project,
                                const RapStyleProfile& style,
                                int offbeatDelayTicks)
{
    const auto* info = TrackRegistry::find(TrackType::ClapGhostSnare);
    const int pitch = info != nullptr ? info->defaultMidiNote : 39;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);

    clap.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool ending = bar == bars - 1;
        const float gate = std::clamp(0.36f + density * 0.18f + (ending ? 0.16f : 0.0f), 0.28f, 0.68f);
        if (deterministicRapUnit(project.params.seed, bar, 12, 1741) > gate)
            continue;

        NoteEvent note;
        note.pitch = pitch;
        note.step = bar * 16 + 12;
        note.length = 1;
        note.velocity = std::clamp(style.snareVelocityMin - 18 + static_cast<int>(project.params.velocityAmount * 8.0f),
                                   style.ghostVelocityMin,
                                   std::min(98, style.snareVelocityMax));
        note.microOffset = germanStreetPocketOffsetFor(TrackType::ClapGhostSnare, note, project, offbeatDelayTicks) + 1;
        note.isGhost = false;
        upsertEastCoastNote(clap, note.pitch, note.step, note.velocity, note.microOffset, false);
    }

    pruneGermanStreetBarLimit(clap, bars, 1, 1);
}

void shapeGermanStreetOpenHatTrack(TrackState& openHat,
                                   const PatternProject& project,
                                   const RapStyleProfile& style,
                                   int offbeatDelayTicks)
{
    const auto* info = TrackRegistry::find(TrackType::OpenHat);
    const int pitch = info != nullptr ? info->defaultMidiNote : 46;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);

    openHat.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool ending = bar == bars - 1;
        const float gate = ending ? std::clamp(0.30f + density * 0.16f, 0.28f, 0.50f)
                                  : std::clamp(0.015f + density * 0.025f, 0.01f, 0.05f);
        if (deterministicRapUnit(project.params.seed, bar, 14, 1751) > gate)
            continue;

        NoteEvent note;
        note.pitch = pitch;
        note.step = bar * 16 + (deterministicRapUnit(project.params.seed, bar, 15, 1753) < 0.82f ? 14 : 15);
        note.length = ending ? 2 : 1;
        note.velocity = std::clamp(style.hatVelocityMin + 18 + static_cast<int>(density * 6.0f),
                                   style.hatVelocityMin,
                                   92);
        note.microOffset = germanStreetPocketOffsetFor(TrackType::OpenHat, note, project, offbeatDelayTicks);
        note.isGhost = false;
        upsertEastCoastNote(openHat, note.pitch, note.step, note.velocity, note.microOffset, false);
    }

    pruneGermanStreetBarLimit(openHat, bars, 1, 1);
}

void shapeGermanStreetPercTrack(TrackState& perc,
                                const PatternProject& project,
                                const RapStyleProfile& style,
                                int offbeatDelayTicks)
{
    const auto* info = TrackRegistry::find(TrackType::Perc);
    const int pitch = info != nullptr ? info->defaultMidiNote : 50;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);

    perc.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool ending = bar == bars - 1;
        const float gate = ending ? std::clamp(0.12f + density * 0.12f, 0.10f, 0.28f)
                                  : std::clamp(0.01f + density * 0.025f, 0.01f, 0.04f);
        if (deterministicRapUnit(project.params.seed, bar, 13, 1761) > gate)
            continue;

        NoteEvent note;
        note.pitch = pitch;
        note.step = bar * 16 + (deterministicRapUnit(project.params.seed, bar, 7, 1763) < 0.55f ? 7 : 13);
        note.length = 1;
        note.velocity = std::clamp(style.percVelocityMin + 8 + static_cast<int>(density * 5.0f), style.percVelocityMin, style.percVelocityMax);
        note.microOffset = germanStreetPocketOffsetFor(TrackType::Perc, note, project, offbeatDelayTicks);
        note.isGhost = false;
        upsertEastCoastNote(perc, note.pitch, note.step, note.velocity, note.microOffset, false);
    }

    pruneGermanStreetBarLimit(perc, bars, 1, 1);
}

void shapeGermanStreetGhostKickTrack(TrackState& ghostKick,
                                     const PatternProject& project,
                                     const RapStyleProfile& style,
                                     int offbeatDelayTicks)
{
    juce::ignoreUnused(offbeatDelayTicks);

    const auto* info = TrackRegistry::find(TrackType::GhostKick);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;
    const int bars = std::max(1, project.params.bars);
    const float density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);

    ghostKick.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool ending = bar == bars - 1;
        const float gate = ending ? std::clamp(0.18f + density * 0.12f, 0.16f, 0.34f)
                                  : std::clamp(0.01f + density * 0.02f, 0.01f, 0.035f);
        if (deterministicRapUnit(project.params.seed, bar, 15, 1771) > gate)
            continue;

        NoteEvent note;
        note.pitch = pitch;
        note.step = bar * 16 + (ending ? 15 : 7);
        note.length = 1;
        note.velocity = std::clamp(style.ghostVelocityMin + 8 + static_cast<int>(density * 7.0f), style.ghostVelocityMin, style.ghostVelocityMax);
        note.microOffset = germanStreetPocketOffsetFor(TrackType::GhostKick, note, project, 0);
        note.isGhost = true;
        upsertEastCoastNote(ghostKick, note.pitch, note.step, note.velocity, note.microOffset, true);
    }

    pruneGermanStreetBarLimit(ghostKick, bars, 1, 1);
}

void applyGermanStreetPocketRules(PatternProject& project,
                                  const RapStyleProfile& style,
                                  const std::unordered_set<TrackType>& mutableTracks)
{
    const int offbeatDelayTicks = germanStreetOffbeatDelayTicks(project, style);

    if (auto* hat = findTrack(project, TrackType::HiHat);
        hat != nullptr && hat->enabled && !hat->locked && mutableTracks.count(hat->type) != 0)
    {
        shapeGermanStreetHatCarrier(*hat, project, style, offbeatDelayTicks);
    }

    if (auto* kick = findTrack(project, TrackType::Kick);
        kick != nullptr && kick->enabled && !kick->locked && mutableTracks.count(kick->type) != 0)
    {
        shapeGermanStreetKickPocket(*kick, project, style, offbeatDelayTicks);
    }

    if (auto* snare = findTrack(project, TrackType::Snare);
        snare != nullptr && snare->enabled && !snare->locked && mutableTracks.count(snare->type) != 0)
    {
        shapeGermanStreetSnarePocket(*snare, project, style, offbeatDelayTicks);
    }

    if (auto* clap = findTrack(project, TrackType::ClapGhostSnare);
        clap != nullptr && clap->enabled && !clap->locked && mutableTracks.count(clap->type) != 0)
    {
        shapeGermanStreetClapLayer(*clap, project, style, offbeatDelayTicks);
    }

    if (auto* openHat = findTrack(project, TrackType::OpenHat);
        openHat != nullptr && openHat->enabled && !openHat->locked && mutableTracks.count(openHat->type) != 0)
    {
        shapeGermanStreetOpenHatTrack(*openHat, project, style, offbeatDelayTicks);
    }

    if (auto* perc = findTrack(project, TrackType::Perc);
        perc != nullptr && perc->enabled && !perc->locked && mutableTracks.count(perc->type) != 0)
    {
        shapeGermanStreetPercTrack(*perc, project, style, offbeatDelayTicks);
    }

    if (auto* ghostKick = findTrack(project, TrackType::GhostKick);
        ghostKick != nullptr && ghostKick->enabled && !ghostKick->locked && mutableTracks.count(ghostKick->type) != 0)
    {
        shapeGermanStreetGhostKickTrack(*ghostKick, project, style, offbeatDelayTicks);
    }

    for (auto& track : project.tracks)
    {
        if (track.locked || mutableTracks.count(track.type) == 0)
            continue;

        if (track.type == TrackType::Ride || track.type == TrackType::Cymbal || track.type == TrackType::HatFX)
            track.notes.clear();
        if (track.type == TrackType::Sub808)
        {
            track.enabled = false;
            track.notes.clear();
            track.sub808Notes.clear();
            track.baseNotes.clear();
            track.baseSub808Notes.clear();
        }
    }
}
} // namespace

RapEngine::RapEngine() = default;

void RapEngine::generate(PatternProject& project)
{
    applyResolvedStyleInfluence(project);
    const auto& style = getRapProfile(project.params.rapSubstyle);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.generationCounter * 19 + 101));

    const auto phrasePlan = createPhrasePlanForStyle(std::max(1, project.params.bars), project.params.densityAmount, rng, style);

    std::unordered_set<TrackType> mutableTracks;
    for (auto& track : project.tracks)
    {
        if (track.locked || !track.enabled)
            continue;

        regenerateTrackInternal(project, track, style, phrasePlan, rng);
        track.templateId += 1;
        track.variationId = 0;
        track.mutationDepth = 0.0f;
        track.subProfile = style.name;
        if (track.laneRole.isEmpty())
            track.laneRole = roleForTrack(track.type);
        mutableTracks.insert(track.type);
    }

    generateDependentTracks(project, style, phrasePlan, rng, mutableTracks);
    postProcess(project, style, rng, mutableTracks);
    validatePattern(project, mutableTracks);
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void RapEngine::regenerateTrack(PatternProject& project, TrackType trackType)
{
    applyResolvedStyleInfluence(project);
    regenerateTrackVariation(project, trackType);
}

void RapEngine::generateTrackNew(PatternProject& project, TrackType trackType)
{
    applyResolvedStyleInfluence(project);
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked)
        return;

    const auto& style = getRapProfile(project.params.rapSubstyle);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.generationCounter * 31 + static_cast<int>(trackType) * 17 + 77));

    const auto phrasePlan = createPhrasePlanForStyle(std::max(1, project.params.bars), project.params.densityAmount, rng, style);
    regenerateTrackInternal(project, *track, style, phrasePlan, rng);

    std::unordered_set<TrackType> mutableTracks { trackType };
    generateDependentTracks(project, style, phrasePlan, rng, mutableTracks);
    postProcess(project, style, rng, mutableTracks);
    validatePattern(project, mutableTracks);
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void RapEngine::regenerateTrackVariation(PatternProject& project, TrackType trackType)
{
    auto* target = findTrack(project, trackType);
    if (target == nullptr || target->locked || !target->enabled)
        return;

    const auto before = target->notes;
    generateTrackNew(project, trackType);

    target = findTrack(project, trackType);
    if (target == nullptr)
        return;

    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Rap, project.params.rapSubstyle);
    const auto& laneDefaults = getLaneStyleDefaults(styleDefaults, trackType);

    if (!before.empty() && !target->notes.empty())
    {
        const float keepScale = std::clamp(laneDefaults.rgVariationIntensity, 0.5f, 1.35f);
        const size_t keep = std::min(static_cast<size_t>(before.size() * 0.35f * keepScale), target->notes.size());
        for (size_t i = 0; i < keep; ++i)
            target->notes[i] = before[i];
    }

    dedupeAndSort(target->notes);
    target->variationId += 1;
    target->mutationDepth = std::clamp(target->mutationDepth + 0.08f, 0.0f, 1.0f);

    PatternPerformanceTransformEngine::captureBasePatterns(project, { trackType });
}

void RapEngine::mutatePattern(PatternProject& project)
{
    applyResolvedStyleInfluence(project);
    std::vector<TrackType> candidates;
    for (const auto& track : project.tracks)
    {
        if (!track.locked && track.enabled)
            candidates.push_back(track.type);
    }

    if (candidates.empty())
        return;

    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.mutationCounter * 211 + 13));
    std::shuffle(candidates.begin(), candidates.end(), rng);

    const auto& style = getRapProfile(project.params.rapSubstyle);
    const bool isLofi = isLofiRapStyle(style);
    const int mutateCount = isLofi
        ? std::max(1, static_cast<int>(candidates.size() / 4))
        : std::max(1, static_cast<int>(candidates.size() / 3));
    for (int i = 0; i < mutateCount && i < static_cast<int>(candidates.size()); ++i)
        mutateTrack(project, candidates[static_cast<size_t>(i)]);

    project.mutationCounter += 1;
}

void RapEngine::mutateTrack(PatternProject& project, TrackType trackType)
{
    applyResolvedStyleInfluence(project);
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked || !track->enabled)
        return;

    if (track->notes.empty())
    {
        generateTrackNew(project, trackType);
        return;
    }

    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.mutationCounter * 149 + static_cast<int>(trackType) * 59));
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Rap, project.params.rapSubstyle);
    const auto& laneDefaults = getLaneStyleDefaults(styleDefaults, trackType);
    const auto& style = getRapProfile(project.params.rapSubstyle);
    const bool isLofi = isLofiRapStyle(style);
    float mutationIntensity = std::clamp(laneDefaults.mutationIntensity, 0.55f, 1.4f);
    if (isLofi)
        mutationIntensity = std::clamp(mutationIntensity * 0.58f, 0.28f, 0.76f);

    if (chance(rng) < (0.5f * mutationIntensity))
    {
        std::vector<size_t> removable;
        for (size_t i = 0; i < track->notes.size(); ++i)
        {
            if (!isAnchorStep(trackType, track->notes[i].step % 16))
                removable.push_back(i);
        }

        if (!removable.empty())
        {
            std::uniform_int_distribution<size_t> pick(0, removable.size() - 1);
            track->notes.erase(track->notes.begin() + static_cast<long long>(removable[pick(rng)]));
        }
    }

    if (chance(rng) < (0.62f * mutationIntensity) && !track->notes.empty())
    {
        std::uniform_int_distribution<size_t> pick(0, track->notes.size() - 1);
        auto& n = track->notes[pick(rng)];
        if (!isAnchorStep(trackType, n.step % 16))
        {
            std::uniform_int_distribution<int> shift(isLofi ? -1 : -1, isLofi ? 1 : 1);
            n.step = std::clamp(n.step + shift(rng), 0, std::max(0, project.params.bars * 16 - 1));
        }
    }

    if (chance(rng) < (0.68f * mutationIntensity) && !track->notes.empty())
    {
        std::uniform_int_distribution<size_t> pick(0, track->notes.size() - 1);
        auto& n = track->notes[pick(rng)];
        std::uniform_int_distribution<int> vel(isLofi ? -5 : -8, isLofi ? 5 : 8);
        n.velocity = std::clamp(n.velocity + vel(rng), 1, 127);
    }

    if (isLofi && (trackType == TrackType::OpenHat || trackType == TrackType::Perc || trackType == TrackType::GhostKick || trackType == TrackType::ClapGhostSnare))
    {
        if (!track->notes.empty() && chance(rng) < 0.38f)
        {
            std::uniform_int_distribution<size_t> pick(0, track->notes.size() - 1);
            track->notes.erase(track->notes.begin() + static_cast<long long>(pick(rng)));
        }
    }

    dedupeAndSort(track->notes);
    track->mutationDepth = std::clamp(track->mutationDepth + 0.12f, 0.0f, 1.0f);
    track->variationId += 1;

    PatternPerformanceTransformEngine::captureBasePatterns(project, { trackType });
}

void RapEngine::regenerateTrackInternal(PatternProject& project,
                                        TrackState& track,
                                        const RapStyleProfile& style,
                                        const std::vector<RapPhraseRole>& phrasePlan,
                                        std::mt19937& rng) const
{
    juce::ignoreUnused(project);
    if (!track.enabled)
    {
        track.notes.clear();
        return;
    }

    switch (track.type)
    {
        case TrackType::Kick:
            kickGenerator.generate(track, project.params, style, project.styleInfluence, phrasePlan, rng);
            break;
        case TrackType::Snare:
            snareGenerator.generate(track, project.params, style, phrasePlan, rng);
            break;
        case TrackType::HiHat:
            hatGenerator.generate(track, project.params, style, project.styleInfluence, phrasePlan, rng);
            break;
        case TrackType::OpenHat:
        case TrackType::ClapGhostSnare:
        case TrackType::GhostKick:
        case TrackType::Ride:
        case TrackType::Cymbal:
        case TrackType::Perc:
            track.notes.clear();
            break;
        default:
            break;
    }

    track.subProfile = style.name;
    if (track.laneRole.isEmpty())
        track.laneRole = roleForTrack(track.type);
}

void RapEngine::generateDependentTracks(PatternProject& project,
                                        const RapStyleProfile& style,
                                        const std::vector<RapPhraseRole>& phrasePlan,
                                        std::mt19937& rng,
                                        const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* kick = findTrack(project, TrackType::Kick);
    auto* snare = findTrack(project, TrackType::Snare);
    auto* hat = findTrack(project, TrackType::HiHat);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const auto spec = getRapStyleSpec(style.substyle);
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Rap, project.params.rapSubstyle);

    if (auto* clap = findTrack(project, TrackType::ClapGhostSnare);
        clap != nullptr && mutableTracks.count(clap->type) != 0 && clap->enabled && !clap->locked && snare != nullptr)
    {
        clap->notes.clear();
        std::uniform_int_distribution<int> clapVel(style.snareVelocityMin - 6, style.snareVelocityMax - 2);
        std::uniform_int_distribution<int> ghostVel(style.ghostVelocityMin, style.ghostVelocityMax);
        const float clapDensity = std::clamp(rapMix(spec.clapGhostDensityMin, spec.clapGhostDensityMax, project.params.densityAmount)
                                                 * laneBalanceWeight(project, TrackType::ClapGhostSnare),
                                             spec.clapGhostDensityMin,
                                             spec.clapGhostDensityMax);

        for (const auto& n : snare->notes)
        {
            const int bar = std::clamp(n.step / 16, 0, std::max(0, project.params.bars - 1));
            const auto referenceKickFeel = buildReferenceRapKickFeel(project, bar);
            const auto& clapDefaults = getLaneStyleDefaults(styleDefaults, clap->type);
            float clapGate = std::clamp(clapDensity * clapDefaults.noteProbability, 0.01f, 0.58f);
            if (referenceKickFeel.available)
                clapGate *= std::clamp(0.88f + referenceKickFeel.supportRatio * 0.22f + referenceKickFeel.anchorRatio * 0.1f, 0.76f, 1.22f);
            if (!n.isGhost && chance(rng) < clapGate)
                clap->notes.push_back({ 39, n.step, 1, std::clamp(clapVel(rng), 1, 127), 3, false });
            else if (chance(rng) < std::clamp(clapDensity * 0.62f * (referenceKickFeel.available ? std::clamp(0.9f + referenceKickFeel.supportRatio * 0.18f, 0.8f, 1.18f) : 1.0f), 0.01f, 0.30f))
                clap->notes.push_back({ 39, std::max(0, n.step - 1), 1, std::clamp(ghostVel(rng), 1, 127), 2, true });
        }
    }

    if (auto* ghostKick = findTrack(project, TrackType::GhostKick);
        ghostKick != nullptr && mutableTracks.count(ghostKick->type) != 0 && ghostKick->enabled && !ghostKick->locked && kick != nullptr)
    {
        ghostKick->notes.clear();
        std::uniform_int_distribution<int> vel(style.ghostVelocityMin, style.ghostVelocityMax);

        for (const auto& n : kick->notes)
        {
            if ((n.step % 16) == 0 || (n.step % 16) == 8)
                continue;

            const int bar = std::clamp(n.step / 16, 0, std::max(0, project.params.bars - 1));
            const auto referenceKickFeel = buildReferenceRapKickFeel(project, bar);
            const auto& ghostDefaults = getLaneStyleDefaults(styleDefaults, ghostKick->type);
            const float baseGhost = rapMix(spec.ghostKickDensityMin, spec.ghostKickDensityMax, project.params.densityAmount)
                * supportAccentWeight(project);
            float gate = std::clamp(baseGhost * ghostDefaults.noteProbability, 0.01f, 0.28f);
            if (referenceKickFeel.available)
                gate *= std::clamp(0.86f + referenceKickFeel.supportRatio * 0.3f + referenceKickFeel.presence[static_cast<size_t>(n.step % 16)] * 0.16f, 0.74f, 1.26f);
            if (chance(rng) < gate)
                ghostKick->notes.push_back({ 35, std::max(0, n.step - 1), 1, vel(rng), 2, true });
        }
    }

    if (auto* openHat = findTrack(project, TrackType::OpenHat);
        openHat != nullptr && mutableTracks.count(openHat->type) != 0 && openHat->enabled && !openHat->locked && hat != nullptr)
    {
        openHat->notes.clear();
        if (spec.openHatUseful)
        {
            std::uniform_int_distribution<int> vel(style.hatVelocityMin + 6, style.hatVelocityMax + 2);
            const float openDensity = std::clamp(rapMix(spec.openHatDensityMin, spec.openHatDensityMax, project.params.densityAmount)
                                                     * laneActivityWeight(project, TrackType::OpenHat),
                                                 spec.openHatDensityMin,
                                                 spec.openHatDensityMax);

            for (const auto& n : hat->notes)
            {
                const int stepInBar = n.step % 16;
                const int bar = n.step / 16;
                const auto referenceHatFeel = buildReferenceRapHatFeel(project, bar);
                const auto role = bar < static_cast<int>(phrasePlan.size()) ? phrasePlan[static_cast<size_t>(bar)] : RapPhraseRole::Base;
                const auto& openDefaults = getLaneStyleDefaults(styleDefaults, openHat->type);

                float gate = openDensity * openDefaults.noteProbability;
                if (stepInBar == 7 || stepInBar == 15)
                    gate += 0.06f;
                if (role == RapPhraseRole::Ending && stepInBar >= 12)
                    gate += 0.08f * openDefaults.phraseEndingProbability;
                if (referenceHatFeel.available)
                    gate *= std::clamp(0.86f + referenceHatFeel.supportRatio * 0.28f + (referenceHatFeel.gapRatio > 0.45f ? 0.08f : 0.0f), 0.74f, 1.22f);

                if (chance(rng) < std::clamp(gate, 0.0f, 0.52f))
                    openHat->notes.push_back({ 46, n.step, role == RapPhraseRole::Ending ? 2 : 1, std::clamp(vel(rng), 1, 127), n.microOffset, false });
            }
        }
    }

    if (auto* ride = findTrack(project, TrackType::Ride);
        ride != nullptr && mutableTracks.count(ride->type) != 0 && ride->enabled && !ride->locked)
    {
        ride->notes.clear();
        if (!spec.rideRare)
        {
            const auto& rideDefaults = getLaneStyleDefaults(styleDefaults, ride->type);
            if (style.rideChance > 0.01f && rideDefaults.enabledByDefault)
            {
                std::uniform_int_distribution<int> vel(style.hatVelocityMin, style.hatVelocityMax);
                const int bars = std::max(1, project.params.bars);
                for (int bar = 0; bar < bars; ++bar)
                {
                    const auto referenceHatFeel = buildReferenceRapHatFeel(project, bar);
                    for (int step : { 2, 6, 10, 14 })
                    {
                        float gate = std::clamp(style.rideChance * rideDefaults.noteProbability * std::clamp(laneBiasFor(project.styleInfluence, TrackRole::Ride).activityWeight, 0.55f, 1.5f), 0.01f, 0.55f);
                        if (referenceHatFeel.available)
                            gate *= std::clamp(0.9f + referenceHatFeel.supportRatio * 0.2f - std::max(0.0f, referenceHatFeel.gapRatio - 0.5f) * 0.12f, 0.78f, 1.18f);
                        if (chance(rng) < gate)
                            ride->notes.push_back({ 51, bar * 16 + step, 1, vel(rng), 2, false });
                    }
                }
            }
        }
    }

    if (auto* crash = findTrack(project, TrackType::Cymbal);
        crash != nullptr && mutableTracks.count(crash->type) != 0 && crash->enabled && !crash->locked)
    {
        crash->notes.clear();
        if (!spec.cymbalRare)
        {
            std::uniform_int_distribution<int> vel(style.snareVelocityMin - 4, style.snareVelocityMax);
            const auto& crashDefaults = getLaneStyleDefaults(styleDefaults, crash->type);
            if (chance(rng) < std::clamp(0.55f * crashDefaults.phraseEndingProbability, 0.06f, 0.9f))
                crash->notes.push_back({ 49, 0, 2, std::clamp(vel(rng), 1, 127), 0, false });

            const int lastBar = std::max(1, project.params.bars) - 1;
            if (chance(rng) < std::clamp(0.35f * crashDefaults.phraseEndingProbability, 0.05f, 0.9f))
                crash->notes.push_back({ 49, lastBar * 16 + 15, 2, std::clamp(vel(rng), 1, 127), 0, false });
        }
    }

    if (auto* perc = findTrack(project, TrackType::Perc);
        perc != nullptr && mutableTracks.count(perc->type) != 0 && perc->enabled && !perc->locked)
    {
        perc->notes.clear();
        std::uniform_int_distribution<int> vel(style.percVelocityMin, style.percVelocityMax);
        const int bars = std::max(1, project.params.bars);

        if (spec.percUseful)
        {
            const float percDensity = std::clamp(rapMix(spec.percDensityMin, spec.percDensityMax, project.params.densityAmount)
                                                     * laneActivityWeight(project, TrackType::Perc),
                                                 spec.percDensityMin,
                                                 spec.percDensityMax);
            for (int bar = 0; bar < bars; ++bar)
            {
                const auto referenceHatFeel = buildReferenceRapHatFeel(project, bar);
                for (int step : { 5, 9, 13 })
                {
                    float gate = std::clamp(percDensity * 0.7f, 0.02f, 0.24f);
                    if (referenceHatFeel.available)
                        gate *= std::clamp(0.88f + referenceHatFeel.supportRatio * 0.24f + (step == 13 ? 0.04f : 0.0f), 0.76f, 1.2f);
                    if (chance(rng) < gate)
                        perc->notes.push_back({ 50, bar * 16 + step, 1, std::clamp(vel(rng), 1, 127), 0, false });
                }
            }
            dedupeAndSort(perc->notes);
            const int maxPercHits = std::max(1, (bars / 2) * 4 + (bars % 2 == 0 ? 0 : 1));
            if (static_cast<int>(perc->notes.size()) > maxPercHits)
                perc->notes.resize(static_cast<size_t>(maxPercHits));
        }

        if (!spec.percUseful)
        {
            for (int bar = 0; bar < bars; ++bar)
            {
                for (int step : { 5, 9, 13 })
                {
                    const auto& percDefaults = getLaneStyleDefaults(styleDefaults, perc->type);
                    if (chance(rng) < std::clamp(style.percChance * percDefaults.noteProbability, 0.01f, 0.45f))
                        perc->notes.push_back({ 50, bar * 16 + step, 1, vel(rng), 0, false });
                }
            }
        }
    }

    if (auto* sub = findTrack(project, TrackType::Sub808);
        sub != nullptr && mutableTracks.count(sub->type) != 0 && sub->enabled && !sub->locked)
    {
        sub->notes.clear();

        const float subDensity = std::clamp(rapMix(spec.sub808DensityMin, spec.sub808DensityMax, project.params.densityAmount) * lowEndCouplingWeight(project),
                                            spec.sub808DensityMin,
                                            spec.sub808DensityMax);

        if (!spec.sub808EnabledByDefault && subDensity < 0.06f)
            return;

        std::uniform_int_distribution<int> vel(style.kickVelocityMin - 20, style.kickVelocityMax - 8);
        const int basePitch = std::clamp(36 + project.params.keyRoot, 24, 84);

        if (kick != nullptr)
        {
            for (const auto& k : kick->notes)
            {
                const int stepInBar = k.step % 16;
                const int bar = std::clamp(k.step / 16, 0, std::max(0, project.params.bars - 1));
                const auto referenceKickFeel = buildReferenceRapKickFeel(project, bar);
                const bool anchor = stepInBar == 0 || stepInBar == 8;
                float gate = spec.sub808FollowKickMoreOften ? (anchor ? 0.62f : 0.36f) : (anchor ? 0.42f : 0.20f);
                gate = std::clamp(gate + subDensity * 0.46f, 0.02f, 0.88f);
                if (referenceKickFeel.available)
                {
                    const float presence = referenceKickFeel.presence[static_cast<size_t>(stepInBar)];
                    gate *= std::clamp(0.86f + presence * (anchor ? 0.42f : 0.34f) + referenceKickFeel.anchorRatio * (anchor ? 0.18f : 0.04f), 0.72f, 1.28f);
                    if (!anchor && referenceKickFeel.density < 0.28f)
                        gate *= 0.82f;
                }
                if (chance(rng) > gate)
                    continue;

                int pitch = basePitch;
                if (spec.sub808AllowSlides && style.substyle == RapSubstyle::RnBRap && chance(rng) < 0.08f)
                    pitch = std::clamp(basePitch + (chance(rng) < 0.5f ? 7 : 12), 24, 96);

                int length = style.substyle == RapSubstyle::RnBRap ? 2 : 1;
                if (referenceKickFeel.available)
                {
                    if (anchor && referenceKickFeel.anchorRatio > 0.32f)
                        length = std::max(length, style.substyle == RapSubstyle::RnBRap ? 3 : 2);
                    if (stepInBar >= 14 && referenceKickFeel.tailRatio > 0.14f)
                        length = std::max(length, 2);
                }
                sub->notes.push_back({ pitch, k.step, length, std::clamp(vel(rng), 1, 127), 0, false });
            }
        }

        dedupeAndSort(sub->notes);
        const float subPerBar = style.substyle == RapSubstyle::DirtySouthClassic
            ? 3.0f
            : (style.substyle == RapSubstyle::HardcoreRap ? 1.0f : 2.0f);
        const int maxSubEvents = std::max(1, static_cast<int>(std::ceil(static_cast<float>(std::max(1, project.params.bars)) * subPerBar)));
        if (static_cast<int>(sub->notes.size()) > maxSubEvents)
            sub->notes.resize(static_cast<size_t>(maxSubEvents));
    }
}

void RapEngine::postProcess(PatternProject& project,
                            const RapStyleProfile& style,
                            std::mt19937& rng,
                            const std::unordered_set<TrackType>& mutableTracks) const
{
    if (isLofiRapStyle(style))
    {
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);
        const auto& lofi = getLofiRapStyleSpec();
        const float swingAmount = std::clamp((project.params.swingPercent - 50.0f) / 25.0f, 0.0f, 1.0f);
        const float targetSwing = lofiMix(lofi.swingMin, lofi.swingMax, swingAmount);
        const int swingTicks = static_cast<int>(juce::jmap(targetSwing, 0.0f, 0.40f, 0.0f, 16.0f));
        const float velocityBlend = std::clamp(lofiMix(lofi.velocityAmountMin, lofi.velocityAmountMax, project.params.velocityAmount), 0.24f, 0.7f);

        for (auto& track : project.tracks)
        {
            if (mutableTracks.count(track.type) == 0)
                continue;

            for (auto& note : track.notes)
            {
                const auto timing = lofiTimingWindow(track.type, note);
                std::uniform_int_distribution<int> microDist(timing.first, timing.second);
                int micro = microDist(rng);
                if ((note.step % 2) == 1 && (track.type == TrackType::HiHat || track.type == TrackType::Perc))
                    micro += static_cast<int>(std::round(swingTicks * 0.42f));

                note.microOffset = std::clamp(micro, -120, 120);

                if (chance(rng) < velocityBlend)
                    note.velocity = std::clamp(sampleLofiVelocity(track.type, note, rng), 1, 127);
            }
        }

        applySampleAwareRapFlavor(project, mutableTracks);

        return;
    }

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const auto spec = getRapStyleSpec(style.substyle);
    const float swingNorm = std::clamp((project.params.swingPercent - 50.0f) / 25.0f, 0.0f, 1.0f);
    const float targetSwing = rapMix(spec.swingMin, spec.swingMax, swingNorm);
    const int swingTicks = static_cast<int>(juce::jmap(targetSwing, 0.50f, 0.60f, 0.0f, 16.0f));
    const float velocityBlend = std::clamp(rapMix(spec.velocityAmountMin, spec.velocityAmountMax, project.params.velocityAmount), 0.2f, 0.72f);

    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0)
            continue;

        for (auto& note : track.notes)
        {
            const auto timing = rapTimingWindow(style.substyle, track.type, note);
            std::uniform_int_distribution<int> microDist(timing.first, timing.second);
            int micro = microDist(rng);
            if ((note.step % 2) == 1 && (track.type == TrackType::HiHat || track.type == TrackType::Perc || track.type == TrackType::OpenHat))
                micro += static_cast<int>(std::round(swingTicks * 0.40f));

            note.microOffset = std::clamp(micro, -120, 120);

            if (chance(rng) < velocityBlend)
                note.velocity = std::clamp(sampleRapVelocity(style.substyle, track.type, note, rng), 1, 127);
        }
    }

    applySampleAwareRapFlavor(project, mutableTracks);
}

void RapEngine::validatePattern(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks) const
{
    const int bars = std::max(1, project.params.bars);
    const auto& style = getRapProfile(project.params.rapSubstyle);

    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0)
            continue;

        dedupeAndSort(track.notes);

        for (auto& note : track.notes)
        {
            note.step = std::clamp(note.step, 0, bars * 16 - 1);
            note.length = std::max(1, note.length);
            note.velocity = std::clamp(note.velocity, 1, 127);
            note.microOffset = std::clamp(note.microOffset, -120, 120);
        }

        int maxHits = bars * 10;
        if (track.type == TrackType::HiHat)
            maxHits = bars * 16;
        else if (track.type == TrackType::OpenHat)
            maxHits = bars * 4;
        else if (track.type == TrackType::ClapGhostSnare || track.type == TrackType::GhostKick)
            maxHits = bars * 5;
        else if (track.type == TrackType::Ride || track.type == TrackType::Cymbal)
            maxHits = bars * 4;

        if (static_cast<int>(track.notes.size()) > maxHits)
            track.notes.resize(static_cast<size_t>(maxHits));
    }

    auto* kick = findTrack(project, TrackType::Kick);
    const auto* snare = findTrack(project, TrackType::Snare);
    const auto* clap = findTrack(project, TrackType::ClapGhostSnare);
    if (kick != nullptr && mutableTracks.count(kick->type) != 0)
    {
        kick->notes.erase(std::remove_if(kick->notes.begin(), kick->notes.end(), [snare, clap](const NoteEvent& k)
        {
            const bool onSnare = snare != nullptr && std::any_of(snare->notes.begin(), snare->notes.end(), [&k](const NoteEvent& s)
            {
                return !s.isGhost && s.step == k.step;
            });

            const bool onClap = clap != nullptr && std::any_of(clap->notes.begin(), clap->notes.end(), [&k](const NoteEvent& c)
            {
                return !c.isGhost && c.step == k.step;
            });

            return onSnare || onClap;
        }), kick->notes.end());
    }

    if (isEastCoastRapStyle(style))
        applyEastCoastPocketRules(project, style, mutableTracks);
    if (isWestCoastRapStyle(style))
        applyWestCoastPocketRules(project, style, mutableTracks);
    if (isDirtySouthRapStyle(style))
        applyDirtySouthPocketRules(project, style, mutableTracks);
    if (isGermanStreetRapStyle(style))
        applyGermanStreetPocketRules(project, style, mutableTracks);
}
} // namespace bbg
