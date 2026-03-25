#include "RapEngine.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "../Core/TrackRegistry.h"
#include "Rap/LofiRapStyleSpec.h"
#include "Rap/RapStyleSpec.h"
#include "StyleInfluence.h"
#include "StyleDefaults.h"

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
    return type == TrackType::HiHat
        || type == TrackType::OpenHat
        || type == TrackType::Perc
        || type == TrackType::Ride
        || type == TrackType::Cymbal;
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
                case RapSubstyle::GermanStreetRap: return { -2, 6 };
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
            const auto& clapDefaults = getLaneStyleDefaults(styleDefaults, clap->type);
            if (!n.isGhost && chance(rng) < std::clamp(clapDensity * clapDefaults.noteProbability, 0.01f, 0.58f))
                clap->notes.push_back({ 39, n.step, 1, std::clamp(clapVel(rng), 1, 127), 3, false });
            else if (chance(rng) < std::clamp(clapDensity * 0.62f, 0.01f, 0.30f))
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

            const auto& ghostDefaults = getLaneStyleDefaults(styleDefaults, ghostKick->type);
            const float baseGhost = rapMix(spec.ghostKickDensityMin, spec.ghostKickDensityMax, project.params.densityAmount)
                * supportAccentWeight(project);
            const float gate = std::clamp(baseGhost * ghostDefaults.noteProbability, 0.01f, 0.28f);
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
                const auto role = bar < static_cast<int>(phrasePlan.size()) ? phrasePlan[static_cast<size_t>(bar)] : RapPhraseRole::Base;
                const auto& openDefaults = getLaneStyleDefaults(styleDefaults, openHat->type);

                float gate = openDensity * openDefaults.noteProbability;
                if (stepInBar == 7 || stepInBar == 15)
                    gate += 0.06f;
                if (role == RapPhraseRole::Ending && stepInBar >= 12)
                    gate += 0.08f * openDefaults.phraseEndingProbability;

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
                    for (int step : { 2, 6, 10, 14 })
                    {
                        if (chance(rng) < std::clamp(style.rideChance * rideDefaults.noteProbability * laneActivityWeight(project, TrackType::Ride), 0.01f, 0.55f))
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
                for (int step : { 5, 9, 13 })
                {
                    if (chance(rng) < std::clamp(percDensity * 0.7f, 0.02f, 0.24f))
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

        const float subDensity = std::clamp(rapMix(spec.sub808DensityMin, spec.sub808DensityMax, project.params.densityAmount),
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
                const bool anchor = stepInBar == 0 || stepInBar == 8;
                float gate = spec.sub808FollowKickMoreOften ? (anchor ? 0.62f : 0.36f) : (anchor ? 0.42f : 0.20f);
                gate = std::clamp(gate + subDensity * 0.46f, 0.02f, 0.88f);
                if (chance(rng) > gate)
                    continue;

                int pitch = basePitch;
                if (spec.sub808AllowSlides && style.substyle == RapSubstyle::RnBRap && chance(rng) < 0.08f)
                    pitch = std::clamp(basePitch + (chance(rng) < 0.5f ? 7 : 12), 24, 96);

                const int length = style.substyle == RapSubstyle::RnBRap ? 2 : 1;
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
}
} // namespace bbg
