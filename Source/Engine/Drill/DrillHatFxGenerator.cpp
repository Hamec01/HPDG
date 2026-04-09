#include "DrillHatFxGenerator.h"

#include <algorithm>
#include <vector>

#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"

namespace bbg
{
namespace
{
struct HatFxCandidate
{
    int tick = 0;
    int velocity = 72;
    juce::String semanticRole;
    int priority = 0;
};

int maxHatFxPerBar(DrillHatDensityIntent density, DrillPhraseBarRole role)
{
    switch (density)
    {
        case DrillHatDensityIntent::Sparse: return role == DrillPhraseBarRole::Release ? 2 : 1;
        case DrillHatDensityIntent::Dense: return role == DrillPhraseBarRole::Release ? 3 : 2;
        case DrillHatDensityIntent::Medium:
        default:
            return role == DrillPhraseBarRole::Release ? 2 : 1;
    }
}

bool withinBar(int tick, int barStartTick)
{
    return tick >= barStartTick && tick < barStartTick + HiResTiming::kTicksPerBar4_4;
}

void addCandidate(std::vector<HatFxCandidate>& out,
                  int tick,
                  int velocity,
                  const juce::String& semanticRole,
                  int priority)
{
    out.push_back({ tick, velocity, semanticRole, priority });
}

void dedupeCandidates(std::vector<HatFxCandidate>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const HatFxCandidate& lhs, const HatFxCandidate& rhs)
    {
        if (lhs.tick != rhs.tick)
            return lhs.tick < rhs.tick;
        if (lhs.priority != rhs.priority)
            return lhs.priority > rhs.priority;
        return lhs.velocity > rhs.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const HatFxCandidate& lhs, const HatFxCandidate& rhs)
    {
        return lhs.tick == rhs.tick;
    }), notes.end());
}
} // namespace

void DrillHatFxGenerator::generate(TrackState& track,
                                   const TrackState& hiHatTrack,
                                   const PatternProject& project,
                                   const DrillPhrasePlan& phrasePlan,
                                   std::mt19937& rng) const
{
    juce::ignoreUnused(project);

    track.notes.clear();
    track.subProfile = "Main";
    track.laneRole = "drill_hat_fx";

    const auto* info = TrackRegistry::find(TrackType::HatFX);
    const int pitch = info != nullptr ? info->defaultMidiNote : 44;
    std::uniform_int_distribution<int> accentVel(58, 88);

    for (const auto& bar : phrasePlan.bars)
    {
        std::vector<HatFxCandidate> candidates;
        candidates.reserve(8);
        const int barStartStep = bar.barIndex * 16;
        const int barStartTick = bar.barIndex * HiResTiming::kTicksPerBar4_4;
        const int snareAnchor = bar.anchorMap.snareAnchorSteps[0];

        for (const auto& note : hiHatTrack.notes)
        {
            if ((note.step / 16) != bar.barIndex)
                continue;

            const int stepInBar = note.step - barStartStep;
            const bool transitionLike = note.semanticRole != "drill_hat_backbone"
                || stepInBar >= 12
                || (snareAnchor >= 0 && std::abs(stepInBar - snareAnchor) <= 1)
                || bar.isPhraseEnd;
            if (!transitionLike)
                continue;

            int tick = HiResTiming::noteTick(note);
            int shifted = tick + (stepInBar >= snareAnchor && snareAnchor >= 0 ? HiResTiming::kTicks1_32 : -HiResTiming::kTicks1_32);
            if (!withinBar(shifted, barStartTick))
                shifted = tick + HiResTiming::kTicks1_32;
            if (!withinBar(shifted, barStartTick))
                continue;

            addCandidate(candidates,
                         shifted,
                         accentVel(rng),
                         stepInBar >= 12 ? "drill_hat_fx_edge" : "drill_hat_fx_transition",
                         note.semanticRole == "drill_hat_triplet" ? 85 : 72);
        }

        if (bar.isPhraseEnd || bar.role == DrillPhraseBarRole::Lift)
        {
            const int edgeTick = barStartTick + 15 * HiResTiming::kTicks1_16 - HiResTiming::kTicks1_32;
            if (withinBar(edgeTick, barStartTick))
                addCandidate(candidates, edgeTick, accentVel(rng), "drill_hat_fx_phrase_edge", 96);
        }

        dedupeCandidates(candidates);
        const int maxNotes = maxHatFxPerBar(bar.hatDensity, bar.role);
        if (static_cast<int>(candidates.size()) > maxNotes)
            candidates.resize(static_cast<size_t>(maxNotes));

        for (const auto& candidate : candidates)
        {
            HiResTiming::addNoteAtTick(track, pitch, candidate.tick, candidate.velocity, false, phrasePlan.phraseSpanBars);
            track.notes.back().semanticRole = candidate.semanticRole;
        }
    }

    std::sort(track.notes.begin(), track.notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
    {
        if (lhs.step != rhs.step)
            return lhs.step < rhs.step;
        if (lhs.microOffset != rhs.microOffset)
            return lhs.microOffset < rhs.microOffset;
        return lhs.velocity > rhs.velocity;
    });
}
} // namespace bbg