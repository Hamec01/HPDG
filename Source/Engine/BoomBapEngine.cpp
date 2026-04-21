#include "BoomBapEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>

#include "../Core/TrackSemantics.h"
#include "GrooveEngine.h"
#include "HumanizeEngine.h"
#include "PatternPerformanceTransformEngine.h"
#include "StyleInfluence.h"
#include "StyleDefaults.h"
#include "VelocityEngine.h"
#include "../Analysis/StepHintWeighter.h"

namespace bbg
{
namespace
{
TrackState* findTrack(PatternProject& project, TrackType type)
{
    auto it = std::find_if(project.tracks.begin(), project.tracks.end(), [type](const TrackState& t) { return t.type == type; });
    return it != project.tracks.end() ? &(*it) : nullptr;
}

const TrackState* findTrack(const PatternProject& project, TrackType type)
{
    auto it = std::find_if(project.tracks.begin(), project.tracks.end(), [type](const TrackState& t) { return t.type == type; });
    return it != project.tracks.end() ? &(*it) : nullptr;
}

bool containsStep(const std::vector<NoteEvent>& notes, int step)
{
    return std::any_of(notes.begin(), notes.end(), [step](const NoteEvent& n) { return n.step == step; });
}

const BoomBapBarBlueprint* blueprintBarAt(const BoomBapGrooveBlueprint& blueprint, int bar)
{
    if (bar < 0 || bar >= static_cast<int>(blueprint.bars.size()))
        return nullptr;

    return &blueprint.bars[static_cast<size_t>(bar)];
}

const BoomBapLaneActivation* laneBarAt(const BoomBapLaneActivationPlan& plan, int bar)
{
    if (bar < 0 || bar >= static_cast<int>(plan.bars.size()))
        return nullptr;

    return &plan.bars[static_cast<size_t>(bar)];
}

void applyBoomBapStyleInfluence(PatternProject& project)
{
    juce::String applyError;
    BoomBapStyleInfluence::apply(project, &applyError);
    juce::ignoreUnused(applyError);
}

float laneActivityWeight(const PatternProject& project, TrackType type)
{
    return std::clamp(laneBiasFor(project.styleInfluence, type).activityWeight, 0.5f, 1.6f);
}

float laneBalanceWeight(const PatternProject& project, TrackType type)
{
    return std::clamp(laneBiasFor(project.styleInfluence, type).balanceWeight, 0.5f, 1.6f);
}

float supportAccentWeight(const PatternProject& project)
{
    return std::clamp(project.styleInfluence.supportAccentWeight, 0.65f, 1.5f);
}

struct ReferenceBoomBapGrooveFeel
{
    bool available = false;
    float carrierRatio = 0.0f;
    float supportRatio = 0.0f;
    float gapRatio = 0.0f;
    float punctuationRatio = 0.0f;
};

ReferenceBoomBapGrooveFeel buildReferenceBoomBapGrooveFeel(const PatternProject& project, int bar)
{
    ReferenceBoomBapGrooveFeel feel;
    int hatBars = 0;
    float hatNotes = 0.0f;
    float carrierNotes = 0.0f;
    float supportNotes = 0.0f;
    float emptySlots = 0.0f;

    if (project.styleInfluence.referenceHatCorpus.available && !project.styleInfluence.referenceHatCorpus.variants.empty())
    {
        for (const auto& variant : project.styleInfluence.referenceHatCorpus.variants)
        {
            if (!variant.available || variant.barMaps.empty())
                continue;
            const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barMaps.size()));
            const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
            if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barMaps.size()))
                continue;

            const auto& barMap = variant.barMaps[static_cast<size_t>(normalizedBar)];
            ++hatBars;
            std::array<bool, 8> occupiedSlots {};
            for (const auto& note : barMap.notes)
            {
                const int step16 = std::clamp(note.tickInBar / 120, 0, 15);
                occupiedSlots[static_cast<size_t>(step16 / 2)] = true;
                hatNotes += 1.0f;
                if ((step16 % 4) == 0)
                    carrierNotes += 1.0f;
                else
                    supportNotes += 1.0f;
            }
            for (size_t i = 0; i < occupiedSlots.size(); ++i)
                if (!occupiedSlots[i])
                    emptySlots += 1.0f;
        }
    }

    float kickNotes = 0.0f;
    float punctuation = 0.0f;
    int kickBars = 0;
    if (project.styleInfluence.referenceKickCorpus.available && !project.styleInfluence.referenceKickCorpus.variants.empty())
    {
        for (const auto& variant : project.styleInfluence.referenceKickCorpus.variants)
        {
            if (!variant.available || variant.barPatterns.empty())
                continue;
            const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barPatterns.size()));
            const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
            if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barPatterns.size()))
                continue;

            const auto& pattern = variant.barPatterns[static_cast<size_t>(normalizedBar)];
            ++kickBars;
            kickNotes += static_cast<float>(pattern.notes.size());
            for (const auto& note : pattern.notes)
                if (note.step16 >= 11)
                    punctuation += 1.0f;
        }
    }

    if (hatBars <= 0 && kickBars <= 0)
        return feel;

    feel.available = true;
    feel.carrierRatio = hatNotes > 0.0f ? carrierNotes / hatNotes : 0.0f;
    feel.supportRatio = hatNotes > 0.0f ? supportNotes / hatNotes : 0.0f;
    feel.gapRatio = hatBars > 0 ? emptySlots / (static_cast<float>(hatBars) * 8.0f) : 0.0f;
    feel.punctuationRatio = kickNotes > 0.0f ? punctuation / kickNotes : 0.0f;
    return feel;
}

void filterLaneNotesByBarActivation(TrackState& track,
                                    const BoomBapLaneActivationPlan& lanePlan,
                                    std::function<bool(const BoomBapLaneActivation&)> selector)
{
    track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [&](const NoteEvent& n)
    {
        const int bar = n.step / 16;
        const auto* lane = laneBarAt(lanePlan, bar);
        return lane != nullptr && !selector(*lane);
    }), track.notes.end());
}

const StepFeature* featureAtStep(const AudioFeatureMap& map, int step)
{
    if (map.steps.empty() || map.stepsPerBar <= 0)
        return nullptr;

    const int normalized = std::max(0, step);
    const size_t idx = static_cast<size_t>(normalized) % map.steps.size();
    return &map.steps[idx];
}

bool isHatLikeTrack(TrackType type)
{
    const auto role = roleFromTrackType(type);
    const auto family = familyFromTrackType(type);

    return role == TrackRole::HiHat
        || role == TrackRole::OpenHat
        || role == TrackRole::Perc
        || family == TrackFamily::CymbalFamily;
}

void applySampleAwareBoomBapFlavor(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks)
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

            float guide = 0.45f * f->accent + 0.30f * f->onset + 0.25f * f->energy;
            if (track.type == TrackType::Kick || track.type == TrackType::GhostKick)
                guide = 0.42f * f->low + 0.33f * f->accent + 0.25f * f->onset;
            else if (isHatLikeTrack(track.type))
                guide = 0.46f * f->high + 0.34f * f->energy + 0.20f * f->onset;

            const float gain = std::clamp(1.0f
                                              + 0.22f * react * support * (guide - 0.5f)
                                              + 0.16f * react * contrast * (0.5f - guide),
                                          0.72f,
                                          1.38f);
            note.velocity = std::clamp(static_cast<int>(static_cast<float>(note.velocity) * gain), 1, 127);

            if ((track.type == TrackType::Snare || track.type == TrackType::ClapGhostSnare)
                && f->nearPhraseBoundary
                && contrast > 0.15f)
            {
                const int nudge = static_cast<int>(2.0f + 3.0f * contrast * react);
                note.microOffset = std::clamp(note.microOffset + nudge, -120, 120);
            }
        }

        if (isHatLikeTrack(track.type) && support * react > 0.45f)
        {
            track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
            {
                const auto* f = featureAtStep(ctx.featureMap, note.step);
                if (f == nullptr)
                    return false;

                const int phase = (note.step + note.velocity + static_cast<int>(track.type)) % 7;
                return phase == 0 && f->high > 0.82f && f->onset < 0.30f && !f->isStrongBeat;
            }), track.notes.end());
        }
    }

    StepHintWeighter::applyToProject(project, mutableTracks);
}

float carrierDensityForMode(CarrierMode mode)
{
    switch (mode)
    {
        case CarrierMode::Hat: return 1.0f;
        case CarrierMode::Ride: return 0.72f;
        case CarrierMode::Hybrid: return 0.86f;
        default: return 1.0f;
    }
}

juce::String roleForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick: return "foundation";
        case TrackType::Snare: return "backbeat";
        case TrackType::HiHat: return "carrier";
        case TrackType::OpenHat: return "phrase_air";
        case TrackType::GhostKick: return "support_ghost";
        case TrackType::ClapGhostSnare: return "backbeat_layer";
        case TrackType::Perc: return "punctuation";
        case TrackType::Ride: return "carrier_support";
        case TrackType::Cymbal: return "ending_mark";
        default: return "lane";
    }
}

bool isAnchorStepForTrack(TrackType type, int stepInBar)
{
    if (type == TrackType::Snare || type == TrackType::ClapGhostSnare)
        return stepInBar == 4 || stepInBar == 12;

    if (type == TrackType::Kick || type == TrackType::GhostKick)
        return stepInBar == 0 || stepInBar == 8;

    if (type == TrackType::HiHat)
        return (stepInBar % 4) == 0;

    return false;
}

void dedupeAndSortNotes(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;

        if (a.isGhost != b.isGhost)
            return !a.isGhost;

        return a.velocity > b.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        return a.step == b.step && a.pitch == b.pitch;
    }), notes.end());
}

std::vector<NoteEvent> mergeVariationNotes(TrackType type,
                                           const std::vector<NoteEvent>& previous,
                                           const std::vector<NoteEvent>& fresh,
                                           float rgVariationIntensity,
                                           std::mt19937& rng)
{
    if (previous.empty())
        return fresh;

    std::vector<NoteEvent> out;
    out.reserve(previous.size() + fresh.size());

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    for (const auto& n : previous)
    {
        const int barStep = n.step % 16;
        const bool anchor = isAnchorStepForTrack(type, barStep) || (type == TrackType::Kick && n.velocity >= 108);
        const float keepChance = anchor ? 1.0f : std::clamp((type == TrackType::HiHat ? 0.58f : 0.50f) * rgVariationIntensity, 0.2f, 0.95f);
        if (chance(rng) <= keepChance)
            out.push_back(n);
    }

    for (const auto& n : fresh)
    {
        const int barStep = n.step % 16;
        const bool anchor = isAnchorStepForTrack(type, barStep);

        if (anchor && containsStep(out, n.step))
            continue;

        if (!anchor)
        {
            const float baseChance = type == TrackType::GhostKick || type == TrackType::Perc ? 0.38f : 0.58f;
            const float addChance = std::clamp(baseChance * rgVariationIntensity, 0.12f, 0.95f);
            if (chance(rng) > addChance)
                continue;
        }

        out.push_back(n);
    }

    dedupeAndSortNotes(out);
    return out;
}

int laneBarBudget(TrackType type,
                  const BoomBapBarBlueprint& bar,
                  BoomBapSubstyle substyle,
                  float density)
{
    const float den = std::clamp(density, 0.0f, 1.0f);

    switch (type)
    {
        case TrackType::HiHat:
        {
            int budget = static_cast<int>(5.0f + bar.hatActivity * 9.0f + den * 2.0f);
            if (substyle == BoomBapSubstyle::Classic)
                budget = std::max(budget, 8);
            if (bar.stripToCore)
                budget = std::min(budget, 6);
            if (substyle == BoomBapSubstyle::LofiRap)
                budget = std::min(budget, 8);
            return std::clamp(budget, substyle == BoomBapSubstyle::Classic && !bar.stripToCore ? 8 : 3, substyle == BoomBapSubstyle::Classic ? 12 : 16);
        }
        case TrackType::OpenHat:
        {
            int budget = static_cast<int>(bar.endLiftAmount > 0.6f ? 2 : 1);
            if (substyle == BoomBapSubstyle::Classic)
                budget = bar.role == PhraseRole::Ending || den > 0.68f ? 1 : 0;
            if (substyle == BoomBapSubstyle::BoomBapGold && bar.role == PhraseRole::Ending)
                budget += 1;
            if (bar.stripToCore)
                budget = 0;
            return std::clamp(budget, 0, 3);
        }
        case TrackType::Perc:
        {
            int budget = static_cast<int>(bar.hatSyncopation * 4.0f + den * 2.0f);
            if (substyle == BoomBapSubstyle::Classic)
                budget = (bar.role == PhraseRole::Ending || (bar.role == PhraseRole::Variation && den > 0.62f)) ? 1 : 0;
            if (bar.role == PhraseRole::Ending)
                budget += 1;
            if (substyle == BoomBapSubstyle::Classic)
                budget = std::min(budget, 1);
            if (substyle == BoomBapSubstyle::LofiRap)
                budget = std::min(budget, 1);
            if (bar.stripToCore)
                budget = 0;
            return std::clamp(budget, 0, 5);
        }
        case TrackType::Ride:
        {
            int budget = static_cast<int>(2.0f + bar.hatActivity * 5.0f);
            if (substyle == BoomBapSubstyle::Classic)
                budget = 0;
            if (bar.role == PhraseRole::Ending)
                budget += 1;
            if (substyle == BoomBapSubstyle::Classic)
                budget = 0;
            if (substyle == BoomBapSubstyle::LofiRap || substyle == BoomBapSubstyle::RussianUnderground)
                budget = 0;
            if (bar.stripToCore)
                budget = 0;
            return std::clamp(budget, 0, 8);
        }
        case TrackType::GhostKick:
        {
            int budget = static_cast<int>(bar.kickSupportAmount * 4.0f + den * 2.0f);
            if (substyle == BoomBapSubstyle::Classic)
                budget = bar.kickSupportAmount > 0.58f || bar.role == PhraseRole::Ending ? 1 : 0;
            if (substyle == BoomBapSubstyle::LofiRap)
                budget = std::min(budget, 1);
            if (bar.stripToCore)
                budget = 0;
            return std::clamp(budget, 0, 4);
        }
        case TrackType::ClapGhostSnare:
        {
            int budget = bar.strongBackbeat ? 2 : 1;
            if (substyle == BoomBapSubstyle::Classic)
                budget = (bar.role == PhraseRole::Variation || bar.role == PhraseRole::Ending) ? 1 : 0;
            if (bar.role == PhraseRole::Ending)
                budget += 1;
            if (substyle == BoomBapSubstyle::Classic)
                budget = std::min(budget, 1);
            if (substyle == BoomBapSubstyle::LofiRap || substyle == BoomBapSubstyle::RussianUnderground)
                budget = std::min(budget, 1);
            if (bar.stripToCore)
                budget = 0;
            return std::clamp(budget, 0, 3);
        }
        case TrackType::Cymbal:
            return bar.role == PhraseRole::Ending && !bar.stripToCore ? 1 : 0;
        default:
            return 16;
    }
}

