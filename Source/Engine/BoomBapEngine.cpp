#include "BoomBapEngine.h"

#include <algorithm>
#include <functional>
#include <unordered_map>

#include "GrooveEngine.h"
#include "HumanizeEngine.h"
#include "StyleInfluence.h"
#include "StyleDefaults.h"
#include "VelocityEngine.h"

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
    return type == TrackType::HiHat
        || type == TrackType::OpenHat
        || type == TrackType::Ride
        || type == TrackType::Cymbal
        || type == TrackType::Perc;
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
            if (bar.stripToCore)
                budget = std::min(budget, 6);
            if (substyle == BoomBapSubstyle::LofiRap)
                budget = std::min(budget, 8);
            return std::clamp(budget, 3, 16);
        }
        case TrackType::OpenHat:
        {
            int budget = static_cast<int>(bar.endLiftAmount > 0.6f ? 2 : 1);
            if (substyle == BoomBapSubstyle::BoomBapGold && bar.role == PhraseRole::Ending)
                budget += 1;
            if (bar.stripToCore)
                budget = 0;
            return std::clamp(budget, 0, 3);
        }
        case TrackType::Perc:
        {
            int budget = static_cast<int>(bar.hatSyncopation * 4.0f + den * 2.0f);
            if (bar.role == PhraseRole::Ending)
                budget += 1;
            if (substyle == BoomBapSubstyle::LofiRap)
                budget = std::min(budget, 1);
            if (bar.stripToCore)
                budget = 0;
            return std::clamp(budget, 0, 5);
        }
        case TrackType::Ride:
        {
            int budget = static_cast<int>(2.0f + bar.hatActivity * 5.0f);
            if (bar.role == PhraseRole::Ending)
                budget += 1;
            if (substyle == BoomBapSubstyle::LofiRap || substyle == BoomBapSubstyle::RussianUnderground)
                budget = 0;
            if (bar.stripToCore)
                budget = 0;
            return std::clamp(budget, 0, 8);
        }
        case TrackType::GhostKick:
        {
            int budget = static_cast<int>(bar.kickSupportAmount * 4.0f + den * 2.0f);
            if (substyle == BoomBapSubstyle::LofiRap)
                budget = std::min(budget, 1);
            if (bar.stripToCore)
                budget = 0;
            return std::clamp(budget, 0, 4);
        }
        case TrackType::ClapGhostSnare:
        {
            int budget = bar.strongBackbeat ? 2 : 1;
            if (bar.role == PhraseRole::Ending)
                budget += 1;
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
            ghostGenerator.generateGhostKick(*ghostKick, *track, style, project.params.densityAmount, phrasePlan, rng);
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
            ghostGenerator.generateClapLayer(*clap, *track, style, phrasePlan, rng);
            filterLaneNotesByBarActivation(*clap, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useClapGhostSnare; });
            mutableTracks.insert(clap->type);
        }
    }
    else if (trackType == TrackType::HiHat)
    {
        if (auto* openHat = findTrack(project, TrackType::OpenHat); openHat != nullptr && !openHat->locked && openHat->enabled)
        {
            openHatGenerator.generate(*openHat, *track, project.params, style, phrasePlan, rng);
            filterLaneNotesByBarActivation(*openHat, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useOpenHat; });
            mutableTracks.insert(openHat->type);
        }
    }

    applyPhraseEndingAccents(project, style, rng, phrasePlan, mutableTracks);
    applyCarrierMode(project, style, phrasePlan, lanePlan, grooveContext, rng, mutableTracks);
    postProcess(project, style, blueprint, lanePlan, rng, mutableTracks);
    validatePattern(project, blueprint, lanePlan, mutableTracks);
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
            ghostGenerator.generateClapLayer(*clap, *target, style, phrasePlan, rng);
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
            ghostGenerator.generateGhostKick(*ghostKick, *target, style, project.params.densityAmount, phrasePlan, rng);
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
            openHatGenerator.generate(*openHat, *target, project.params, style, phrasePlan, rng);
            filterLaneNotesByBarActivation(*openHat, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useOpenHat; });
            const auto& openDefaults = getLaneStyleDefaults(styleDefaults, openHat->type);
            openHat->notes = mergeVariationNotes(openHat->type, oldOpen, openHat->notes, openDefaults.rgVariationIntensity, rng);
            openHat->variationId += 1;
            mutableTracks.insert(openHat->type);
        }
    }

    postProcess(project, style, blueprint, lanePlan, rng, mutableTracks);
    validatePattern(project, blueprint, lanePlan, mutableTracks);
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
        const auto grooveContext = buildGrooveContext(project, candidateStyle, rng);
        const auto phrasePlan = BoomBapPhrasePlanner::createPlan(std::max(1, project.params.bars),
                                      grooveContext.phraseVariationAmount,
                                      style.substyle,
                                      project.params.densityAmount,
                                      rng);
        const auto blueprint = buildBoomBapGrooveBlueprint(project.params, candidateStyle, phrasePlan, grooveContext.halfTimeReference, rng);
        regenerateTrackInternal(project, candidate, candidateStyle, phrasePlan, blueprint, rng);

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
        ghostGenerator.generateClapLayer(*clap, *snare, style, phrasePlan, rng);
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
        ghostGenerator.generateGhostKick(*ghostKick, *kick, style, project.params.densityAmount * laneActivityWeight(project, TrackType::Kick), phrasePlan, rng);
        filterLaneNotesByBarActivation(*ghostKick, lanePlan, [](const BoomBapLaneActivation& lane) { return lane.useGhostKick; });
    }

    if (auto* openHat = findTrack(project, TrackType::OpenHat);
        openHat != nullptr && hat != nullptr && mutableTracks.find(openHat->type) != mutableTracks.end() && !openHat->locked && openHat->enabled)
    {
        openHatGenerator.generate(*openHat, *hat, project.params, style, phrasePlan, rng);
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
        percGenerator.generate(*perc, project.params, style, phrasePlan, rng);
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
        if (!containsStep(snareGhost->notes, endingBar * 16 + 11) && chance(rng) < std::clamp(0.35f * laneEnding, 0.08f, 0.9f))
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
            if (!containsStep(snare->notes, bar2 * 16 + 11) && chance(rng) < std::clamp(0.42f * laneEnding, 0.08f, 0.95f))
                snare->notes.push_back({ 38, bar2 * 16 + 11, 1, style.snareVelocityMin + 6, 8, false });

            if (!containsStep(snare->notes, bar4 * 16 + 15) && chance(rng) < std::clamp(0.48f * laneEnding, 0.08f, 0.95f))
                snare->notes.push_back({ 38, bar4 * 16 + 15, 1, style.snareVelocityMin + 10, -6, false });
        }
    }
}

