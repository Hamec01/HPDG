#include "DrillSnareGenerator.h"

#include <algorithm>

#include "../../Core/TrackRegistry.h"

namespace bbg
{
namespace
{
enum class DrillSnareDecorationMode
{
    BackboneOnly = 0,
    ClapLayer,
    PushGhost,
    DragGhost
};

bool isGhostMode(DrillSnareDecorationMode mode)
{
    return mode == DrillSnareDecorationMode::PushGhost || mode == DrillSnareDecorationMode::DragGhost;
}

DrillSnareDecorationMode chooseDecorationMode(const DrillPhraseBarPlan& bar, std::mt19937& rng)
{
    switch (bar.supportAccent)
    {
        case DrillSupportAccentIntent::None:
            return DrillSnareDecorationMode::BackboneOnly;

        case DrillSupportAccentIntent::Light:
        {
            std::discrete_distribution<int> pick { 72, 28 };
            return pick(rng) == 1 ? DrillSnareDecorationMode::ClapLayer : DrillSnareDecorationMode::BackboneOnly;
        }

        case DrillSupportAccentIntent::Push:
        {
            std::discrete_distribution<int> pick {
                bar.role == DrillPhraseBarRole::Lift ? 14.0 : 18.0,
                bar.role == DrillPhraseBarRole::Lift ? 28.0 : 34.0,
                bar.role == DrillPhraseBarRole::Lift ? 58.0 : 48.0
            };

            switch (pick(rng))
            {
                case 1: return DrillSnareDecorationMode::ClapLayer;
                case 2: return DrillSnareDecorationMode::PushGhost;
                default: return DrillSnareDecorationMode::BackboneOnly;
            }
        }

        case DrillSupportAccentIntent::Drag:
        default:
        {
            std::discrete_distribution<int> pick {
                bar.role == DrillPhraseBarRole::Release ? 12.0 : 18.0,
                bar.role == DrillPhraseBarRole::Release ? 24.0 : 30.0,
                bar.role == DrillPhraseBarRole::Release ? 64.0 : 52.0
            };

            switch (pick(rng))
            {
                case 1: return DrillSnareDecorationMode::ClapLayer;
                case 2: return DrillSnareDecorationMode::DragGhost;
                default: return DrillSnareDecorationMode::BackboneOnly;
            }
        }
    }
}

std::vector<int> candidateGhostStepsForMode(const DrillPhraseBarPlan& bar,
                                            DrillSnareDecorationMode mode,
                                            int primarySnare)
{
    std::vector<int> candidates;
    candidates.reserve(bar.anchorMap.supportAccentSteps.size());

    for (const int supportStep : bar.anchorMap.supportAccentSteps)
    {
        if (supportStep < 0 || supportStep == primarySnare)
            continue;

        const int distance = std::abs(supportStep - primarySnare);
        if (distance < 1 || distance > 2)
            continue;

        if (mode == DrillSnareDecorationMode::PushGhost && supportStep > primarySnare)
            continue;
        if (mode == DrillSnareDecorationMode::DragGhost && supportStep < primarySnare)
            continue;

        candidates.push_back(supportStep);
    }

    std::sort(candidates.begin(), candidates.end(), [primarySnare, mode](int lhs, int rhs)
    {
        const int leftDistance = std::abs(lhs - primarySnare);
        const int rightDistance = std::abs(rhs - primarySnare);
        if (leftDistance != rightDistance)
            return leftDistance < rightDistance;
        if (mode == DrillSnareDecorationMode::PushGhost)
            return lhs > rhs;
        return lhs < rhs;
    });

    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}
} // namespace

void DrillSnareGenerator::generate(TrackState& snareTrack,
                                   TrackState* clapGhostTrack,
                                   const PatternProject& project,
                                   const DrillPhrasePlan& phrasePlan,
                                   std::mt19937& rng) const
{
    juce::ignoreUnused(project);

    snareTrack.notes.clear();
    snareTrack.subProfile = "Main";
    snareTrack.laneRole = "drill_snare";

    if (clapGhostTrack != nullptr)
    {
        clapGhostTrack->notes.clear();
        clapGhostTrack->subProfile = "Main";
        clapGhostTrack->laneRole = "drill_clap_ghost";
    }

    const auto* snareInfo = TrackRegistry::find(TrackType::Snare);
    const auto* clapInfo = TrackRegistry::find(TrackType::ClapGhostSnare);
    const int snarePitch = snareInfo != nullptr ? snareInfo->defaultMidiNote : 38;
    const int clapPitch = clapInfo != nullptr ? clapInfo->defaultMidiNote : 39;

    std::uniform_int_distribution<int> snareVelocity(98, 118);
    std::uniform_int_distribution<int> layerVelocity(68, 90);
    std::uniform_int_distribution<int> ghostVelocity(42, 70);
    std::uniform_int_distribution<int> layerDrag(6, 14);
    std::uniform_int_distribution<int> pushOffset(-14, -4);
    std::uniform_int_distribution<int> dragOffset(8, 20);

    for (const auto& bar : phrasePlan.bars)
    {
        const int barStart = bar.barIndex * 16;
        const int primarySnare = bar.anchorMap.snareAnchorSteps[0];
        const auto decorationMode = chooseDecorationMode(bar, rng);

        for (const int stepInBar : bar.anchorMap.snareAnchorSteps)
        {
            if (stepInBar < 0)
                continue;

            NoteEvent note;
            note.pitch = snarePitch;
            note.step = barStart + stepInBar;
            note.length = 1;
            note.velocity = snareVelocity(rng);
            note.microOffset = 0;
            note.isGhost = false;
            note.semanticRole = "drill_snare_backbone";
            snareTrack.notes.push_back(note);

            if (clapGhostTrack != nullptr && decorationMode == DrillSnareDecorationMode::ClapLayer)
            {
                NoteEvent layer;
                layer.pitch = clapPitch;
                layer.step = barStart + stepInBar;
                layer.length = 1;
                layer.velocity = layerVelocity(rng);
                layer.microOffset = layerDrag(rng);
                layer.isGhost = false;
                layer.semanticRole = "drill_clap_layer";
                clapGhostTrack->notes.push_back(layer);
            }
        }

        if (clapGhostTrack == nullptr || primarySnare < 0 || !isGhostMode(decorationMode))
            continue;

        const auto ghostCandidates = candidateGhostStepsForMode(bar, decorationMode, primarySnare);
        if (ghostCandidates.empty())
            continue;

        NoteEvent ghost;
        ghost.pitch = clapPitch;
        ghost.step = barStart + ghostCandidates.front();
        ghost.length = 1;
        ghost.velocity = ghostVelocity(rng);
        ghost.microOffset = decorationMode == DrillSnareDecorationMode::DragGhost ? dragOffset(rng) : pushOffset(rng);
        ghost.isGhost = true;
        ghost.semanticRole = "drill_snare_ghost";
        clapGhostTrack->notes.push_back(ghost);
    }

    std::sort(snareTrack.notes.begin(), snareTrack.notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
    {
        return lhs.step < rhs.step;
    });

    if (clapGhostTrack != nullptr)
    {
        std::sort(clapGhostTrack->notes.begin(), clapGhostTrack->notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
        {
            if (lhs.step != rhs.step)
                return lhs.step < rhs.step;
            return lhs.velocity > rhs.velocity;
        });
    }
}
} // namespace bbg