void trimTrackToBarBudgets(TrackState& track,
                           const BoomBapGrooveBlueprint& blueprint,
                           BoomBapSubstyle substyle,
                           float density)
{
    if (track.notes.empty())
        return;

    std::unordered_map<int, std::vector<NoteEvent>> byBar;
    byBar.reserve(blueprint.bars.size());
    for (const auto& note : track.notes)
        byBar[note.step / 16].push_back(note);

    std::vector<NoteEvent> trimmed;
    trimmed.reserve(track.notes.size());

    for (const auto& [barIndex, notes] : byBar)
    {
        const auto* bar = blueprintBarAt(blueprint, barIndex);
        if (bar == nullptr)
        {
            trimmed.insert(trimmed.end(), notes.begin(), notes.end());
            continue;
        }

        const int budget = laneBarBudget(track.type, *bar, substyle, density);
        if (budget <= 0)
            continue;

        std::vector<NoteEvent> sorted = notes;
        std::stable_sort(sorted.begin(), sorted.end(), [](const NoteEvent& a, const NoteEvent& b)
        {
            const int aStrong = ((a.step % 4) == 0) ? 1 : 0;
            const int bStrong = ((b.step % 4) == 0) ? 1 : 0;
            if (aStrong != bStrong)
                return aStrong > bStrong;

            if (a.isGhost != b.isGhost)
                return !a.isGhost;

            return a.velocity > b.velocity;
        });

        const int keep = std::min(budget, static_cast<int>(sorted.size()));
        for (int i = 0; i < keep; ++i)
            trimmed.push_back(sorted[static_cast<size_t>(i)]);
    }

    track.notes = std::move(trimmed);
    dedupeAndSortNotes(track.notes);
}

int normalizedStepInBar(int step)
{
    const int normalized = step % 16;
    return normalized < 0 ? normalized + 16 : normalized;
}

int classicGhostPriority(const NoteEvent& note, int bars)
{
    const int step = normalizedStepInBar(note.step);
    const int bar = note.step / 16;
    int score = note.velocity;

    if (bar == bars - 1)
        score += 120;
    if (step == 11)
        score += 80;
    else if (step == 3)
        score += 70;
    else if (step == 15)
        score += 55;
    else if (step == 10 || step == 14)
        score += 30;

    return score;
}

int classicKickPriority(const NoteEvent& note)
{
    const int step = normalizedStepInBar(note.step);
    int score = note.velocity;

    if (step == 0)
        score += 220;
    else if (step == 8 || step == 10)
        score += 120;
    else if (step == 7 || step == 11 || step == 14)
        score += 80;
    else if (step == 3 || step == 6 || step == 13 || step == 15)
        score += 45;

    return score;
}

void pruneClassicSnareGhosts(TrackState& snare, int bars)
{
    std::vector<NoteEvent> anchors;
    std::vector<NoteEvent> ghosts;
    anchors.reserve(snare.notes.size());
    ghosts.reserve(snare.notes.size());

    for (const auto& note : snare.notes)
    {
        if (note.isGhost)
            ghosts.push_back(note);
        else
            anchors.push_back(note);
    }

    const int maxGhosts = std::max(1, (bars + 3) / 4);
    std::stable_sort(ghosts.begin(), ghosts.end(), [bars](const NoteEvent& left, const NoteEvent& right)
    {
        const int leftScore = classicGhostPriority(left, bars);
        const int rightScore = classicGhostPriority(right, bars);
        if (leftScore != rightScore)
            return leftScore > rightScore;
        return left.step < right.step;
    });
    if (static_cast<int>(ghosts.size()) > maxGhosts)
        ghosts.resize(static_cast<size_t>(maxGhosts));

    anchors.insert(anchors.end(), ghosts.begin(), ghosts.end());
    snare.notes = std::move(anchors);
    dedupeAndSortNotes(snare.notes);
}

void pruneClassicSupportGhostLane(TrackState& track, int bars, const TrackState* snare)
{
    track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [snare](const NoteEvent& note)
    {
        if (normalizedStepInBar(note.step) == 4 || normalizedStepInBar(note.step) == 12)
            return true;
        if (snare == nullptr)
            return false;
        return std::any_of(snare->notes.begin(), snare->notes.end(), [&note](const NoteEvent& snareNote)
        {
            return !snareNote.isGhost && snareNote.step == note.step;
        });
    }), track.notes.end());

    std::stable_sort(track.notes.begin(), track.notes.end(), [bars](const NoteEvent& left, const NoteEvent& right)
    {
        const int leftScore = classicGhostPriority(left, bars);
        const int rightScore = classicGhostPriority(right, bars);
        if (leftScore != rightScore)
            return leftScore > rightScore;
        return left.step < right.step;
    });

    const int maxEvents = std::max(1, (bars + 3) / 4);
    if (static_cast<int>(track.notes.size()) > maxEvents)
        track.notes.resize(static_cast<size_t>(maxEvents));

    dedupeAndSortNotes(track.notes);
}

void pruneClassicKickDensity(TrackState& kick, int bars)
{
    std::vector<NoteEvent> filtered;
    filtered.reserve(kick.notes.size());

    for (int bar = 0; bar < bars; ++bar)
    {
        std::vector<NoteEvent> barNotes;
        for (const auto& note : kick.notes)
        {
            if (note.step / 16 != bar)
                continue;
            const int step = normalizedStepInBar(note.step);
            if (step == 4 || step == 12)
                continue;
            barNotes.push_back(note);
        }

        std::stable_sort(barNotes.begin(), barNotes.end(), [](const NoteEvent& left, const NoteEvent& right)
        {
            const int leftScore = classicKickPriority(left);
            const int rightScore = classicKickPriority(right);
            if (leftScore != rightScore)
                return leftScore > rightScore;
            return left.step < right.step;
        });

        const int maxPerBar = bar == bars - 1 ? 5 : 4;
        if (static_cast<int>(barNotes.size()) > maxPerBar)
            barNotes.resize(static_cast<size_t>(maxPerBar));

        filtered.insert(filtered.end(), barNotes.begin(), barNotes.end());
    }

    kick.notes = std::move(filtered);
    dedupeAndSortNotes(kick.notes);
}

void reinforceClassicHatEighths(TrackState& hat, int bars, const BoomBapStyleProfile& style)
{
    static constexpr std::array<int, 8> kEighthSteps { 0, 2, 4, 6, 8, 10, 12, 14 };

    for (int bar = 0; bar < bars; ++bar)
    {
        for (size_t i = 0; i < kEighthSteps.size(); ++i)
        {
            const int step = bar * 16 + kEighthSteps[i];
            if (containsStep(hat.notes, step))
                continue;

            const bool quarter = (kEighthSteps[i] % 4) == 0;
            const int velocity = std::clamp(style.hatVelocityMin + (quarter ? 22 : 12) + static_cast<int>(i % 2) * 2,
                                            style.hatVelocityMin,
                                            style.hatVelocityMax);
            const int microOffset = quarter ? 0 : 4;
            hat.notes.push_back({ 42, step, 1, velocity, microOffset, false });
        }
    }

    dedupeAndSortNotes(hat.notes);
}

int deterministicDustyDrift(int seed, int bar, int step, int salt, int spread)
{
    if (spread <= 0)
        return 0;

    unsigned int x = static_cast<unsigned int>(seed * 374761393u
                                               + bar * 668265263u
                                               + step * 2246822519u
                                               + salt * 3266489917u);
    x ^= x >> 13;
    x *= 1274126177u;
    x ^= x >> 16;

    const int width = spread * 2 + 1;
    return static_cast<int>(x % static_cast<unsigned int>(width)) - spread;
}

int dustyOffbeatDelayTicks(const PatternProject& project, const BoomBapStyleProfile& style)
{
    const float requested = std::clamp(project.params.swingPercent, 56.5f, 62.0f);
    const float profiled = std::clamp(style.swingPercent, 57.0f, 61.0f);
    const float swingPoint = requested * 0.42f + profiled * 0.58f;
    const float delayedEighthTicks = 480.0f * (swingPoint / 100.0f - 0.5f);
    return std::clamp(static_cast<int>(std::round(delayedEighthTicks)), 28, 48);
}

int dustyPocketOffsetFor(TrackType type,
                         const NoteEvent& note,
                         const PatternProject& project,
                         const BoomBapStyleProfile& style,
                         int offbeatDelayTicks)
{
    juce::ignoreUnused(style);

    const int step = normalizedStepInBar(note.step);
    const int bar = std::max(0, note.step / 16);
    const int phase = step % 4;
    const int smallDrift = deterministicDustyDrift(project.params.seed, bar, step, static_cast<int>(type) + 11, 3);
    const int tinyDrift = deterministicDustyDrift(project.params.seed, bar, step, static_cast<int>(type) + 29, 2);

    switch (type)
    {
        case TrackType::HiHat:
            if (phase == 0)
                return std::clamp(5 + tinyDrift, 0, 12);
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + smallDrift, 28, 52);
            if (phase == 1)
                return std::clamp(offbeatDelayTicks / 2 - 8 + smallDrift, 0, 20);
            return std::clamp(offbeatDelayTicks / 2 + 5 + smallDrift, 10, 32);

        case TrackType::Ride:
            if (phase == 0)
                return std::clamp(7 + tinyDrift, 2, 15);
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + 2 + smallDrift, 30, 56);
            return std::clamp(offbeatDelayTicks / 2 + 6 + smallDrift, 12, 34);

        case TrackType::Snare:
            if (!note.isGhost && (step == 4 || step == 12))
            {
                const float anchorScale = step == 4 ? 0.34f : 0.30f;
                return std::clamp(static_cast<int>(std::round(static_cast<float>(offbeatDelayTicks) * anchorScale)) + tinyDrift,
                                  8,
                                  20);
            }
            return std::clamp(static_cast<int>(std::round(static_cast<float>(offbeatDelayTicks) * 0.20f)) + smallDrift,
                              -4,
                              16);

        case TrackType::ClapGhostSnare:
            if (step == 4 || step == 12)
                return std::clamp(static_cast<int>(std::round(static_cast<float>(offbeatDelayTicks) * 0.34f)) + 10 + tinyDrift,
                                  16,
                                  32);
            return std::clamp(offbeatDelayTicks / 3 + smallDrift, 2, 18);

        case TrackType::Kick:
            if (step == 0)
                return std::clamp(tinyDrift, -3, 4);
            if (step == 8)
                return std::clamp(2 + tinyDrift, -1, 8);
            if (phase == 2)
                return std::clamp(static_cast<int>(std::round(static_cast<float>(offbeatDelayTicks) * 0.56f)) + smallDrift,
                                  14,
                                  34);
            if (step == 3 || step == 7 || step == 11 || step == 15)
                return std::clamp(-3 + smallDrift, -10, 8);
            return std::clamp(offbeatDelayTicks / 3 + smallDrift, -4, 22);

        case TrackType::GhostKick:
            if (step == 3 || step == 7 || step == 11 || step == 15)
                return std::clamp(-5 + smallDrift, -12, 8);
            return std::clamp(offbeatDelayTicks / 3 + smallDrift, -4, 18);

        case TrackType::OpenHat:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + 4 + smallDrift, 32, 58);
            return std::clamp(offbeatDelayTicks / 2 + 8 + smallDrift, 14, 36);

        case TrackType::Perc:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + tinyDrift, 28, 54);
            if (step == 3 || step == 7 || step == 11 || step == 15)
                return std::clamp(offbeatDelayTicks / 2 + 5 + smallDrift, 10, 34);
            return std::clamp(7 + smallDrift, -2, 22);

        default:
            break;
    }

    return note.microOffset;
}

int dustyPriority(TrackType type, const NoteEvent& note)
{
    const int step = normalizedStepInBar(note.step);
    int score = note.velocity;

    switch (type)
    {
        case TrackType::Kick:
            if (step == 0)
                score += 260;
            else if (step == 8 || step == 10)
                score += 150;
            else if (step == 3 || step == 7 || step == 11 || step == 14 || step == 15)
                score += 80;
            break;
        case TrackType::GhostKick:
            if (step == 3 || step == 7 || step == 11 || step == 15)
                score += 100;
            break;
        case TrackType::ClapGhostSnare:
            if (step == 4 || step == 12)
                score += 180;
            else if (step == 3 || step == 11 || step == 15)
                score += 70;
            break;
        case TrackType::OpenHat:
            if (step == 14 || step == 15)
                score += 120;
            else if (step == 6 || step == 10)
                score += 60;
            break;
        case TrackType::Perc:
            if (step == 10 || step == 14 || step == 7 || step == 15)
                score += 90;
            break;
        case TrackType::Ride:
            if ((step % 2) == 0)
                score += 120;
            break;
        default:
            break;
    }

    return score;
}

