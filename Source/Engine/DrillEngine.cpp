#include "DrillEngine.h"

#include <algorithm>

#include "../Analysis/StepHintWeighter.h"
#include "PatternPerformanceTransformEngine.h"
#include "StyleInfluence.h"

namespace bbg
{
namespace
{
TrackState* findTrack(PatternProject& project, TrackType type)
{
    for (auto& track : project.tracks)
    {
        if (track.type == type)
            return &track;
    }

    return nullptr;
}

std::mt19937 makeDrillRng(const PatternProject& project, int salt)
{
    const int seed = std::max(1, project.params.seed);
    return std::mt19937(static_cast<std::mt19937::result_type>(seed + salt));
}

bool canTouch(const std::unordered_set<TrackType>& mutableTracks, TrackType type)
{
    return mutableTracks.find(type) != mutableTracks.end();
}

void applyDrillStyleInfluence(PatternProject& project)
{
    juce::String applyError;
    DrillStyleInfluence::apply(project, &applyError);
    juce::ignoreUnused(applyError);
}
} // namespace

void DrillEngine::generate(PatternProject& project)
{
    runGenerationPass(project,
                      { TrackType::HiHat, TrackType::HatFX, TrackType::Snare, TrackType::ClapGhostSnare, TrackType::Kick, TrackType::Sub808 },
                      0x2451);
}

void DrillEngine::regenerateTrack(PatternProject& project, TrackType trackType)
{
    runGenerationPass(project, expandMutableTracks(trackType), 0x2571);
}

void DrillEngine::generateTrackNew(PatternProject& project, TrackType trackType)
{
    runGenerationPass(project, expandMutableTracks(trackType), 0x2691);
}

void DrillEngine::regenerateTrackVariation(PatternProject& project, TrackType trackType)
{
    runGenerationPass(project, expandMutableTracks(trackType), 0x2791);
}

void DrillEngine::mutatePattern(PatternProject& project)
{
    runGenerationPass(project,
                      { TrackType::HiHat, TrackType::HatFX, TrackType::Snare, TrackType::ClapGhostSnare, TrackType::Kick, TrackType::Sub808 },
                      0x2891);
}

void DrillEngine::mutateTrack(PatternProject& project, TrackType trackType)
{
    runGenerationPass(project, expandMutableTracks(trackType), 0x2991);
}

void DrillEngine::applyPhrasePlan(PatternProject& project, const DrillPhrasePlan& plan) const
{
    project.phraseLengthBars = juce::jmax(1, plan.phraseSpanBars);
    project.phraseRoleSummary = plan.summary;
}

void DrillEngine::runGenerationPass(PatternProject& project,
                                    const std::unordered_set<TrackType>& mutableTracks,
                                    int seedSalt) const
{
    if (mutableTracks.empty())
        return;

    applyDrillStyleInfluence(project);

    const auto plan = DrillPhrasePlanner::buildPlan(project);
    applyPhrasePlan(project, plan);

    auto* hat = findTrack(project, TrackType::HiHat);
    auto* hatFx = findTrack(project, TrackType::HatFX);
    auto* snare = findTrack(project, TrackType::Snare);
    auto* clapGhost = findTrack(project, TrackType::ClapGhostSnare);
    auto* kick = findTrack(project, TrackType::Kick);
    auto* sub = findTrack(project, TrackType::Sub808);

    if (hat != nullptr && canTouch(mutableTracks, TrackType::HiHat) && hat->enabled && !hat->locked)
    {
        auto rng = makeDrillRng(project, seedSalt + 0x0111);
        hatGenerator.generate(*hat, project, plan, rng);
    }

    if (hatFx != nullptr && canTouch(mutableTracks, TrackType::HatFX) && hatFx->enabled && !hatFx->locked && hat != nullptr)
    {
        auto rng = makeDrillRng(project, seedSalt + 0x0222);
        hatFxGenerator.generate(*hatFx, *hat, project, plan, rng);
    }

    if (snare != nullptr && canTouch(mutableTracks, TrackType::Snare) && snare->enabled && !snare->locked)
    {
        TrackState* clapTarget = nullptr;
        if (clapGhost != nullptr && canTouch(mutableTracks, TrackType::ClapGhostSnare) && clapGhost->enabled && !clapGhost->locked)
            clapTarget = clapGhost;

        auto rng = makeDrillRng(project, seedSalt + 0x0333);
        snareGenerator.generate(*snare, clapTarget, project, plan, rng);
    }
    else if (clapGhost != nullptr && canTouch(mutableTracks, TrackType::ClapGhostSnare) && clapGhost->enabled && !clapGhost->locked)
    {
        clapGhost->notes.clear();
        clapGhost->laneRole = "drill_clap_ghost";
        clapGhost->subProfile = "Main";
    }

    if (kick != nullptr && canTouch(mutableTracks, TrackType::Kick) && kick->enabled && !kick->locked)
    {
        auto rng = makeDrillRng(project, seedSalt + 0x0444);
        kickGenerator.generate(*kick, project, plan, snare, rng);
    }

    if (sub != nullptr && canTouch(mutableTracks, TrackType::Sub808) && sub->enabled && !sub->locked && kick != nullptr)
    {
        auto rng = makeDrillRng(project, seedSalt + 0x0555);
        subGenerator.generate(*sub, *kick, project, plan, snare, rng);
    }

    StepHintWeighter::applyToProject(project, mutableTracks);
    validator.validate(project, plan, mutableTracks);
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

std::unordered_set<TrackType> DrillEngine::expandMutableTracks(TrackType trackType)
{
    switch (trackType)
    {
        case TrackType::HiHat:
        case TrackType::HatFX:
            return { TrackType::HiHat, TrackType::HatFX };
        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            return { TrackType::Snare, TrackType::ClapGhostSnare };
        case TrackType::Kick:
        case TrackType::Sub808:
            return { TrackType::Kick, TrackType::Sub808 };
        default:
            return {};
    }
}
} // namespace bbg