BoomBapEngine::GrooveContext BoomBapEngine::buildGrooveContext(const PatternProject& project,
                                                               const BoomBapStyleProfile& style,
                                                               std::mt19937& rng) const
{
    GrooveContext context;
    context.halfTimeReference = style.grooveReferenceBpm >= 150;

    const float baseVar = style.barVariationAmount;
    const float halfBias = context.halfTimeReference ? style.halfTimeReferenceBias * 0.16f : 0.0f;
    const float densityBias = (project.params.densityAmount - 0.5f) * 0.10f;
    context.phraseVariationAmount = std::clamp(baseVar + halfBias + densityBias, 0.15f, 0.75f);

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

    if (mutableTracks.count(hat->type) != 0)
    {
        std::vector<NoteEvent> reducedHat;
        reducedHat.reserve(hat->notes.size());

        for (const auto& n : hat->notes)
        {
            const bool anchor = (n.step % 4) == 0;
            const float keep = anchor ? 1.0f : std::clamp(0.55f + project.params.densityAmount * 0.35f, 0.35f, 0.92f) * densityScale;
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
        if (track.type == TrackType::GhostKick)
            maxHits = bars * (lowDensity ? 2 : 4);
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
        const int minHatHits = bars * (style.substyle == BoomBapSubstyle::Aggressive ? 8 : (style.substyle == BoomBapSubstyle::LofiRap ? 4 : 6));
        if (static_cast<int>(hat->notes.size()) < minHatHits)
        {
            auto* mutableHat = findTrack(project, TrackType::HiHat);
            if (mutableHat != nullptr)
            {
                for (int bar = 0; bar < bars && static_cast<int>(mutableHat->notes.size()) < minHatHits; ++bar)
                    mutableHat->notes.push_back({ 42, bar * 16, 1, style.hatVelocityMin + 6, 0, false });
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