void pruneDustyBarLimit(TrackState& track, int bars, int maxPerBar, int endingMaxPerBar)
{
    std::vector<NoteEvent> filtered;
    filtered.reserve(track.notes.size());

    for (int bar = 0; bar < bars; ++bar)
    {
        std::vector<NoteEvent> barNotes;
        for (const auto& note : track.notes)
        {
            if (note.step / 16 == bar)
                barNotes.push_back(note);
        }

        std::stable_sort(barNotes.begin(), barNotes.end(), [&track](const NoteEvent& left, const NoteEvent& right)
        {
            const int leftScore = dustyPriority(track.type, left);
            const int rightScore = dustyPriority(track.type, right);
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
    dedupeAndSortNotes(track.notes);
}

void shapeDustyVelocity(TrackState& track, const BoomBapStyleProfile& style)
{
    for (auto& note : track.notes)
    {
        const int step = normalizedStepInBar(note.step);
        const int phase = step % 4;

        switch (track.type)
        {
            case TrackType::HiHat:
                if (phase == 0)
                    note.velocity += 4;
                else if (phase == 2)
                    note.velocity -= 2;
                else
                    note.velocity -= 8;
                note.velocity = std::clamp(note.velocity, style.hatVelocityMin, style.hatVelocityMax);
                break;
            case TrackType::Ride:
                note.velocity = std::clamp(note.velocity - 12, std::max(38, style.hatVelocityMin - 12), std::min(88, style.hatVelocityMax - 6));
                break;
            case TrackType::Kick:
                if (step == 0 || step == 8)
                    note.velocity += 5;
                else
                    note.velocity -= 4;
                note.velocity = std::clamp(note.velocity, style.kickVelocityMin, style.kickVelocityMax);
                break;
            case TrackType::GhostKick:
                note.velocity = std::clamp(note.velocity - 6, style.ghostVelocityMin, style.ghostVelocityMax);
                break;
            case TrackType::Snare:
                if (!note.isGhost && (step == 4 || step == 12))
                    note.velocity += 3;
                note.velocity = std::clamp(note.velocity, note.isGhost ? style.ghostVelocityMin : style.snareVelocityMin, note.isGhost ? style.ghostVelocityMax : style.snareVelocityMax);
                break;
            case TrackType::ClapGhostSnare:
                note.velocity = std::clamp(note.velocity - (note.isGhost ? 4 : 8), style.ghostVelocityMin, style.clapVelocityMax);
                break;
            case TrackType::OpenHat:
                note.velocity = std::clamp(note.velocity - 4, style.openHatVelocityMin, style.openHatVelocityMax);
                break;
            case TrackType::Perc:
                note.velocity = std::clamp(note.velocity - 5, style.percVelocityMin, style.percVelocityMax);
                break;
            default:
                break;
        }
    }
}

void reinforceDustyHatCarrier(TrackState& hat,
                              const PatternProject& project,
                              const BoomBapStyleProfile& style,
                              int offbeatDelayTicks)
{
    static constexpr std::array<int, 8> kDustyCarrierSteps { 0, 2, 4, 6, 8, 10, 12, 14 };
    static constexpr std::array<int, 4> kDustyOffbeatSteps { 2, 6, 10, 14 };

    const auto* info = TrackRegistry::find(TrackType::HiHat);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;
    const int bars = std::max(1, project.params.bars);

    for (int bar = 0; bar < bars; ++bar)
    {
        auto addHatAt = [&](int stepInBar)
        {
            const int step = bar * 16 + stepInBar;
            if (containsStep(hat.notes, step))
                return false;

            NoteEvent note;
            note.pitch = pitch;
            note.step = step;
            note.length = 1;
            note.velocity = std::clamp(style.hatVelocityMin + ((stepInBar % 4) == 0 ? 24 : 16), style.hatVelocityMin, style.hatVelocityMax);
            note.microOffset = dustyPocketOffsetFor(TrackType::HiHat, note, project, style, offbeatDelayTicks);
            note.isGhost = false;
            hat.notes.push_back(note);
            return true;
        };

        int offbeatCount = static_cast<int>(std::count_if(hat.notes.begin(), hat.notes.end(), [bar](const NoteEvent& note)
        {
            const int step = normalizedStepInBar(note.step);
            return note.step / 16 == bar && (step == 2 || step == 6 || step == 10 || step == 14);
        }));

        for (const int stepInBar : kDustyOffbeatSteps)
        {
            if (offbeatCount >= 3)
                break;

            if (addHatAt(stepInBar))
                ++offbeatCount;
        }

        int count = static_cast<int>(std::count_if(hat.notes.begin(), hat.notes.end(), [bar](const NoteEvent& note)
        {
            return note.step / 16 == bar && (normalizedStepInBar(note.step) % 2) == 0;
        }));

        for (const int stepInBar : kDustyCarrierSteps)
        {
            if (count >= 6)
                break;

            if (addHatAt(stepInBar))
                ++count;
        }
    }

    dedupeAndSortNotes(hat.notes);
}

void applyDustyPocketRules(PatternProject& project,
                           const BoomBapStyleProfile& style,
                           const std::unordered_set<TrackType>& mutableTracks)
{
    const int bars = std::max(1, project.params.bars);
    const int offbeatDelayTicks = dustyOffbeatDelayTicks(project, style);

    if (auto* hat = findTrack(project, TrackType::HiHat);
        hat != nullptr && hat->enabled && !hat->locked && mutableTracks.count(hat->type) != 0)
    {
        reinforceDustyHatCarrier(*hat, project, style, offbeatDelayTicks);
    }

    for (auto& track : project.tracks)
    {
        if (track.locked || mutableTracks.count(track.type) == 0)
            continue;

        for (auto& note : track.notes)
            note.microOffset = dustyPocketOffsetFor(track.type, note, project, style, offbeatDelayTicks);

        shapeDustyVelocity(track, style);
        dedupeAndSortNotes(track.notes);
    }

    if (auto* hat = findTrack(project, TrackType::HiHat);
        hat != nullptr && hat->enabled && !hat->locked && mutableTracks.count(hat->type) != 0)
    {
        reinforceDustyHatCarrier(*hat, project, style, offbeatDelayTicks);
    }

    if (auto* kick = findTrack(project, TrackType::Kick);
        kick != nullptr && !kick->locked && mutableTracks.count(kick->type) != 0)
    {
        kick->notes.erase(std::remove_if(kick->notes.begin(), kick->notes.end(), [](const NoteEvent& note)
        {
            const int step = normalizedStepInBar(note.step);
            return step == 4 || step == 12;
        }), kick->notes.end());

        for (int bar = 0; bar < bars; ++bar)
        {
            const int anchor = bar * 16;
            if (!containsStep(kick->notes, anchor))
            {
                NoteEvent note;
                note.pitch = 36;
                note.step = anchor;
                note.length = 1;
                note.velocity = std::clamp(style.kickVelocityMin + 10, style.kickVelocityMin, style.kickVelocityMax);
                note.microOffset = dustyPocketOffsetFor(TrackType::Kick, note, project, style, offbeatDelayTicks);
                note.isGhost = false;
                kick->notes.push_back(note);
            }
        }
        dedupeAndSortNotes(kick->notes);
        pruneDustyBarLimit(*kick, bars, 4, 5);
    }

    if (auto* ghostKick = findTrack(project, TrackType::GhostKick);
        ghostKick != nullptr && !ghostKick->locked && mutableTracks.count(ghostKick->type) != 0)
    {
        pruneDustyBarLimit(*ghostKick, bars, 1, 2);
    }

    if (auto* clap = findTrack(project, TrackType::ClapGhostSnare);
        clap != nullptr && !clap->locked && mutableTracks.count(clap->type) != 0)
    {
        pruneDustyBarLimit(*clap, bars, 2, 2);
    }

    if (auto* openHat = findTrack(project, TrackType::OpenHat);
        openHat != nullptr && !openHat->locked && mutableTracks.count(openHat->type) != 0)
    {
        pruneDustyBarLimit(*openHat, bars, 1, 2);
    }

    if (auto* perc = findTrack(project, TrackType::Perc);
        perc != nullptr && !perc->locked && mutableTracks.count(perc->type) != 0)
    {
        pruneDustyBarLimit(*perc, bars, 1, 2);
    }

    if (auto* ride = findTrack(project, TrackType::Ride);
        ride != nullptr && !ride->locked && mutableTracks.count(ride->type) != 0)
    {
        pruneDustyBarLimit(*ride, bars, 6, 7);
    }
}

float jazzySwingRatio(const PatternProject& project, const BoomBapStyleProfile& style)
{
    const float bpm = std::clamp(project.params.bpm > 0.0f ? project.params.bpm : interpretedReferenceTempo(style),
                                 72.0f,
                                 180.0f);
    const float slowAmount = std::clamp((128.0f - bpm) / 56.0f, 0.0f, 1.0f);
    const float requestedSwing = std::clamp(project.params.swingPercent, 58.0f, 66.0f);
    const float styleSwing = std::clamp(style.swingPercent, 59.0f, 66.0f);
    const float swingLift = ((requestedSwing * 0.45f + styleSwing * 0.55f) - 62.0f) * 0.035f;
    return std::clamp(1.55f + slowAmount * 0.62f + swingLift, 1.50f, 2.24f);
}

int jazzyOffbeatDelayTicks(const PatternProject& project, const BoomBapStyleProfile& style)
{
    const float ratio = jazzySwingRatio(project, style);
    const float offbeatTick = 960.0f * ratio / (1.0f + ratio);
    return std::clamp(static_cast<int>(std::round(offbeatTick - 480.0f)), 88, 178);
}

float deterministicJazzyUnit(int seed, int bar, int step, int salt)
{
    unsigned int x = static_cast<unsigned int>(seed * 2654435761u
                                               + bar * 2246822519u
                                               + step * 3266489917u
                                               + salt * 668265263u);
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

int deterministicJazzyDrift(int seed, int bar, int step, int salt, int spread)
{
    return deterministicDustyDrift(seed + 97, bar, step, salt + 173, spread);
}

int jazzyPocketOffsetFor(TrackType type,
                         const NoteEvent& note,
                         const PatternProject& project,
                         const BoomBapStyleProfile& style,
                         int offbeatDelayTicks)
{
    juce::ignoreUnused(style);

    const int step = normalizedStepInBar(note.step);
    const int bar = std::max(0, note.step / 16);
    const int phase = step % 4;
    const int smallDrift = deterministicJazzyDrift(project.params.seed, bar, step, static_cast<int>(type) + 41, 4);
    const int tinyDrift = deterministicJazzyDrift(project.params.seed, bar, step, static_cast<int>(type) + 67, 2);

    switch (type)
    {
        case TrackType::Ride:
            if (phase == 0)
                return std::clamp(4 + tinyDrift, -2, 10);
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + smallDrift, 96, 188);
            return std::clamp(offbeatDelayTicks / 2 + smallDrift, 32, 96);

        case TrackType::HiHat:
            if (step == 4 || step == 12)
                return std::clamp(5 + tinyDrift, -1, 12);
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + smallDrift, 92, 184);
            return std::clamp(8 + smallDrift, -4, 28);

        case TrackType::Kick:
            if (step == 0 || step == 4 || step == 8 || step == 12)
                return std::clamp(2 + tinyDrift, -3, 8);
            if (phase == 2)
                return std::clamp(offbeatDelayTicks - 18 + smallDrift, 74, 164);
            return std::clamp(8 + smallDrift, -8, 36);

        case TrackType::Snare:
            if (!note.isGhost && (step == 4 || step == 12))
                return std::clamp(7 + tinyDrift, 2, 14);
            if (phase == 2)
                return std::clamp(offbeatDelayTicks - 10 + smallDrift, 82, 174);
            if (step == 3 || step == 7 || step == 11 || step == 15)
                return std::clamp(-4 + smallDrift, -14, 10);
            return std::clamp(10 + smallDrift, -6, 34);

        case TrackType::ClapGhostSnare:
            if (step == 12)
                return std::clamp(9 + tinyDrift, 3, 16);
            if (phase == 2)
                return std::clamp(offbeatDelayTicks - 6 + smallDrift, 88, 178);
            return std::clamp(9 + smallDrift, -4, 34);

        case TrackType::GhostKick:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks - 20 + smallDrift, 70, 160);
            return std::clamp(2 + smallDrift, -12, 22);

        case TrackType::OpenHat:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + 6 + smallDrift, 104, 196);
            return std::clamp(18 + smallDrift, 4, 48);

        case TrackType::Perc:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks - 6 + smallDrift, 84, 178);
            if (step == 3 || step == 7 || step == 11 || step == 15)
                return std::clamp(-2 + smallDrift, -12, 18);
            return std::clamp(12 + smallDrift, -4, 42);

        default:
            break;
    }

    return note.microOffset;
}

NoteEvent* findNoteAtStep(TrackState& track, int step)
{
    auto it = std::find_if(track.notes.begin(), track.notes.end(), [step](const NoteEvent& note)
    {
        return note.step == step;
    });
    return it != track.notes.end() ? &(*it) : nullptr;
}

void upsertJazzyNote(TrackState& track,
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

int jazzyPriority(TrackType type, const NoteEvent& note)
{
    const int step = normalizedStepInBar(note.step);
    int score = note.velocity;

    switch (type)
    {
        case TrackType::Ride:
            if (step == 0 || step == 4 || step == 6 || step == 8 || step == 12 || step == 14)
                score += 260;
            break;
        case TrackType::HiHat:
            if (step == 0 || step == 4 || step == 6 || step == 8 || step == 12 || step == 14)
                score += 260;
            break;
        case TrackType::Cymbal:
            if (step == 4 || step == 12)
                score += 260;
            else if (step == 6 || step == 14)
                score += 90;
            break;
        case TrackType::Kick:
            if (step == 0 || step == 4 || step == 8 || step == 12)
                score += 220;
            else if (step == 10 || step == 14 || step == 7 || step == 11)
                score += 90;
            break;
        case TrackType::Snare:
            if (!note.isGhost && (step == 4 || step == 12))
                score += 240;
            else if (step == 6 || step == 10 || step == 14)
                score += 120;
            else if (step == 3 || step == 7 || step == 11 || step == 15)
                score += 80;
            break;
        case TrackType::ClapGhostSnare:
        case TrackType::Perc:
            if (step == 6 || step == 10 || step == 14)
                score += 120;
            else if (step == 3 || step == 7 || step == 11 || step == 15)
                score += 80;
            break;
        default:
            break;
    }

    return score;
}

void pruneJazzyBarLimit(TrackState& track, int bars, int maxPerBar, int endingMaxPerBar)
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
            const int leftScore = jazzyPriority(track.type, left);
            const int rightScore = jazzyPriority(track.type, right);
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
    dedupeAndSortNotes(track.notes);
}

void reinforceJazzyRidePattern(TrackState& ride,
                               const PatternProject& project,
                               const BoomBapStyleProfile& style,
                               int offbeatDelayTicks)
{
    static constexpr std::array<int, 6> kRideSteps { 0, 4, 6, 8, 12, 14 };
    const auto* info = TrackRegistry::find(TrackType::Ride);
    const int pitch = info != nullptr ? info->defaultMidiNote : 51;
    const int bars = std::max(1, project.params.bars);

    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : kRideSteps)
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool skip = stepInBar == 6 || stepInBar == 14;
            const bool twoFour = stepInBar == 4 || stepInBar == 12;
            note.velocity = std::clamp(style.hatVelocityMin + (twoFour ? 34 : (skip ? 20 : 28)),
                                       style.hatVelocityMin,
                                       std::min(112, style.hatVelocityMax + 12));
            note.microOffset = jazzyPocketOffsetFor(TrackType::Ride, note, project, style, offbeatDelayTicks);
            note.isGhost = false;
            upsertJazzyNote(ride, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    dedupeAndSortNotes(ride.notes);
}

void reinforceJazzyHatCymbalPattern(TrackState& hat,
                                    const PatternProject& project,
                                    const BoomBapStyleProfile& style,
                                    int offbeatDelayTicks)
{
    static constexpr std::array<int, 6> kHatCymbalSteps { 0, 4, 6, 8, 12, 14 };
    const auto* info = TrackRegistry::find(TrackType::HiHat);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;
    const int bars = std::max(1, project.params.bars);

    hat.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : kHatCymbalSteps)
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool skip = stepInBar == 6 || stepInBar == 14;
            const bool twoFour = stepInBar == 4 || stepInBar == 12;
            note.velocity = std::clamp(style.hatVelocityMin + (twoFour ? 28 : (skip ? 15 : 22)),
                                       style.hatVelocityMin,
                                       std::min(108, style.hatVelocityMax + 8));
            note.microOffset = jazzyPocketOffsetFor(TrackType::Ride, note, project, style, offbeatDelayTicks);
            note.isGhost = false;
            upsertJazzyNote(hat, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneJazzyBarLimit(hat, bars, 6, 6);
}

void shapeJazzyCymbalQuietHatFoot(TrackState& cymbal,
                                  const PatternProject& project,
                                  const BoomBapStyleProfile& style,
                                  int offbeatDelayTicks)
{
    const auto* info = TrackRegistry::find(TrackType::Cymbal);
    const int pitch = info != nullptr ? info->defaultMidiNote : 49;
    const int bars = std::max(1, project.params.bars);

    std::vector<NoteEvent> filtered;
    filtered.reserve(cymbal.notes.size());
    for (const auto& note : cymbal.notes)
    {
        const int step = normalizedStepInBar(note.step);
        const int bar = std::max(0, note.step / 16);
        const bool foot = step == 4 || step == 12;
        const bool lightBrush = (step == 6 || step == 14)
            && deterministicJazzyUnit(project.params.seed, bar, step, 211) < 0.18f;
        if (!foot && !lightBrush)
            continue;

        auto shaped = note;
        shaped.pitch = pitch;
        shaped.velocity = std::clamp(foot ? (step == 12 ? 38 : 34) : 25, 20, 44);
        shaped.microOffset = jazzyPocketOffsetFor(TrackType::HiHat, shaped, project, style, offbeatDelayTicks);
        shaped.isGhost = false;
        filtered.push_back(shaped);
    }
    cymbal.notes = std::move(filtered);

    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : { 4, 12 })
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = stepInBar == 12 ? 38 : 34;
            note.microOffset = jazzyPocketOffsetFor(TrackType::HiHat, note, project, style, offbeatDelayTicks);
            note.isGhost = false;
            upsertJazzyNote(cymbal, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneJazzyBarLimit(cymbal, bars, 3, 4);
}

void shapeJazzyKickFeather(TrackState& kick,
                           const PatternProject& project,
                           const BoomBapStyleProfile& style,
                           int offbeatDelayTicks)
{
    const auto* info = TrackRegistry::find(TrackType::Kick);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;
    const int bars = std::max(1, project.params.bars);
    std::vector<NoteEvent> comping;
    comping.reserve(kick.notes.size());

    for (int bar = 0; bar < bars; ++bar)
    {
        std::vector<NoteEvent> barComp;
        for (auto note : kick.notes)
        {
            if (note.step / 16 != bar)
                continue;

            const int step = normalizedStepInBar(note.step);
            if (step == 0 || step == 4 || step == 8 || step == 12)
                continue;

            note.pitch = pitch;
            note.velocity = std::clamp(style.kickVelocityMin + 20 + (jazzyPriority(TrackType::Kick, note) % 8),
                                       style.kickVelocityMin + 8,
                                       std::min(style.kickVelocityMax, 86));
            note.microOffset = jazzyPocketOffsetFor(TrackType::Kick, note, project, style, offbeatDelayTicks);
            note.isGhost = false;
            barComp.push_back(note);
        }

        std::stable_sort(barComp.begin(), barComp.end(), [](const NoteEvent& left, const NoteEvent& right)
        {
            const int leftScore = jazzyPriority(TrackType::Kick, left);
            const int rightScore = jazzyPriority(TrackType::Kick, right);
            if (leftScore != rightScore)
                return leftScore > rightScore;
            return left.step < right.step;
        });

        const int compLimit = bar == bars - 1 ? 2 : 1;
        if (static_cast<int>(barComp.size()) > compLimit)
            barComp.resize(static_cast<size_t>(compLimit));

        comping.insert(comping.end(), barComp.begin(), barComp.end());
    }

    kick.notes = std::move(comping);
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : { 0, 4, 8, 12 })
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.kickVelocityMin + (stepInBar == 0 || stepInBar == 8 ? 12 : 8),
                                       style.kickVelocityMin,
                                       std::min(style.kickVelocityMax, 66));
            note.microOffset = jazzyPocketOffsetFor(TrackType::Kick, note, project, style, offbeatDelayTicks);
            note.isGhost = false;
            upsertJazzyNote(kick, note.pitch, note.step, note.velocity, note.microOffset, false);
        }

        const float compChance = deterministicJazzyUnit(project.params.seed, bar, 0, 421);
        if (compChance < 0.72f || bar == bars - 1)
        {
            static constexpr std::array<std::array<int, 2>, 5> kKickCompMotifs {{
                {{ 10, -1 }},
                {{ 14, -1 }},
                {{ 7, -1 }},
                {{ 10, 14 }},
                {{ 15, -1 }}
            }};

            const int motifIndex = static_cast<int>(deterministicJazzyUnit(project.params.seed, bar, 0, 423) * static_cast<float>(kKickCompMotifs.size()))
                % static_cast<int>(kKickCompMotifs.size());
            const auto& motif = kKickCompMotifs[static_cast<size_t>(motifIndex)];
            const int motifLimit = bar == bars - 1 ? 2 : 1;
            int added = 0;
            for (const int stepInBar : motif)
            {
                if (stepInBar < 0 || added >= motifLimit)
                    continue;

                NoteEvent note;
                note.pitch = pitch;
                note.step = bar * 16 + stepInBar;
                note.length = 1;
                note.velocity = std::clamp(style.kickVelocityMin + 17 + static_cast<int>(deterministicJazzyUnit(project.params.seed, bar, stepInBar, 427) * 9.0f),
                                           style.kickVelocityMin + 10,
                                           std::min(style.kickVelocityMax, 76));
                note.microOffset = jazzyPocketOffsetFor(TrackType::Kick, note, project, style, offbeatDelayTicks);
                note.isGhost = false;
                upsertJazzyNote(kick, note.pitch, note.step, note.velocity, note.microOffset, false);
                ++added;
            }
        }
    }

    pruneJazzyBarLimit(kick, bars, 5, 6);
}

void shapeJazzySnareComping(TrackState& snare,
                            const PatternProject& project,
                            const BoomBapStyleProfile& style,
                            int offbeatDelayTicks)
{
    static constexpr std::array<std::array<int, 3>, 6> kCompMotifs {{
        {{ 6, -1, -1 }},
        {{ 10, 14, -1 }},
        {{ 3, 10, -1 }},
        {{ 7, 11, -1 }},
        {{ 2, 6, 14 }},
        {{ 15, -1, -1 }}
    }};

    const auto* info = TrackRegistry::find(TrackType::Snare);
    const int pitch = info != nullptr ? info->defaultMidiNote : 38;
    const int bars = std::max(1, project.params.bars);

    for (auto& note : snare.notes)
    {
        const int step = normalizedStepInBar(note.step);
        note.pitch = pitch;
        if (!note.isGhost && (step == 4 || step == 12))
        {
            note.velocity = std::clamp(style.snareVelocityMin + (step == 12 ? 24 : 18),
                                       style.snareVelocityMin,
                                       std::min(style.snareVelocityMax, 100));
        }
        else
        {
            note.isGhost = true;
            note.velocity = std::clamp(note.velocity - 10, style.ghostVelocityMin, style.ghostVelocityMax);
        }
        note.microOffset = jazzyPocketOffsetFor(TrackType::Snare, note, project, style, offbeatDelayTicks);
    }

    for (int bar = 0; bar < bars; ++bar)
    {
        const int motifIndex = static_cast<int>(deterministicJazzyUnit(project.params.seed, bar, 0, 307) * static_cast<float>(kCompMotifs.size())) % static_cast<int>(kCompMotifs.size());
        const auto& motif = kCompMotifs[static_cast<size_t>(motifIndex)];

        for (const int stepInBar : motif)
        {
            if (stepInBar < 0)
                continue;
            if (stepInBar == 2 && deterministicJazzyUnit(project.params.seed, bar, stepInBar, 311) > 0.42f)
                continue;

            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.ghostVelocityMin + 12 + static_cast<int>(deterministicJazzyUnit(project.params.seed, bar, stepInBar, 313) * 16.0f),
                                       style.ghostVelocityMin,
                                       style.ghostVelocityMax);
            note.microOffset = jazzyPocketOffsetFor(TrackType::Snare, note, project, style, offbeatDelayTicks);
            note.isGhost = true;
            upsertJazzyNote(snare, note.pitch, note.step, note.velocity, note.microOffset, true);
        }
    }

    pruneJazzyBarLimit(snare, bars, 5, 6);
}

void shapeJazzySupportTrack(TrackState& track,
                            const PatternProject& project,
                            const BoomBapStyleProfile& style,
                            int offbeatDelayTicks)
{
    for (auto& note : track.notes)
    {
        const int step = normalizedStepInBar(note.step);
        note.microOffset = jazzyPocketOffsetFor(track.type, note, project, style, offbeatDelayTicks);
        switch (track.type)
        {
            case TrackType::ClapGhostSnare:
                note.isGhost = true;
                note.velocity = std::clamp(note.velocity - 16, style.ghostVelocityMin, std::min(style.clapVelocityMax, 78));
                break;
            case TrackType::GhostKick:
                note.velocity = std::clamp(note.velocity - 10, style.ghostVelocityMin, style.ghostVelocityMax);
                break;
            case TrackType::OpenHat:
                if (!(step == 14 || step == 15 || step == 6 || step == 10))
                    note.step = -1;
                else
                    note.velocity = std::clamp(note.velocity - 10, style.openHatVelocityMin, style.openHatVelocityMax);
                break;
            case TrackType::Perc:
                note.velocity = std::clamp(note.velocity - 8, style.percVelocityMin, style.percVelocityMax);
                break;
            case TrackType::Cymbal:
                note.velocity = std::clamp(note.velocity - 34, 20, 44);
                break;
            case TrackType::Ride:
                note.velocity = std::clamp(note.velocity, style.hatVelocityMin, std::min(112, style.hatVelocityMax + 12));
                break;
            default:
                break;
        }
    }

    track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [](const NoteEvent& note)
    {
        return note.step < 0;
    }), track.notes.end());

    const int bars = std::max(1, project.params.bars);
    switch (track.type)
    {
        case TrackType::ClapGhostSnare: pruneJazzyBarLimit(track, bars, 2, 3); break;
        case TrackType::GhostKick: pruneJazzyBarLimit(track, bars, 1, 2); break;
        case TrackType::OpenHat: pruneJazzyBarLimit(track, bars, 1, 2); break;
        case TrackType::Perc: pruneJazzyBarLimit(track, bars, 2, 3); break;
        case TrackType::Cymbal: pruneJazzyBarLimit(track, bars, 3, 4); break;
        case TrackType::Ride: pruneJazzyBarLimit(track, bars, 7, 8); break;
        default: dedupeAndSortNotes(track.notes); break;
    }
}

void applyJazzyPocketRules(PatternProject& project,
                           const BoomBapStyleProfile& style,
                           const std::unordered_set<TrackType>& mutableTracks)
{
    const int offbeatDelayTicks = jazzyOffbeatDelayTicks(project, style);

    if (auto* ride = findTrack(project, TrackType::Ride);
        ride != nullptr && ride->enabled && !ride->locked && mutableTracks.count(ride->type) != 0)
    {
        ride->notes.clear();
    }

    if (auto* hat = findTrack(project, TrackType::HiHat);
        hat != nullptr && hat->enabled && !hat->locked && mutableTracks.count(hat->type) != 0)
    {
        reinforceJazzyHatCymbalPattern(*hat, project, style, offbeatDelayTicks);
    }

    if (auto* cymbal = findTrack(project, TrackType::Cymbal);
        cymbal != nullptr && cymbal->enabled && !cymbal->locked && mutableTracks.count(cymbal->type) != 0)
    {
        shapeJazzyCymbalQuietHatFoot(*cymbal, project, style, offbeatDelayTicks);
    }

    if (auto* kick = findTrack(project, TrackType::Kick);
        kick != nullptr && kick->enabled && !kick->locked && mutableTracks.count(kick->type) != 0)
    {
        shapeJazzyKickFeather(*kick, project, style, offbeatDelayTicks);
    }

    if (auto* snare = findTrack(project, TrackType::Snare);
        snare != nullptr && snare->enabled && !snare->locked && mutableTracks.count(snare->type) != 0)
    {
        shapeJazzySnareComping(*snare, project, style, offbeatDelayTicks);
    }

    for (auto& track : project.tracks)
    {
        if (track.locked || mutableTracks.count(track.type) == 0)
            continue;

        if (track.type == TrackType::ClapGhostSnare
            || track.type == TrackType::GhostKick
            || track.type == TrackType::OpenHat
            || track.type == TrackType::Perc)
        {
            shapeJazzySupportTrack(track, project, style, offbeatDelayTicks);
        }
    }
}

int goldOffbeatDelayTicks(const PatternProject& project, const BoomBapStyleProfile& style)
{
    const float requested = std::clamp(project.params.swingPercent, 56.0f, 61.5f);
    const float profiled = std::clamp(style.swingPercent, 56.0f, 61.5f);
    const float swingPoint = requested * 0.40f + profiled * 0.60f;
    const float delayedEighthTicks = 480.0f * (swingPoint / 100.0f - 0.5f);
    return std::clamp(static_cast<int>(std::round(delayedEighthTicks)), 28, 56);
}

int goldPocketOffsetFor(TrackType type,
                        const NoteEvent& note,
                        const PatternProject& project,
                        int offbeatDelayTicks)
{
    const int step = normalizedStepInBar(note.step);
    const int bar = std::max(0, note.step / 16);
    const int phase = step % 4;
    const int drift = deterministicDustyDrift(project.params.seed + 313, bar, step, static_cast<int>(type) + 97, 3);
    const int tiny = deterministicDustyDrift(project.params.seed + 337, bar, step, static_cast<int>(type) + 101, 2);

    switch (type)
    {
        case TrackType::HiHat:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + drift, 28, 58);
            return std::clamp(3 + tiny, -2, 9);

        case TrackType::Kick:
            if (step == 0 || step == 8)
                return std::clamp(-1 + tiny, -6, 6);
            if (step == 10 || step == 11 || step == 14 || step == 15)
                return std::clamp(6 + drift, -2, 18);
            return std::clamp(2 + drift, -8, 12);

        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            if (!note.isGhost && (step == 4 || step == 12))
                return std::clamp((step == 12 ? 11 : 9) + tiny, 6, 16);
            if (step == 3 || step == 11)
                return std::clamp(-2 + drift, -10, 8);
            return std::clamp(6 + drift, -4, 18);

        case TrackType::OpenHat:
        case TrackType::Perc:
            if (phase == 2)
                return std::clamp(offbeatDelayTicks + drift, 28, 60);
            return std::clamp(4 + drift, -4, 18);

        default:
            break;
    }

    return note.microOffset;
}

void upsertGoldNote(TrackState& track,
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

int goldPriority(TrackType type, const NoteEvent& note)
{
    const int step = normalizedStepInBar(note.step);
    int score = note.velocity;

    switch (type)
    {
        case TrackType::HiHat:
            if ((step % 2) == 0)
                score += 180;
            if (step == 0 || step == 8)
                score += 50;
            break;
        case TrackType::Kick:
            if (step == 0)
                score += 260;
            else if (step == 10 || step == 11)
                score += 190;
            else if (step == 8)
                score += 150;
            else if (step == 3 || step == 6 || step == 14)
                score += 100;
            break;
        case TrackType::Snare:
            if (!note.isGhost && (step == 4 || step == 12))
                score += 260;
            else if (step == 3 || step == 10 || step == 11 || step == 14)
                score += 80;
            break;
        case TrackType::ClapGhostSnare:
            if (step == 4 || step == 12)
                score += 180;
            break;
        default:
            break;
    }

    return score;
}

void pruneGoldBarLimit(TrackState& track, int bars, int maxPerBar, int endingMaxPerBar)
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
            const int leftScore = goldPriority(track.type, left);
            const int rightScore = goldPriority(track.type, right);
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
    dedupeAndSortNotes(track.notes);
}

void shapeGoldHatCarrier(TrackState& hat,
                         const PatternProject& project,
                         const BoomBapStyleProfile& style,
                         int offbeatDelayTicks)
{
    static constexpr std::array<int, 8> kEighthSteps { 0, 2, 4, 6, 8, 10, 12, 14 };
    const auto* info = TrackRegistry::find(TrackType::HiHat);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;
    const int bars = std::max(1, project.params.bars);

    hat.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : kEighthSteps)
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool down = stepInBar == 0 || stepInBar == 8;
            const bool backbeat = stepInBar == 4 || stepInBar == 12;
            const bool offbeat = (stepInBar % 4) == 2;
            note.velocity = std::clamp(style.hatVelocityMin + (down ? 34 : backbeat ? 29 : offbeat ? 17 : 22),
                                       style.hatVelocityMin,
                                       style.hatVelocityMax);
            note.microOffset = goldPocketOffsetFor(TrackType::HiHat, note, project, offbeatDelayTicks);
            note.isGhost = false;
            upsertGoldNote(hat, note.pitch, note.step, note.velocity, note.microOffset, false);
        }

        if (deterministicJazzyUnit(project.params.seed, bar, 7, 509) < (bar == bars - 1 ? 0.42f : 0.22f))
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + 7;
            note.length = 1;
            note.velocity = std::clamp(style.hatVelocityMin + 8, style.hatVelocityMin, style.hatVelocityMax);
            note.microOffset = std::clamp(offbeatDelayTicks / 2 + 4, 12, 34);
            note.isGhost = false;
            upsertGoldNote(hat, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneGoldBarLimit(hat, bars, 9, 9);
}

void shapeGoldKickPocket(TrackState& kick,
                         const PatternProject& project,
                         const BoomBapStyleProfile& style,
                         int offbeatDelayTicks)
{
    static constexpr std::array<std::array<int, 5>, 6> kGoldKickMotifs {{
        {{ 0, 3, 10, 11, -1 }},
        {{ 0, 6, 8, 10, -1 }},
        {{ 0, 3, 8, 11, 14 }},
        {{ 0, 5, 10, 11, -1 }},
        {{ 0, 2, 8, 10, 14 }},
        {{ 0, 7, 10, 11, -1 }}
    }};

    const auto* info = TrackRegistry::find(TrackType::Kick);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;
    const int bars = std::max(1, project.params.bars);

    kick.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const int motifIndex = static_cast<int>(deterministicJazzyUnit(project.params.seed, bar, 0, 521) * static_cast<float>(kGoldKickMotifs.size()))
            % static_cast<int>(kGoldKickMotifs.size());
        const auto& motif = kGoldKickMotifs[static_cast<size_t>(motifIndex)];

        for (const int stepInBar : motif)
        {
            if (stepInBar < 0 || stepInBar == 4 || stepInBar == 12)
                continue;

            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            const bool root = stepInBar == 0;
            const bool secondAnchor = stepInBar == 8 || stepInBar == 10 || stepInBar == 11;
            note.velocity = std::clamp(style.kickVelocityMin + (root ? 22 : secondAnchor ? 14 : 8),
                                       style.kickVelocityMin,
                                       style.kickVelocityMax);
            note.microOffset = goldPocketOffsetFor(TrackType::Kick, note, project, offbeatDelayTicks);
            note.isGhost = false;
            upsertGoldNote(kick, note.pitch, note.step, note.velocity, note.microOffset, false);
        }
    }

    pruneGoldBarLimit(kick, bars, 5, 5);
}

void shapeGoldSnarePocket(TrackState& snare,
                          const PatternProject& project,
                          const BoomBapStyleProfile& style,
                          int offbeatDelayTicks)
{
    juce::ignoreUnused(offbeatDelayTicks);

    const auto* info = TrackRegistry::find(TrackType::Snare);
    const int pitch = info != nullptr ? info->defaultMidiNote : 38;
    const int bars = std::max(1, project.params.bars);

    snare.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int stepInBar : { 4, 12 })
        {
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.snareVelocityMin + (stepInBar == 12 ? 16 : 11), style.snareVelocityMin, style.snareVelocityMax);
            note.microOffset = goldPocketOffsetFor(TrackType::Snare, note, project, 0);
            note.isGhost = false;
            upsertGoldNote(snare, note.pitch, note.step, note.velocity, note.microOffset, false);
        }

        const float ghostPick = deterministicJazzyUnit(project.params.seed, bar, 0, 541);
        const int ghostStep = ghostPick < 0.34f ? 11 : (ghostPick < 0.58f ? 3 : (ghostPick < 0.78f ? 10 : -1));
        if (ghostStep >= 0 || bar == bars - 1)
        {
            const int stepInBar = ghostStep >= 0 ? ghostStep : 14;
            NoteEvent note;
            note.pitch = pitch;
            note.step = bar * 16 + stepInBar;
            note.length = 1;
            note.velocity = std::clamp(style.ghostVelocityMin + 7 + static_cast<int>(deterministicJazzyUnit(project.params.seed, bar, stepInBar, 547) * 9.0f),
                                       style.ghostVelocityMin,
                                       style.ghostVelocityMax);
            note.microOffset = goldPocketOffsetFor(TrackType::Snare, note, project, 0);
            note.isGhost = true;
            upsertGoldNote(snare, note.pitch, note.step, note.velocity, note.microOffset, true);
        }
    }

    pruneGoldBarLimit(snare, bars, 3, 4);
}

void shapeGoldClapLayer(TrackState& clap,
                        const PatternProject& project,
                        const BoomBapStyleProfile& style)
{
    const auto* info = TrackRegistry::find(TrackType::ClapGhostSnare);
    const int pitch = info != nullptr ? info->defaultMidiNote : 39;
    const int bars = std::max(1, project.params.bars);

    clap.notes.clear();
    for (int bar = 0; bar < bars; ++bar)
    {
        const float layerChance = deterministicJazzyUnit(project.params.seed, bar, 12, 557);
        if (layerChance > (bar == bars - 1 ? 0.62f : 0.42f))
            continue;

        const int stepInBar = layerChance < 0.24f ? 4 : 12;
        NoteEvent note;
        note.pitch = pitch;
        note.step = bar * 16 + stepInBar;
        note.length = 1;
        note.velocity = std::clamp(style.clapVelocityMin + (stepInBar == 12 ? 10 : 4), style.clapVelocityMin, style.clapVelocityMax);
        note.microOffset = goldPocketOffsetFor(TrackType::ClapGhostSnare, note, project, 0);
        note.isGhost = false;
        upsertGoldNote(clap, note.pitch, note.step, note.velocity, note.microOffset, false);
    }

    pruneGoldBarLimit(clap, bars, 1, 1);
}

void shapeGoldSupportTrack(TrackState& track,
                           const PatternProject& project,
                           const BoomBapStyleProfile& style,
                           int offbeatDelayTicks)
{
    for (auto& note : track.notes)
    {
        const int step = normalizedStepInBar(note.step);
        note.microOffset = goldPocketOffsetFor(track.type, note, project, offbeatDelayTicks);
        switch (track.type)
        {
            case TrackType::GhostKick:
                note.velocity = std::clamp(note.velocity - 12, style.ghostVelocityMin, style.ghostVelocityMax);
                break;
            case TrackType::OpenHat:
                if (!(step == 14 || step == 15))
                    note.step = -1;
                else
                    note.velocity = std::clamp(note.velocity - 12, style.openHatVelocityMin, style.openHatVelocityMax);
                break;
            case TrackType::Perc:
                if (!(step == 3 || step == 7 || step == 11 || step == 14))
                    note.step = -1;
                else
                    note.velocity = std::clamp(note.velocity - 10, style.percVelocityMin, style.percVelocityMax);
                break;
            case TrackType::Ride:
            case TrackType::Cymbal:
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

    const int bars = std::max(1, project.params.bars);
    switch (track.type)
    {
        case TrackType::GhostKick: pruneGoldBarLimit(track, bars, 1, 1); break;
        case TrackType::OpenHat: pruneGoldBarLimit(track, bars, 0, 1); break;
        case TrackType::Perc: pruneGoldBarLimit(track, bars, 1, 2); break;
        default: dedupeAndSortNotes(track.notes); break;
    }
}

void applyBoomBapGoldPocketRules(PatternProject& project,
                                 const BoomBapStyleProfile& style,
                                 const std::unordered_set<TrackType>& mutableTracks)
{
    const int offbeatDelayTicks = goldOffbeatDelayTicks(project, style);

    if (auto* hat = findTrack(project, TrackType::HiHat);
        hat != nullptr && hat->enabled && !hat->locked && mutableTracks.count(hat->type) != 0)
    {
        shapeGoldHatCarrier(*hat, project, style, offbeatDelayTicks);
    }

    if (auto* kick = findTrack(project, TrackType::Kick);
        kick != nullptr && kick->enabled && !kick->locked && mutableTracks.count(kick->type) != 0)
    {
        shapeGoldKickPocket(*kick, project, style, offbeatDelayTicks);
    }

    if (auto* snare = findTrack(project, TrackType::Snare);
        snare != nullptr && snare->enabled && !snare->locked && mutableTracks.count(snare->type) != 0)
    {
        shapeGoldSnarePocket(*snare, project, style, offbeatDelayTicks);
    }

    if (auto* clap = findTrack(project, TrackType::ClapGhostSnare);
        clap != nullptr && clap->enabled && !clap->locked && mutableTracks.count(clap->type) != 0)
    {
        shapeGoldClapLayer(*clap, project, style);
    }

    for (auto& track : project.tracks)
    {
        if (track.locked || mutableTracks.count(track.type) == 0)
            continue;

        if (track.type == TrackType::GhostKick
            || track.type == TrackType::OpenHat
            || track.type == TrackType::Perc
            || track.type == TrackType::Ride
            || track.type == TrackType::Cymbal)
        {
            shapeGoldSupportTrack(track, project, style, offbeatDelayTicks);
        }
    }
}
} // namespace

BoomBapEngine::BoomBapEngine() = default;

void BoomBapEngine::generate(PatternProject& project)
{
    applyBoomBapStyleInfluence(project);
    const auto& style = getBoomBapProfile(project.params.boombapSubstyle);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed));
    const auto grooveContext = buildGrooveContext(project, style, rng);
    const auto phrasePlan = BoomBapPhrasePlanner::createPlan(std::max(1, project.params.bars),
                                                              grooveContext.phraseVariationAmount,
                                                              style.substyle,
                                                              project.params.densityAmount,
                                                              rng);
    const auto blueprint = buildBoomBapGrooveBlueprint(project.params, style, phrasePlan, grooveContext.halfTimeReference, rng);
    const auto lanePlan = buildBoomBapLaneActivation(project.params, style, blueprint, rng);

    project.phraseLengthBars = std::max(1, project.params.bars);
    project.phraseRoleSummary = phraseSummaryString(phrasePlan);

    std::unordered_set<TrackType> mutableTracks;

    for (auto& track : project.tracks)
    {
        if (track.locked)
            continue;

        mutableTracks.insert(track.type);

        if (track.type == TrackType::Snare || track.type == TrackType::Kick || track.type == TrackType::HiHat)
            regenerateTrackInternal(project, track, style, phrasePlan, blueprint, rng);
        else
            track.notes.clear();

        track.templateId = static_cast<int>(style.substyle) * 100 + static_cast<int>(track.type) * 7;
        track.variationId = 0;
        track.mutationDepth = 0.0f;
        track.subProfile = style.name;
        track.laneRole = roleForTrack(track.type);
    }

    generateDependentTracks(project, style, phrasePlan, lanePlan, rng, mutableTracks);
    applyCarrierMode(project, style, phrasePlan, lanePlan, grooveContext, rng, mutableTracks);
    applyPhraseEndingAccents(project, style, rng, phrasePlan, mutableTracks);
    postProcess(project, style, blueprint, lanePlan, rng, mutableTracks);
    validatePattern(project, blueprint, lanePlan, mutableTracks);
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void BoomBapEngine::regenerateTrack(PatternProject& project, TrackType trackType)
{
    applyBoomBapStyleInfluence(project);
    regenerateTrackVariation(project, trackType);
}

void BoomBapEngine::generateTrackNew(PatternProject& project, TrackType trackType)
{
    applyBoomBapStyleInfluence(project);
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked)
        return;

    const auto& style = getBoomBapProfile(project.params.boombapSubstyle);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + static_cast<int>(trackType) * 131 + project.generationCounter * 17));
    const auto grooveContext = buildGrooveContext(project, style, rng);
    const auto phrasePlan = BoomBapPhrasePlanner::createPlan(std::max(1, project.params.bars),
                                                              grooveContext.phraseVariationAmount,
                                                              style.substyle,
                                                              project.params.densityAmount,
                                                              rng);
    const auto blueprint = buildBoomBapGrooveBlueprint(project.params, style, phrasePlan, grooveContext.halfTimeReference, rng);
    const auto lanePlan = buildBoomBapLaneActivation(project.params, style, blueprint, rng);

    project.phraseLengthBars = std::max(1, project.params.bars);
    project.phraseRoleSummary = phraseSummaryString(phrasePlan);

    regenerateTrackInternal(project, *track, style, phrasePlan, blueprint, rng);
    track->variationId = 0;
    track->mutationDepth = 0.0f;
    track->templateId = static_cast<int>(style.substyle) * 100 + static_cast<int>(track->type) * 7;
    track->subProfile = style.name;
    track->laneRole = roleForTrack(track->type);

    std::unordered_set<TrackType> mutableTracks { trackType };

    if (trackType == TrackType::Kick)
    {
        if (auto* ghostKick = findTrack(project, TrackType::GhostKick); ghostKick != nullptr && !ghostKick->locked && ghostKick->enabled)
        {
            ghostGenerator.generateGhostKick(*ghostKick, *track, style, project.styleInfluence, project.params.densityAmount, phrasePlan, rng);
            filterLaneNotesByBarActivation(*ghostKick, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useGhostKick; });

            if (grooveContext.halfTimeReference)
            {
                for (auto& n : ghostKick->notes)
                    n.microOffset = std::clamp(n.microOffset + 3, -120, 120);
            }
            mutableTracks.insert(ghostKick->type);
        }
    }
    else if (trackType == TrackType::Snare)
    {
        if (auto* clap = findTrack(project, TrackType::ClapGhostSnare); clap != nullptr && !clap->locked && clap->enabled)
        {
            ghostGenerator.generateClapLayer(*clap, *track, style, project.styleInfluence, phrasePlan, rng);
            filterLaneNotesByBarActivation(*clap, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useClapGhostSnare; });
            mutableTracks.insert(clap->type);
        }
    }
    else if (trackType == TrackType::HiHat)
    {
        if (auto* openHat = findTrack(project, TrackType::OpenHat); openHat != nullptr && !openHat->locked && openHat->enabled)
        {
            openHatGenerator.generate(*openHat, *track, project.params, style, project.styleInfluence, phrasePlan, rng);
            filterLaneNotesByBarActivation(*openHat, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useOpenHat; });
            mutableTracks.insert(openHat->type);
        }
    }

    applyPhraseEndingAccents(project, style, rng, phrasePlan, mutableTracks);
    applyCarrierMode(project, style, phrasePlan, lanePlan, grooveContext, rng, mutableTracks);
    postProcess(project, style, blueprint, lanePlan, rng, mutableTracks);
    validatePattern(project, blueprint, lanePlan, mutableTracks);
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void BoomBapEngine::regenerateTrackVariation(PatternProject& project, TrackType trackType)
{
    auto* target = findTrack(project, trackType);
    if (target == nullptr || target->locked)
        return;

    const auto previous = target->notes;

    generateTrackNew(project, trackType);

    target = findTrack(project, trackType);
    if (target == nullptr)
        return;

    const auto& style = getBoomBapProfile(project.params.boombapSubstyle);
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::BoomBap, project.params.boombapSubstyle);
    const auto& laneDefaults = getLaneStyleDefaults(styleDefaults, trackType);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + static_cast<int>(trackType) * 199 + project.generationCounter * 29));
    const auto grooveContext = buildGrooveContext(project, style, rng);
    const auto phrasePlan = BoomBapPhrasePlanner::createPlan(std::max(1, project.params.bars),
                                                              grooveContext.phraseVariationAmount,
                                                              style.substyle,
                                                              project.params.densityAmount,
                                                              rng);
    const auto blueprint = buildBoomBapGrooveBlueprint(project.params, style, phrasePlan, grooveContext.halfTimeReference, rng);
    const auto lanePlan = buildBoomBapLaneActivation(project.params, style, blueprint, rng);

    target->notes = mergeVariationNotes(trackType, previous, target->notes, laneDefaults.rgVariationIntensity, rng);
    target->variationId += 1;
    target->mutationDepth = std::clamp(target->mutationDepth + 0.08f, 0.0f, 1.0f);
    target->laneRole = roleForTrack(trackType);
    target->subProfile = style.name;

    std::unordered_set<TrackType> mutableTracks { trackType };

    if (trackType == TrackType::Snare)
    {
        if (auto* clap = findTrack(project, TrackType::ClapGhostSnare); clap != nullptr && !clap->locked && clap->enabled)
        {
            const auto oldClap = clap->notes;
            ghostGenerator.generateClapLayer(*clap, *target, style, project.styleInfluence, phrasePlan, rng);
            filterLaneNotesByBarActivation(*clap, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useClapGhostSnare; });
            const auto& clapDefaults = getLaneStyleDefaults(styleDefaults, clap->type);
            clap->notes = mergeVariationNotes(clap->type, oldClap, clap->notes, clapDefaults.rgVariationIntensity, rng);
            clap->variationId += 1;
            mutableTracks.insert(clap->type);
        }
    }

    if (trackType == TrackType::Kick)
    {
        if (auto* ghostKick = findTrack(project, TrackType::GhostKick); ghostKick != nullptr && !ghostKick->locked && ghostKick->enabled)
        {
            const auto oldGhost = ghostKick->notes;
            ghostGenerator.generateGhostKick(*ghostKick, *target, style, project.styleInfluence, project.params.densityAmount, phrasePlan, rng);
            filterLaneNotesByBarActivation(*ghostKick, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useGhostKick; });
            const auto& ghostDefaults = getLaneStyleDefaults(styleDefaults, ghostKick->type);
            ghostKick->notes = mergeVariationNotes(ghostKick->type, oldGhost, ghostKick->notes, ghostDefaults.rgVariationIntensity, rng);
            ghostKick->variationId += 1;
            mutableTracks.insert(ghostKick->type);
        }
    }

    if (trackType == TrackType::HiHat)
    {
        if (auto* openHat = findTrack(project, TrackType::OpenHat); openHat != nullptr && !openHat->locked && openHat->enabled)
        {
            const auto oldOpen = openHat->notes;
            openHatGenerator.generate(*openHat, *target, project.params, style, project.styleInfluence, phrasePlan, rng);
            filterLaneNotesByBarActivation(*openHat, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useOpenHat; });
            const auto& openDefaults = getLaneStyleDefaults(styleDefaults, openHat->type);
            openHat->notes = mergeVariationNotes(openHat->type, oldOpen, openHat->notes, openDefaults.rgVariationIntensity, rng);
            openHat->variationId += 1;
            mutableTracks.insert(openHat->type);
        }
    }

    postProcess(project, style, blueprint, lanePlan, rng, mutableTracks);
    validatePattern(project, blueprint, lanePlan, mutableTracks);
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void BoomBapEngine::mutatePattern(PatternProject& project)
{
    applyBoomBapStyleInfluence(project);
    const auto& style = getBoomBapProfile(project.params.boombapSubstyle);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.mutationCounter * 911 + 17));

    std::vector<TrackType> candidates;
    for (const auto& track : project.tracks)
    {
        if (!track.locked && track.enabled)
            candidates.push_back(track.type);
    }

    if (candidates.empty())
        return;

    std::shuffle(candidates.begin(), candidates.end(), rng);
    const int mutateCount = style.substyle == BoomBapSubstyle::LofiRap
        ? std::max(1, static_cast<int>(candidates.size() / 4))
        : std::max(1, static_cast<int>(candidates.size() / 3));

    for (int i = 0; i < mutateCount && i < static_cast<int>(candidates.size()); ++i)
        mutateTrack(project, candidates[static_cast<size_t>(i)]);

    project.mutationCounter += 1;
    project.phraseLengthBars = std::max(1, project.params.bars);
    project.phraseRoleSummary = phraseSummaryString(BoomBapPhrasePlanner::createPlan(project.phraseLengthBars,
                                                                                      style.barVariationAmount,
                                                                                      style.substyle,
                                                                                      project.params.densityAmount,
                                                                                      rng));
}

void BoomBapEngine::mutateTrack(PatternProject& project, TrackType trackType)
{
    applyBoomBapStyleInfluence(project);
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked || !track->enabled)
        return;

    if (track->notes.empty())
    {
        generateTrackNew(project, trackType);
        return;
    }

    const auto& style = getBoomBapProfile(project.params.boombapSubstyle);
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::BoomBap, project.params.boombapSubstyle);
    const auto& laneDefaults = getLaneStyleDefaults(styleDefaults, trackType);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.mutationCounter * 101 + static_cast<int>(trackType) * 43));
    const auto grooveContext = buildGrooveContext(project, style, rng);
    const auto phrasePlan = BoomBapPhrasePlanner::createPlan(std::max(1, project.params.bars),
                                                              grooveContext.phraseVariationAmount,
                                                              style.substyle,
                                                              project.params.densityAmount,
                                                              rng);
    const auto blueprint = buildBoomBapGrooveBlueprint(project.params, style, phrasePlan, grooveContext.halfTimeReference, rng);
    const auto lanePlan = buildBoomBapLaneActivation(project.params, style, blueprint, rng);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    const bool skeletonLane = trackType == TrackType::Kick || trackType == TrackType::Snare;
    float mutationIntensity = skeletonLane ? 0.86f : 1.0f;
    if (style.substyle == BoomBapSubstyle::RussianUnderground)
        mutationIntensity *= skeletonLane ? 0.74f : 0.82f;
    else if (style.substyle == BoomBapSubstyle::BoomBapGold)
        mutationIntensity *= skeletonLane ? 0.92f : 1.14f;
    else if (style.substyle == BoomBapSubstyle::LofiRap)
        mutationIntensity *= skeletonLane ? 0.58f : 0.64f;
    mutationIntensity *= laneDefaults.mutationIntensity;

    const auto isAnchor = [trackType](const NoteEvent& n)
    {
        return isAnchorStepForTrack(trackType, n.step % 16);
    };

    if (chance(rng) < (0.6f * mutationIntensity))
    {
        std::vector<size_t> removable;
        for (size_t i = 0; i < track->notes.size(); ++i)
            if (!isAnchor(track->notes[i]))
                removable.push_back(i);

        if (!removable.empty())
        {
            std::uniform_int_distribution<size_t> pick(0, removable.size() - 1);
            track->notes.erase(track->notes.begin() + static_cast<long long>(removable[pick(rng)]));
        }
    }

    if (chance(rng) < (0.7f * mutationIntensity) && !track->notes.empty())
    {
        std::uniform_int_distribution<size_t> pick(0, track->notes.size() - 1);
        auto& note = track->notes[pick(rng)];
        std::uniform_int_distribution<int> vel(-12, 12);
        std::uniform_int_distribution<int> micro(-10, 10);
        note.velocity = std::clamp(note.velocity + vel(rng), 1, 127);
        note.microOffset = std::clamp(note.microOffset + micro(rng), -120, 120);
    }

    if (chance(rng) < (0.5f * mutationIntensity))
    {
        std::vector<size_t> movable;
        for (size_t i = 0; i < track->notes.size(); ++i)
            if (!isAnchor(track->notes[i]))
                movable.push_back(i);

        if (!movable.empty())
        {
            std::uniform_int_distribution<size_t> pick(0, movable.size() - 1);
            auto& n = track->notes[movable[pick(rng)]];
            std::uniform_int_distribution<int> shift(-2, 2);
            n.step = std::clamp(n.step + shift(rng), 0, std::max(0, project.params.bars * 16 - 1));
        }
    }

    if (chance(rng) < (0.55f * mutationIntensity))
    {
        TrackState candidate = *track;
        const auto& candidateStyle = getBoomBapProfile(project.params.boombapSubstyle);
        const auto candidateGrooveContext = buildGrooveContext(project, candidateStyle, rng);
        const auto candidatePhrasePlan = BoomBapPhrasePlanner::createPlan(std::max(1, project.params.bars),
                                                                          candidateGrooveContext.phraseVariationAmount,
                                                                          style.substyle,
                                                                          project.params.densityAmount,
                                                                          rng);
        const auto candidateBlueprint = buildBoomBapGrooveBlueprint(project.params,
                                                                    candidateStyle,
                                                                    candidatePhrasePlan,
                                                                    candidateGrooveContext.halfTimeReference,
                                                                    rng);
        regenerateTrackInternal(project, candidate, candidateStyle, candidatePhrasePlan, candidateBlueprint, rng);

        for (const auto& note : candidate.notes)
        {
            if (!containsStep(track->notes, note.step) && !isAnchor(note))
            {
                track->notes.push_back(note);
                break;
            }
        }
    }

    dedupeAndSortNotes(track->notes);
    track->mutationDepth = std::clamp(track->mutationDepth + 0.12f, 0.0f, 1.0f);
    track->variationId += 1;
    project.mutationCounter += 1;

    std::unordered_set<TrackType> mutableTracks { trackType };
    postProcess(project, style, blueprint, lanePlan, rng, mutableTracks);
    validatePattern(project, blueprint, lanePlan, mutableTracks);
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void BoomBapEngine::regenerateTrackInternal(PatternProject& project,
                                            TrackState& track,
                                            const BoomBapStyleProfile& style,
                                            const std::vector<PhraseRole>& phrasePlan,
                                            const BoomBapGrooveBlueprint& blueprint,
                                            std::mt19937& rng)
{
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
            hatGenerator.generate(track, project.params, style, project.styleInfluence, phrasePlan, blueprint, rng);
            break;
        default:
            break;
    }

    if (track.type == TrackType::Kick)
    {
        for (const auto& bar : blueprint.bars)
        {
            if (bar.kickSupportAmount < 0.42f)
                continue;

            const int anchorStep = bar.barIndex * 16;
            if (!containsStep(track.notes, anchorStep))
                track.notes.push_back({ 36, anchorStep, 1, style.kickVelocityMin + 6, 0, false });
        }
    }
    else if (track.type == TrackType::Snare)
    {
        for (const auto& bar : blueprint.bars)
        {
            if (!bar.strongBackbeat)
                continue;

            const int beat2 = bar.barIndex * 16 + 4;
            const int beat4 = bar.barIndex * 16 + 12;
            if (!containsStep(track.notes, beat2))
                track.notes.push_back({ 38, beat2, 1, style.snareVelocityMin + 4, 0, false });
            if (!containsStep(track.notes, beat4))
                track.notes.push_back({ 38, beat4, 1, style.snareVelocityMin + 8, 0, false });
        }
    }
    else if (track.type == TrackType::HiHat)
    {
        track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [&](const NoteEvent& n)
        {
            const int barIndex = n.step / 16;
            const auto* bar = blueprintBarAt(blueprint, barIndex);
            return bar != nullptr && bar->hatActivity < 0.42f && (n.step % 2) == 1;
        }), track.notes.end());
    }

    track.subProfile = style.name;
    track.laneRole = roleForTrack(track.type);
}

void BoomBapEngine::generateDependentTracks(PatternProject& project,
                                            const BoomBapStyleProfile& style,
                                            const std::vector<PhraseRole>& phrasePlan,
                                            const BoomBapLaneActivationPlan& lanePlan,
                                            std::mt19937& rng,
                                            const std::unordered_set<TrackType>& mutableTracks)
{
    auto* snare = findTrack(project, TrackType::Snare);
    auto* kick = findTrack(project, TrackType::Kick);
    auto* hat = findTrack(project, TrackType::HiHat);

    if (auto* clap = findTrack(project, TrackType::ClapGhostSnare);
        clap != nullptr && snare != nullptr && mutableTracks.find(clap->type) != mutableTracks.end() && !clap->locked && clap->enabled)
    {
        ghostGenerator.generateClapLayer(*clap, *snare, style, project.styleInfluence, phrasePlan, rng);
        filterLaneNotesByBarActivation(*clap, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useClapGhostSnare; });
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);
        const float clapKeep = std::clamp(laneBalanceWeight(project, TrackType::ClapGhostSnare) * supportAccentWeight(project) * 0.78f, 0.2f, 1.0f);
        clap->notes.erase(std::remove_if(clap->notes.begin(), clap->notes.end(), [&](const NoteEvent& note)
        {
            return !note.isGhost && chance(rng) > clapKeep;
        }), clap->notes.end());
    }

    if (auto* ghostKick = findTrack(project, TrackType::GhostKick);
        ghostKick != nullptr && kick != nullptr && mutableTracks.find(ghostKick->type) != mutableTracks.end() && !ghostKick->locked && ghostKick->enabled)
    {
        ghostGenerator.generateGhostKick(*ghostKick, *kick, style, project.styleInfluence, project.params.densityAmount * laneActivityWeight(project, TrackType::Kick), phrasePlan, rng);
        filterLaneNotesByBarActivation(*ghostKick, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useGhostKick; });
    }

    if (auto* openHat = findTrack(project, TrackType::OpenHat);
        openHat != nullptr && hat != nullptr && mutableTracks.find(openHat->type) != mutableTracks.end() && !openHat->locked && openHat->enabled)
    {
        openHatGenerator.generate(*openHat, *hat, project.params, style, project.styleInfluence, phrasePlan, rng);
        filterLaneNotesByBarActivation(*openHat, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useOpenHat; });
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);
        const float openKeep = std::clamp(laneActivityWeight(project, TrackType::OpenHat), 0.2f, 1.0f);
        openHat->notes.erase(std::remove_if(openHat->notes.begin(), openHat->notes.end(), [&](const NoteEvent&)
        {
            return chance(rng) > openKeep;
        }), openHat->notes.end());
    }

    if (auto* perc = findTrack(project, TrackType::Perc);
        perc != nullptr && mutableTracks.find(perc->type) != mutableTracks.end() && !perc->locked && perc->enabled)
    {
        percGenerator.generate(*perc, project.params, style, project.styleInfluence, phrasePlan, rng);
        filterLaneNotesByBarActivation(*perc, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.usePerc; });
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);
        const float percKeep = std::clamp(laneActivityWeight(project, TrackType::Perc), 0.18f, 1.0f);
        perc->notes.erase(std::remove_if(perc->notes.begin(), perc->notes.end(), [&](const NoteEvent&)
        {
            return chance(rng) > percKeep;
        }), perc->notes.end());
    }

    if (auto* ride = findTrack(project, TrackType::Ride);
        ride != nullptr && mutableTracks.find(ride->type) != mutableTracks.end() && !ride->locked)
    {
        ride->notes.clear();
    }

    if (auto* cymbal = findTrack(project, TrackType::Cymbal);
        cymbal != nullptr && mutableTracks.find(cymbal->type) != mutableTracks.end() && !cymbal->locked)
    {
        cymbal->notes.clear();

        if (cymbal->enabled)
        {
            for (int bar = 0; bar < std::max(1, project.params.bars); ++bar)
            {
                const auto* lane = laneBarAt(lanePlan, bar);
                if (lane == nullptr || !lane->useCymbal)
                    continue;

                const int step = bar * 16 + 15;
                cymbal->notes.push_back({ 49, step, 2, std::min(120, style.openHatVelocityMax + 6), 0, false });
            }
        }
    }

    if (style.substyle == BoomBapSubstyle::LofiRap)
    {
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);

        if (auto* clap = findTrack(project, TrackType::ClapGhostSnare); clap != nullptr)
        {
            clap->notes.erase(std::remove_if(clap->notes.begin(), clap->notes.end(), [&chance, &rng](const NoteEvent&)
            {
                return chance(rng) < 0.65f;
            }), clap->notes.end());
        }

        if (auto* ghostKick = findTrack(project, TrackType::GhostKick); ghostKick != nullptr)
        {
            ghostKick->notes.erase(std::remove_if(ghostKick->notes.begin(), ghostKick->notes.end(), [&chance, &rng](const NoteEvent&)
            {
                return chance(rng) < 0.55f;
            }), ghostKick->notes.end());
        }

        if (auto* openHat = findTrack(project, TrackType::OpenHat); openHat != nullptr)
        {
            openHat->notes.erase(std::remove_if(openHat->notes.begin(), openHat->notes.end(), [&chance, &rng](const NoteEvent&)
            {
                return chance(rng) < 0.62f;
            }), openHat->notes.end());
        }

        if (auto* perc = findTrack(project, TrackType::Perc); perc != nullptr)
        {
            perc->notes.erase(std::remove_if(perc->notes.begin(), perc->notes.end(), [&chance, &rng](const NoteEvent&)
            {
                return chance(rng) < 0.60f;
            }), perc->notes.end());
        }
    }
}

void BoomBapEngine::postProcess(PatternProject& project,
                                const BoomBapStyleProfile& style,
                                const BoomBapGrooveBlueprint& blueprint,
                                const BoomBapLaneActivationPlan& lanePlan,
                                std::mt19937& rng,
                                const std::unordered_set<TrackType>& mutableTracks)
{
    juce::ignoreUnused(lanePlan);

    GrooveEngine::applySwing(project, style, mutableTracks);
    VelocityEngine::applyVelocityShape(project, style, mutableTracks);
    HumanizeEngine::applyHumanize(project, style, mutableTracks, rng);

    const auto& styleDefaults = getGenreStyleDefaults(GenreType::BoomBap, project.params.boombapSubstyle);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    if (style.substyle == BoomBapSubstyle::LofiRap)
    {
        auto sampleRange = [&rng](int lo, int hi)
        {
            std::uniform_int_distribution<int> dist(lo, hi);
            return dist(rng);
        };

        for (auto& track : project.tracks)
        {
            if (mutableTracks.find(track.type) == mutableTracks.end())
                continue;

            for (auto& note : track.notes)
            {
                switch (track.type)
                {
                    case TrackType::HiHat:
                        note.microOffset = note.isGhost ? sampleRange(-12, 14) : sampleRange(-6, 8);
                        note.velocity = note.isGhost ? sampleRange(28, 52) : sampleRange(58, 82);
                        break;
                    case TrackType::Snare:
                    case TrackType::ClapGhostSnare:
                        note.microOffset = note.isGhost ? sampleRange(0, 10) : sampleRange(6, 18);
                        note.velocity = note.isGhost ? sampleRange(28, 52) : sampleRange(76, 108);
                        break;
                    case TrackType::Kick:
                        note.microOffset = isAnchorStepForTrack(track.type, note.step % 16) ? sampleRange(-10, 12) : sampleRange(-16, 18);
                        note.velocity = note.isGhost ? sampleRange(26, 52) : sampleRange(70, 105);
                        break;
                    case TrackType::GhostKick:
                        note.microOffset = sampleRange(-16, 18);
                        note.velocity = sampleRange(26, 52);
                        break;
                    case TrackType::OpenHat:
                        note.microOffset = sampleRange(-4, 10);
                        note.velocity = sampleRange(62, 88);
                        break;
                    case TrackType::Perc:
                        note.microOffset = sampleRange(-8, 12);
                        note.velocity = sampleRange(44, 76);
                        break;
                    default:
                        break;
                }
            }
        }

        applySampleAwareBoomBapFlavor(project, mutableTracks);

        return;
    }

    for (auto& track : project.tracks)
    {
        if (mutableTracks.find(track.type) == mutableTracks.end())
            continue;

        for (auto& note : track.notes)
        {
            const int barIndex = note.step / 16;
            const auto* bar = blueprintBarAt(blueprint, barIndex);
            if (bar == nullptr)
                continue;

            if (track.type == TrackType::Snare && !note.isGhost)
            {
                const int stepInBar = note.step % 16;
                if (stepInBar == 4 || stepInBar == 12)
                {
                    const int latePush = static_cast<int>(bar->lateBackbeatAmount * 7.0f);
                    note.microOffset = std::clamp(note.microOffset + latePush, -120, 120);
                }
            }
            else if (track.type == TrackType::HiHat)
            {
                const float velScale = std::clamp(0.78f + bar->hatActivity * 0.34f + bar->grit * 0.08f, 0.6f, 1.24f);
                note.velocity = std::clamp(static_cast<int>(static_cast<float>(note.velocity) * velScale), style.hatVelocityMin, style.hatVelocityMax);
                if (bar->stripToCore && (note.step % 2) == 1)
                    note.velocity = std::max(style.hatVelocityMin, note.velocity - 10);
            }
            else if (track.type == TrackType::Kick && !note.isGhost)
            {
                if ((note.step % 16) != 0 && bar->kickSupportAmount < 0.34f)
                    note.velocity = std::max(style.kickVelocityMin, note.velocity - 12);
            }
        }
    }

    for (auto& track : project.tracks)
    {
        if (mutableTracks.find(track.type) == mutableTracks.end())
            continue;

        const auto& lane = getLaneStyleDefaults(styleDefaults, track.type);
        std::vector<NoteEvent> filtered;
        filtered.reserve(track.notes.size());

        for (auto& note : track.notes)
        {
            const bool anchor = isAnchorStepForTrack(track.type, note.step % 16);
            const float keep = anchor ? 1.0f : std::clamp(lane.noteProbability * (0.55f + lane.densityBias * 0.45f), 0.18f, 1.0f);
            if (chance(rng) > keep)
                continue;

            note.microOffset = static_cast<int>(note.microOffset * lane.timingBias);
            note.velocity = std::clamp(static_cast<int>(note.velocity * (0.94f + lane.humanizeBias * 0.08f)), 1, 127);
            filtered.push_back(note);
        }

        if (!filtered.empty())
            track.notes = std::move(filtered);

        if (track.type == TrackType::HiHat
            || track.type == TrackType::OpenHat
            || track.type == TrackType::Perc
            || track.type == TrackType::Ride
            || track.type == TrackType::GhostKick
            || track.type == TrackType::ClapGhostSnare
            || track.type == TrackType::Cymbal)
        {
            trimTrackToBarBudgets(track, blueprint, style.substyle, project.params.densityAmount);
        }
    }

    if (auto* snare = findTrack(project, TrackType::Snare); snare != nullptr)
    {
        for (const auto& bar : blueprint.bars)
        {
            const int beat2 = bar.barIndex * 16 + 4;
            const int beat4 = bar.barIndex * 16 + 12;
            if (bar.strongBackbeat)
            {
                if (!containsStep(snare->notes, beat2))
                    snare->notes.push_back({ 38, beat2, 1, style.snareVelocityMin + 4, 0, false });
                if (!containsStep(snare->notes, beat4))
                    snare->notes.push_back({ 38, beat4, 1, style.snareVelocityMin + 8, 0, false });
            }
        }
        dedupeAndSortNotes(snare->notes);
    }

    if (auto* kick = findTrack(project, TrackType::Kick); kick != nullptr)
    {
        for (const auto& bar : blueprint.bars)
        {
            const int anchor = bar.barIndex * 16;
            if (bar.kickSupportAmount > 0.42f && !containsStep(kick->notes, anchor))
                kick->notes.push_back({ 36, anchor, 1, style.kickVelocityMin + 6, 0, false });
        }
        dedupeAndSortNotes(kick->notes);
    }

    applySampleAwareBoomBapFlavor(project, mutableTracks);
}

void BoomBapEngine::applyPhraseEndingAccents(PatternProject& project,
                                             const BoomBapStyleProfile& style,
                                             std::mt19937& rng,
                                             const std::vector<PhraseRole>& phrasePlan,
                                             const std::unordered_set<TrackType>& mutableTracks)
{
    const int bars = std::max(1, project.params.bars);
    if (bars == 1)
        return;

    const int endingBar = bars - 1;
    if (endingBar >= static_cast<int>(phrasePlan.size()) || phrasePlan[static_cast<size_t>(endingBar)] != PhraseRole::Ending)
        return;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::BoomBap, project.params.boombapSubstyle);

    if (auto* openHat = findTrack(project, TrackType::OpenHat);
        openHat != nullptr && mutableTracks.count(openHat->type) != 0 && openHat->enabled && !openHat->locked)
    {
        std::uniform_int_distribution<int> endingVariant(0, 2);
        const int variant = endingVariant(rng);
        const int step = variant == 0 ? 14 : (variant == 1 ? 15 : 13);
        const float laneEnding = getLaneStyleDefaults(styleDefaults, TrackType::OpenHat).phraseEndingProbability;
        if (!containsStep(openHat->notes, endingBar * 16 + step) && chance(rng) < std::clamp(style.openHatChance * 0.95f * laneEnding, 0.10f, 0.95f))
            openHat->notes.push_back({ 46, endingBar * 16 + step, 2, style.openHatVelocityMax - 4, variant == 2 ? -6 : 0, false });
    }

    if (auto* kick = findTrack(project, TrackType::Kick);
        kick != nullptr && mutableTracks.count(kick->type) != 0 && kick->enabled && !kick->locked)
    {
        std::uniform_int_distribution<int> kickVariant(0, 2);
        const int variant = kickVariant(rng);
        const int step = variant == 0 ? 15 : (variant == 1 ? 14 : 11);
        const float laneEnding = getLaneStyleDefaults(styleDefaults, TrackType::Kick).phraseEndingProbability;
        if (!containsStep(kick->notes, endingBar * 16 + step) && chance(rng) < std::clamp(0.56f * laneEnding, 0.1f, 0.95f))
            kick->notes.push_back({ 36, endingBar * 16 + step, 1, style.kickVelocityMin + 10, variant == 2 ? 8 : -4, false });
    }

    if (auto* hat = findTrack(project, TrackType::HiHat);
        hat != nullptr && mutableTracks.count(hat->type) != 0 && hat->enabled && !hat->locked)
    {
        const int step = chance(rng) < 0.5f ? 14 : 15;
        const float laneEnding = getLaneStyleDefaults(styleDefaults, TrackType::HiHat).phraseEndingProbability;
        if (!containsStep(hat->notes, endingBar * 16 + step) && chance(rng) < std::clamp(0.62f * laneEnding, 0.1f, 0.95f))
            hat->notes.push_back({ 42, endingBar * 16 + step, 1, style.hatVelocityMax - 2, 2, false });
    }

    if (auto* snareGhost = findTrack(project, TrackType::ClapGhostSnare);
        snareGhost != nullptr && mutableTracks.count(snareGhost->type) != 0 && snareGhost->enabled && !snareGhost->locked)
    {
        const float laneEnding = getLaneStyleDefaults(styleDefaults, TrackType::ClapGhostSnare).phraseEndingProbability;
        const float classicScale = style.substyle == BoomBapSubstyle::Classic ? 0.34f : 1.0f;
        if (!containsStep(snareGhost->notes, endingBar * 16 + 11) && chance(rng) < std::clamp(0.35f * laneEnding * classicScale, 0.02f, 0.9f))
            snareGhost->notes.push_back({ 39, endingBar * 16 + 11, 1, style.ghostVelocityMax - 3, 10, true });
    }

    if (auto* perc = findTrack(project, TrackType::Perc);
        perc != nullptr && mutableTracks.count(perc->type) != 0 && perc->enabled && !perc->locked)
    {
        const int step = chance(rng) < 0.5f ? 13 : 12;
        const float laneEnding = getLaneStyleDefaults(styleDefaults, TrackType::Perc).phraseEndingProbability;
        if (!containsStep(perc->notes, endingBar * 16 + step) && chance(rng) < std::clamp(0.35f * laneEnding, 0.08f, 0.9f))
            perc->notes.push_back({ 50, endingBar * 16 + step, 1, style.percVelocityMin + 5, 0, false });
    }

    if (bars >= 4)
    {
        const int bar2 = 1;
        const int bar4 = 3;

        if (auto* snare = findTrack(project, TrackType::Snare); snare != nullptr && mutableTracks.count(snare->type) != 0 && !snare->locked)
        {
            const float laneEnding = getLaneStyleDefaults(styleDefaults, TrackType::Snare).phraseEndingProbability;
            const float supportScale = style.substyle == BoomBapSubstyle::Classic ? 0.26f : 1.0f;
            const bool asGhost = style.substyle == BoomBapSubstyle::Classic;
            const int supportVelocity = asGhost ? style.ghostVelocityMax : style.snareVelocityMin + 6;
            if (!containsStep(snare->notes, bar2 * 16 + 11) && chance(rng) < std::clamp(0.42f * laneEnding * supportScale, 0.02f, 0.95f))
                snare->notes.push_back({ 38, bar2 * 16 + 11, 1, supportVelocity, 8, asGhost });

            if (!containsStep(snare->notes, bar4 * 16 + 15) && chance(rng) < std::clamp(0.48f * laneEnding * supportScale, 0.02f, 0.95f))
                snare->notes.push_back({ 38, bar4 * 16 + 15, 1, supportVelocity, -6, asGhost });
        }
    }
}

BoomBapEngine::GrooveContext BoomBapEngine::buildGrooveContext(const PatternProject& project,
                                                               const BoomBapStyleProfile& style,
                                                               std::mt19937& rng) const
{
    GrooveContext context;
    context.halfTimeReference = style.grooveReferenceBpm >= 150;
    const auto referenceFeel = buildReferenceBoomBapGrooveFeel(project, 0);

    const float baseVar = style.barVariationAmount;
    const float halfBias = context.halfTimeReference ? style.halfTimeReferenceBias * 0.16f : 0.0f;
    const float densityBias = (project.params.densityAmount - 0.5f) * 0.10f;
    const float referenceVar = referenceFeel.available ? (referenceFeel.supportRatio - referenceFeel.carrierRatio) * 0.08f : 0.0f;
    context.phraseVariationAmount = std::clamp(baseVar + halfBias + densityBias + referenceVar, 0.15f, 0.75f);

    std::array<float, 3> weights {
        std::max(0.01f, style.hatCarrierPreference),
        std::max(0.01f, style.rideCarrierPreference),
        std::max(0.01f, style.hybridCarrierPreference)
    };

    if (context.halfTimeReference)
    {
        weights[0] *= 0.92f;
        weights[1] *= 1.28f;
        weights[2] *= 1.14f;
    }
    if (referenceFeel.available)
    {
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);
        weights[0] *= std::clamp(0.88f + referenceFeel.carrierRatio * 0.42f, 0.78f, 1.28f);
        weights[1] *= std::clamp(0.92f + referenceFeel.gapRatio * 0.36f, 0.78f, 1.24f);
        weights[2] *= std::clamp(0.9f + referenceFeel.supportRatio * 0.28f, 0.78f, 1.22f);
        if (referenceFeel.gapRatio > 0.5f && referenceFeel.carrierRatio < 0.3f)
            context.halfTimeReference = context.halfTimeReference || chance(rng) < 0.42f;
    }

    std::discrete_distribution<int> pick(weights.begin(), weights.end());
    context.carrierMode = static_cast<CarrierMode>(pick(rng));
    return context;
}

void BoomBapEngine::applyCarrierMode(PatternProject& project,
                                     const BoomBapStyleProfile& style,
                                     const std::vector<PhraseRole>& phrasePlan,
                                     const BoomBapLaneActivationPlan& lanePlan,
                                     const GrooveContext& grooveContext,
                                     std::mt19937& rng,
                                     const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* hat = findTrack(project, TrackType::HiHat);
    auto* ride = findTrack(project, TrackType::Ride);
    if (hat == nullptr || ride == nullptr)
        return;

    if (mutableTracks.count(hat->type) == 0 && mutableTracks.count(ride->type) == 0)
        return;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const float densityScale = carrierDensityForMode(grooveContext.carrierMode);
    const auto referenceFeel = buildReferenceBoomBapGrooveFeel(project, 0);

    if (mutableTracks.count(hat->type) != 0)
    {
        std::vector<NoteEvent> reducedHat;
        reducedHat.reserve(hat->notes.size());

        for (const auto& n : hat->notes)
        {
            const bool anchor = (n.step % 4) == 0;
            float keep = anchor ? 1.0f : std::clamp(0.55f + project.params.densityAmount * 0.35f, 0.35f, 0.92f) * densityScale;
            if (referenceFeel.available)
                keep *= anchor ? std::clamp(0.9f + referenceFeel.carrierRatio * 0.16f, 0.86f, 1.1f)
                               : std::clamp(0.9f + referenceFeel.supportRatio * 0.12f - std::max(0.0f, referenceFeel.gapRatio - 0.5f) * 0.16f, 0.78f, 1.08f);
            if (chance(rng) <= keep)
                reducedHat.push_back(n);
        }

        if (!reducedHat.empty())
            hat->notes = std::move(reducedHat);
    }

    if (mutableTracks.count(ride->type) == 0 || ride->locked || !ride->enabled)
        return;

    ride->notes.clear();

    const bool useRide = grooveContext.carrierMode == CarrierMode::Ride || grooveContext.carrierMode == CarrierMode::Hybrid;
    if (!useRide)
        return;

    std::uniform_int_distribution<int> rideVel(style.hatVelocityMin + 4, std::min(110, style.hatVelocityMax + 6));

    for (const auto& n : hat->notes)
    {
        const int stepInBar = n.step % 16;
        const int bar = n.step / 16;
        const auto role = bar < static_cast<int>(phrasePlan.size()) ? phrasePlan[static_cast<size_t>(bar)] : PhraseRole::Base;
        const auto* lane = laneBarAt(lanePlan, bar);
        if (lane != nullptr && !lane->useRide)
            continue;

        float gate = (grooveContext.carrierMode == CarrierMode::Ride) ? 0.72f : 0.34f;
        if (stepInBar % 4 == 0)
            gate += 0.18f;
        if (role == PhraseRole::Ending && stepInBar >= 12)
            gate += 0.18f;

        if (chance(rng) > std::clamp(gate, 0.08f, 0.95f))
            continue;

        NoteEvent rideNote;
        rideNote.pitch = 51;
        rideNote.step = n.step;
        rideNote.length = 1;
        rideNote.velocity = std::clamp(rideVel(rng), 72, 112);
        rideNote.microOffset = std::clamp(n.microOffset + (grooveContext.halfTimeReference ? 2 : 0), -80, 90);
        rideNote.isGhost = false;
        ride->notes.push_back(rideNote);
    }

    if (grooveContext.carrierMode == CarrierMode::Hybrid)
    {
        std::vector<NoteEvent> cleaned;
        cleaned.reserve(ride->notes.size());
        for (const auto& n : ride->notes)
        {
            if ((n.step % 2) == 0 || chance(rng) < 0.22f)
                cleaned.push_back(n);
        }
        ride->notes = std::move(cleaned);
    }
}

void BoomBapEngine::validatePattern(PatternProject& project,
                                    const BoomBapGrooveBlueprint& blueprint,
                                    const BoomBapLaneActivationPlan& lanePlan,
                                    const std::unordered_set<TrackType>& mutableTracks) const
{
    if (auto* openHat = findTrack(project, TrackType::OpenHat); openHat != nullptr)
        filterLaneNotesByBarActivation(*openHat, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useOpenHat; });

    if (auto* ride = findTrack(project, TrackType::Ride); ride != nullptr)
        filterLaneNotesByBarActivation(*ride, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useRide; });

    if (auto* perc = findTrack(project, TrackType::Perc); perc != nullptr)
        filterLaneNotesByBarActivation(*perc, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.usePerc; });

    if (auto* ghostKick = findTrack(project, TrackType::GhostKick); ghostKick != nullptr)
        filterLaneNotesByBarActivation(*ghostKick, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useGhostKick; });

    if (auto* clap = findTrack(project, TrackType::ClapGhostSnare); clap != nullptr)
        filterLaneNotesByBarActivation(*clap, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useClapGhostSnare; });

    if (auto* cymbal = findTrack(project, TrackType::Cymbal); cymbal != nullptr)
        filterLaneNotesByBarActivation(*cymbal, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useCymbal; });

    const auto* snare = findTrack(project, TrackType::Snare);
    const auto* clap = findTrack(project, TrackType::ClapGhostSnare);
    const auto* hat = findTrack(project, TrackType::HiHat);
    const auto* kick = findTrack(project, TrackType::Kick);
    const bool lowDensity = project.params.densityAmount < 0.35f;
    const bool highDensity = project.params.densityAmount > 0.75f;
    const auto style = getBoomBapProfile(project.params.boombapSubstyle);

    for (auto& track : project.tracks)
    {
        if (mutableTracks.find(track.type) == mutableTracks.end())
            continue;

        std::sort(track.notes.begin(), track.notes.end(), [](const NoteEvent& a, const NoteEvent& b)
        {
            if (a.step != b.step)
                return a.step < b.step;
            return a.velocity > b.velocity;
        });

        // Keep one note per step for stability in v0.1.
        track.notes.erase(std::unique(track.notes.begin(), track.notes.end(), [](const NoteEvent& a, const NoteEvent& b)
        {
            return a.step == b.step;
        }), track.notes.end());

        for (auto& note : track.notes)
        {
            note.length = std::max(1, note.length);

            const int barIndex = note.step / 16;
            const auto* bar = blueprintBarAt(blueprint, barIndex);
            if (bar != nullptr && bar->stripToCore)
            {
                if (track.type == TrackType::HiHat && (note.step % 2) == 1)
                    note.velocity = std::max(style.hatVelocityMin, note.velocity - 12);

                if (track.type == TrackType::GhostKick
                    || track.type == TrackType::OpenHat
                    || track.type == TrackType::Perc
                    || track.type == TrackType::Ride
                    || track.type == TrackType::Cymbal
                    || track.type == TrackType::ClapGhostSnare)
                {
                    note.step = -1;
                    continue;
                }
            }

            int minVel = note.isGhost ? style.ghostVelocityMin : style.snareVelocityMin;
            int maxVel = note.isGhost ? style.ghostVelocityMax : style.snareVelocityMax;

            if (track.type == TrackType::Kick)
            {
                minVel = style.kickVelocityMin;
                maxVel = style.kickVelocityMax;
            }
            else if (track.type == TrackType::HiHat)
            {
                minVel = style.hatVelocityMin;
                maxVel = style.hatVelocityMax;

                const int stepInBar = note.step % 16;
                int accentDelta = 0;
                if ((stepInBar % 4) == 0)
                    accentDelta += 8;
                else if ((stepInBar % 2) == 1)
                    accentDelta -= 10;
                else
                    accentDelta -= 3;

                if (stepInBar >= 14)
                    accentDelta += 4;

                note.velocity += accentDelta;
            }
            else if (track.type == TrackType::ClapGhostSnare)
            {
                minVel = style.clapVelocityMin;
                maxVel = style.clapVelocityMax;
            }
            else if (track.type == TrackType::OpenHat)
            {
                minVel = style.openHatVelocityMin;
                maxVel = style.openHatVelocityMax;
            }
            else if (track.type == TrackType::Perc)
            {
                minVel = style.percVelocityMin;
                maxVel = style.percVelocityMax;
            }
            else if (track.type == TrackType::Ride)
            {
                minVel = std::max(40, style.hatVelocityMin - 8);
                maxVel = std::min(120, style.hatVelocityMax - 4);
            }

            note.velocity = std::clamp(note.velocity, minVel, maxVel);
        }

        track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [](const NoteEvent& n)
        {
            return n.step < 0;
        }), track.notes.end());

        const int bars = std::max(1, project.params.bars);
        int maxHits = bars * 12;
        if (style.substyle == BoomBapSubstyle::Classic && track.type == TrackType::Kick)
            maxHits = bars * 5;
        if (track.type == TrackType::GhostKick)
            maxHits = bars * (lowDensity ? 2 : 4);
        if (style.substyle == BoomBapSubstyle::Classic && track.type == TrackType::GhostKick)
            maxHits = std::max(1, (bars + 3) / 4);
        if (style.substyle == BoomBapSubstyle::Classic && track.type == TrackType::ClapGhostSnare)
            maxHits = std::max(1, (bars + 3) / 4);
        if (track.type == TrackType::Perc)
            maxHits = bars * (lowDensity ? 1 : highDensity ? 5 : 3);

        if (style.substyle == BoomBapSubstyle::LofiRap)
        {
            if (track.type == TrackType::Kick)
                maxHits = bars * 4;
            else if (track.type == TrackType::HiHat)
                maxHits = bars * 12;
            else if (track.type == TrackType::OpenHat)
                maxHits = std::max(1, bars);
            else if (track.type == TrackType::ClapGhostSnare || track.type == TrackType::GhostKick)
                maxHits = std::max(1, bars * 2);
            else if (track.type == TrackType::Perc)
                maxHits = std::max(1, bars * 2);
            else if (track.type == TrackType::Ride || track.type == TrackType::Cymbal)
                maxHits = 0;
        }

        if (static_cast<int>(track.notes.size()) > maxHits)
            track.notes.resize(static_cast<size_t>(maxHits));

        if (track.type == TrackType::HiHat
            || track.type == TrackType::OpenHat
            || track.type == TrackType::Perc
            || track.type == TrackType::Ride
            || track.type == TrackType::GhostKick
            || track.type == TrackType::ClapGhostSnare
            || track.type == TrackType::Cymbal)
        {
            trimTrackToBarBudgets(track, blueprint, style.substyle, project.params.densityAmount);
        }

        if (track.type == TrackType::GhostKick)
        {
            for (auto& note : track.notes)
                note.velocity = std::min(note.velocity, style.ghostVelocityMax);
        }
    }

    // Avoid open hats directly colliding with snare/clap anchors.
    if (auto* openHat = findTrack(project, TrackType::OpenHat); openHat != nullptr)
    {
        openHat->notes.erase(std::remove_if(openHat->notes.begin(), openHat->notes.end(), [snare, clap](const NoteEvent& n)
        {
            const auto onSnare = snare != nullptr && std::any_of(snare->notes.begin(), snare->notes.end(), [&n](const NoteEvent& s) { return s.step == n.step; });
            const auto onClap = clap != nullptr && std::any_of(clap->notes.begin(), clap->notes.end(), [&n](const NoteEvent& c) { return c.step == n.step; });
            return onSnare || onClap;
        }), openHat->notes.end());
    }

    // Keep backbeat lane separation: kick should not stack on snare/clap hits.
    if (auto* mutableKick = findTrack(project, TrackType::Kick); mutableKick != nullptr)
    {
        mutableKick->notes.erase(std::remove_if(mutableKick->notes.begin(), mutableKick->notes.end(), [snare, clap](const NoteEvent& k)
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
        }), mutableKick->notes.end());
    }

    // Keep carrier hats present, especially for styles where hats drive the pocket.
    if (hat != nullptr)
    {
        const int bars = std::max(1, project.params.bars);
        const int minHatHits = bars * (style.substyle == BoomBapSubstyle::Classic ? 8 : (style.substyle == BoomBapSubstyle::Aggressive ? 8 : (style.substyle == BoomBapSubstyle::LofiRap ? 4 : 6)));
        if (static_cast<int>(hat->notes.size()) < minHatHits)
        {
            auto* mutableHat = findTrack(project, TrackType::HiHat);
            if (mutableHat != nullptr)
            {
                if (style.substyle == BoomBapSubstyle::Classic)
                {
                    reinforceClassicHatEighths(*mutableHat, bars, style);
                }
                else
                {
                    for (int bar = 0; bar < bars && static_cast<int>(mutableHat->notes.size()) < minHatHits; ++bar)
                        mutableHat->notes.push_back({ 42, bar * 16, 1, style.hatVelocityMin + 6, 0, false });
                }
            }
        }
    }

    if (auto* mutableSnare = findTrack(project, TrackType::Snare); mutableSnare != nullptr)
    {
        for (const auto& bar : blueprint.bars)
        {
            if (!bar.strongBackbeat)
                continue;

            const int beat2 = bar.barIndex * 16 + 4;
            const int beat4 = bar.barIndex * 16 + 12;
            if (!containsStep(mutableSnare->notes, beat2))
                mutableSnare->notes.push_back({ 38, beat2, 1, style.snareVelocityMin + 4, 0, false });
            if (!containsStep(mutableSnare->notes, beat4))
                mutableSnare->notes.push_back({ 38, beat4, 1, style.snareVelocityMin + 8, 0, false });
        }
    }

    if (auto* mutableKick = findTrack(project, TrackType::Kick); mutableKick != nullptr)
    {
        for (const auto& bar : blueprint.bars)
        {
            if (bar.kickSupportAmount < 0.50f)
                continue;

            const int step = bar.barIndex * 16;
            if (!containsStep(mutableKick->notes, step))
                mutableKick->notes.push_back({ 36, step, 1, style.kickVelocityMin + 6, 0, false });
        }
    }

    if (style.substyle == BoomBapSubstyle::Classic)
    {
        const int bars = std::max(1, project.params.bars);
        if (auto* mutableSnare = findTrack(project, TrackType::Snare); mutableSnare != nullptr)
            pruneClassicSnareGhosts(*mutableSnare, bars);
        if (auto* mutableClap = findTrack(project, TrackType::ClapGhostSnare); mutableClap != nullptr)
            pruneClassicSupportGhostLane(*mutableClap, bars, findTrack(project, TrackType::Snare));
        if (auto* mutableGhostKick = findTrack(project, TrackType::GhostKick); mutableGhostKick != nullptr)
            pruneClassicSupportGhostLane(*mutableGhostKick, bars, nullptr);
        if (auto* mutableKick = findTrack(project, TrackType::Kick); mutableKick != nullptr)
            pruneClassicKickDensity(*mutableKick, bars);
        if (auto* mutableHat = findTrack(project, TrackType::HiHat); mutableHat != nullptr)
            reinforceClassicHatEighths(*mutableHat, bars, style);
    }

    // Guard style identity: aggressive should not be empty, laidback should not get busy.
    if (kick != nullptr)
    {
        const int bars = std::max(1, project.params.bars);
        auto* mutableKick = findTrack(project, TrackType::Kick);
        if (mutableKick != nullptr)
        {
            if (style.substyle == BoomBapSubstyle::Aggressive && static_cast<int>(mutableKick->notes.size()) < bars * 4)
            {
                for (int bar = 0; bar < bars; ++bar)
                    mutableKick->notes.push_back({ 36, bar * 16 + 8, 1, style.kickVelocityMin + 8, 0, false });
            }

            if (style.substyle == BoomBapSubstyle::LaidBack && static_cast<int>(mutableKick->notes.size()) > bars * 6)
                mutableKick->notes.resize(static_cast<size_t>(bars * 6));
        }
    }

    if (style.substyle == BoomBapSubstyle::Dusty)
        applyDustyPocketRules(project, style, mutableTracks);
    if (style.substyle == BoomBapSubstyle::Jazzy)
        applyJazzyPocketRules(project, style, mutableTracks);
    if (style.substyle == BoomBapSubstyle::BoomBapGold)
        applyBoomBapGoldPocketRules(project, style, mutableTracks);
}

juce::String BoomBapEngine::phraseSummaryString(const std::vector<PhraseRole>& roles)
{
    juce::StringArray parts;
    for (const auto role : roles)
    {
        switch (role)
        {
            case PhraseRole::Base: parts.add("statement"); break;
            case PhraseRole::Variation: parts.add("variation"); break;
            case PhraseRole::Contrast: parts.add("support"); break;
            case PhraseRole::Ending: parts.add("ending"); break;
            default: parts.add("base"); break;
        }
    }

    return parts.joinIntoString("|");
}

bool BoomBapEngine::isKickAnchorStep(int stepInBar)
{
    return stepInBar == 0 || stepInBar == 8;
}

bool BoomBapEngine::isSnareAnchorStep(int stepInBar)
{
    return stepInBar == 4 || stepInBar == 12;
}
} // namespace bbg
