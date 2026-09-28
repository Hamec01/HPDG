#include "DrillPatternValidator.h"

#include <algorithm>
#include <array>

#include "../../Core/Sub808Types.h"
#include "../../Core/TimingGrid.h"
#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"

namespace bbg
{
namespace
{
// This validator's anchor maps (DrillPhraseBarPlan) are expressed as 1/16 step-in-bar indices —
// that template representation is fine to keep (TIMING GRID V2, Sub-Drill section). These two
// helpers are the only bridge between that and the now tick-native NoteEvent/Sub808NoteEvent.
template <typename Note>
int stepIndexOf(const Note& note)
{
    return note.gridTick / TimingGrid::Sixteenth;
}

int tickForStep(int step)
{
    return step * TimingGrid::Sixteenth;
}

TrackState* findTrack(PatternProject& project, TrackType type)
{
    for (auto& track : project.tracks)
    {
        if (track.type == type)
            return &track;
    }

    return nullptr;
}

bool canTouch(const std::unordered_set<TrackType>& mutableTracks, TrackType type)
{
    return mutableTracks.find(type) != mutableTracks.end();
}

int hatMaxPerBar(DrillHatDensityIntent density)
{
    switch (density)
    {
        case DrillHatDensityIntent::Sparse: return 8;
        case DrillHatDensityIntent::Dense: return 14;
        case DrillHatDensityIntent::Medium:
        default: return 11;
    }
}

int maxKickNotesForBar(const DrillPhraseBarPlan& bar)
{
    return bar.role == DrillPhraseBarRole::Lift || bar.role == DrillPhraseBarRole::Release ? 3 : 2;
}

int maxSubStartsForBar(const DrillPhraseBarPlan& bar)
{
    return bar.lowEnd == DrillLowEndIntent::Move || bar.lowEnd == DrillLowEndIntent::Release ? 2 : 1;
}

bool isReferenceHatSemantic(const juce::String& semanticRole)
{
    return semanticRole.startsWith("drill_hat_reference");
}

bool isProtectedHatSemantic(const juce::String& semanticRole)
{
    return semanticRole == "drill_hat_backbone" || isReferenceHatSemantic(semanticRole);
}

int semanticPriority(const juce::String& semanticRole)
{
    if (isReferenceHatSemantic(semanticRole))
        return 7;
    if (semanticRole == "drill_hat_backbone")
        return 6;
    if (semanticRole == "drill_snare_backbone")
        return 6;
    if (semanticRole == "drill_kick_anchor")
        return 5;
    if (semanticRole == "drill_hat_fx_phrase_edge")
        return 5;
    if (semanticRole == "drill_clap_layer")
        return 4;
    if (semanticRole.contains("triplet") || semanticRole.contains("transition") || semanticRole.contains("release"))
        return 3;
    return 2;
}

bool collidesWithSnare(const DrillPhraseBarPlan& bar, int stepInBar)
{
    for (const int snareStep : bar.anchorMap.snareAnchorSteps)
    {
        if (snareStep >= 0 && snareStep == stepInBar)
            return true;
    }

    return false;
}

int primarySnareStep(const DrillPhraseBarPlan& bar)
{
    return bar.anchorMap.snareAnchorSteps[0];
}

bool isSnareAnchorStep(const DrillPhraseBarPlan& bar, int stepInBar)
{
    for (const int snareStep : bar.anchorMap.snareAnchorSteps)
    {
        if (snareStep >= 0 && snareStep == stepInBar)
            return true;
    }

    return false;
}

bool isClapLayerCandidate(const DrillPhraseBarPlan& bar, const NoteEvent& note)
{
    return note.semanticRole == "drill_clap_layer" && isSnareAnchorStep(bar, stepIndexOf(note) % 16);
}

bool isGhostCandidate(const DrillPhraseBarPlan& bar, const NoteEvent& note)
{
    if (note.semanticRole != "drill_snare_ghost")
        return false;

    const int primarySnare = primarySnareStep(bar);
    if (primarySnare < 0)
        return false;

    const int stepInBar = stepIndexOf(note) % 16;
    const int distance = std::abs(stepInBar - primarySnare);
    if (distance < 1 || distance > 2)
        return false;

    if (bar.supportAccent == DrillSupportAccentIntent::Push)
        return stepInBar < primarySnare;
    if (bar.supportAccent == DrillSupportAccentIntent::Drag)
        return stepInBar > primarySnare;
    return false;
}

int ghostCandidateScore(const DrillPhraseBarPlan& bar, const NoteEvent& note)
{
    const int primarySnare = primarySnareStep(bar);
    const int stepInBar = stepIndexOf(note) % 16;
    const int distanceScore = std::abs(stepInBar - primarySnare) == 1 ? 160 : 120;
    const int offsetScore = bar.supportAccent == DrillSupportAccentIntent::Push
        ? std::max(0, -note.timingOffsetTicks)
        : std::max(0, note.timingOffsetTicks);
    return distanceScore + offsetScore + note.velocity;
}

void sanitizeClapGhostTrack(TrackState& clapGhost, const DrillPhrasePlan& phrasePlan)
{
    std::vector<NoteEvent> filtered;
    filtered.reserve(clapGhost.notes.size());

    for (const auto& bar : phrasePlan.bars)
    {
        std::vector<NoteEvent> layerCandidates;
        std::vector<NoteEvent> ghostCandidates;

        for (const auto& note : clapGhost.notes)
        {
            if ((stepIndexOf(note) / 16) != bar.barIndex)
                continue;

            if (isClapLayerCandidate(bar, note))
                layerCandidates.push_back(note);
            else if (isGhostCandidate(bar, note))
                ghostCandidates.push_back(note);
        }

        if (!layerCandidates.empty())
        {
            std::sort(layerCandidates.begin(), layerCandidates.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
            {
                if (lhs.gridTick != rhs.gridTick)
                    return lhs.gridTick < rhs.gridTick;
                return lhs.velocity > rhs.velocity;
            });

            int lastKeptStep = -1;
            for (const auto& note : layerCandidates)
            {
                if (stepIndexOf(note) == lastKeptStep)
                    continue;
                filtered.push_back(note);
                lastKeptStep = stepIndexOf(note);
            }

            continue;
        }

        if ((bar.supportAccent == DrillSupportAccentIntent::Push || bar.supportAccent == DrillSupportAccentIntent::Drag)
            && !ghostCandidates.empty())
        {
            const auto bestGhost = std::max_element(ghostCandidates.begin(), ghostCandidates.end(), [&](const NoteEvent& lhs, const NoteEvent& rhs)
            {
                return ghostCandidateScore(bar, lhs) < ghostCandidateScore(bar, rhs);
            });

            filtered.push_back(*bestGhost);
        }
    }

    clapGhost.notes = std::move(filtered);
}

void sortAndDedupeNotes(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
    {
        if (lhs.gridTick != rhs.gridTick)
            return lhs.gridTick < rhs.gridTick;
        if (lhs.timingOffsetTicks != rhs.timingOffsetTicks)
            return lhs.timingOffsetTicks < rhs.timingOffsetTicks;
        if (lhs.pitch != rhs.pitch)
            return lhs.pitch < rhs.pitch;
        const int lhsPriority = semanticPriority(lhs.semanticRole);
        const int rhsPriority = semanticPriority(rhs.semanticRole);
        if (lhsPriority != rhsPriority)
            return lhsPriority > rhsPriority;
        return lhs.velocity > rhs.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
    {
        return lhs.gridTick == rhs.gridTick && lhs.timingOffsetTicks == rhs.timingOffsetTicks && lhs.pitch == rhs.pitch;
    }), notes.end());
}

void ensureHatBackbone(TrackState& hat, const DrillPhraseBarPlan& bar)
{
    const auto* info = TrackRegistry::find(TrackType::HiHat);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;
    const int barStart = bar.barIndex * 16;

    for (const int carrier : bar.anchorMap.hatCarrierSteps)
    {
        if (carrier < 0)
            continue;

        const int step = barStart + carrier;
        const int tick = (bar.barIndex * HiResTiming::kTicksPerBar4_4) + carrier * HiResTiming::kTicks1_16;
        const auto found = std::find_if(hat.notes.begin(), hat.notes.end(), [&](const NoteEvent& note)
        {
            if (note.semanticRole == "drill_hat_backbone" && note.gridTick == tickForStep(step) && note.timingOffsetTicks == 0)
                return true;
            return isReferenceHatSemantic(note.semanticRole)
                && std::abs(HiResTiming::noteTick(note) - tick) <= HiResTiming::kTicks1_32;
        });

        if (found == hat.notes.end())
        {
            NoteEvent note;
            note.pitch = pitch;
            note.gridTick = tickForStep(step);
            note.lengthTicks = TimingGrid::Sixteenth;
            note.velocity = 86;
            note.semanticRole = "drill_hat_backbone";
            hat.notes.push_back(note);
        }
    }
}

int copiedReferenceHatsForBar(const TrackState& hat, int barIndex)
{
    int count = 0;
    for (const auto& note : hat.notes)
    {
        if ((stepIndexOf(note) / 16) == barIndex && isReferenceHatSemantic(note.semanticRole))
            ++count;
    }

    return count;
}

int hatProximityWindowTicks(const NoteEvent& note)
{
    if (isProtectedHatSemantic(note.semanticRole))
        return 96;
    if (note.semanticRole == "drill_hat_transition")
        return 84;
    if (note.semanticRole.contains("triplet") || note.semanticRole.contains("burst"))
        return 72;
    return 80;
}

void sanitizeHatProximity(TrackState& hat, const DrillPhrasePlan& phrasePlan)
{
    std::vector<NoteEvent> filtered;
    filtered.reserve(hat.notes.size());

    for (const auto& bar : phrasePlan.bars)
    {
        std::vector<NoteEvent> barNotes;
        for (const auto& note : hat.notes)
        {
            if ((stepIndexOf(note) / 16) == bar.barIndex)
                barNotes.push_back(note);
        }

        std::sort(barNotes.begin(), barNotes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
        {
            const bool lhsProtected = isProtectedHatSemantic(lhs.semanticRole);
            const bool rhsProtected = isProtectedHatSemantic(rhs.semanticRole);
            if (lhsProtected != rhsProtected)
                return lhsProtected > rhsProtected;

            const int lhsPriority = semanticPriority(lhs.semanticRole);
            const int rhsPriority = semanticPriority(rhs.semanticRole);
            if (lhsPriority != rhsPriority)
                return lhsPriority > rhsPriority;
            if (lhs.velocity != rhs.velocity)
                return lhs.velocity > rhs.velocity;
            return HiResTiming::noteTick(lhs) < HiResTiming::noteTick(rhs);
        });

        std::vector<NoteEvent> kept;
        kept.reserve(barNotes.size());
        for (const auto& note : barNotes)
        {
            if (isProtectedHatSemantic(note.semanticRole))
            {
                kept.push_back(note);
                continue;
            }

            const int candidateTick = HiResTiming::noteTick(note);
            bool conflictsWithProtectedWindow = false;
            for (const auto& existing : kept)
            {
                if (!isProtectedHatSemantic(existing.semanticRole))
                    continue;

                const int protectedTick = HiResTiming::noteTick(existing);
                const int windowTicks = std::max(hatProximityWindowTicks(note), hatProximityWindowTicks(existing));
                if (std::abs(candidateTick - protectedTick) <= windowTicks)
                {
                    conflictsWithProtectedWindow = true;
                    break;
                }
            }

            if (!conflictsWithProtectedWindow)
                kept.push_back(note);
        }

        filtered.insert(filtered.end(), kept.begin(), kept.end());
    }

    hat.notes = std::move(filtered);
}

void trimNotesPerBar(std::vector<NoteEvent>& notes, int barIndex, int maxNotes)
{
    std::vector<size_t> indices;
    for (size_t index = 0; index < notes.size(); ++index)
        if ((stepIndexOf(notes[index]) / 16) == barIndex)
            indices.push_back(index);

    if (static_cast<int>(indices.size()) <= maxNotes)
        return;

    std::sort(indices.begin(), indices.end(), [&](size_t lhs, size_t rhs)
    {
        const int leftPriority = semanticPriority(notes[lhs].semanticRole);
        const int rightPriority = semanticPriority(notes[rhs].semanticRole);
        if (leftPriority != rightPriority)
            return leftPriority > rightPriority;
        return notes[lhs].velocity > notes[rhs].velocity;
    });

    std::vector<bool> keep(notes.size(), false);
    for (int index = 0; index < maxNotes; ++index)
        keep[indices[static_cast<size_t>(index)]] = true;

    for (size_t index = 0; index < notes.size(); ++index)
    {
        if ((stepIndexOf(notes[index]) / 16) != barIndex)
            keep[index] = true;
    }

    std::vector<NoteEvent> filtered;
    filtered.reserve(notes.size());
    for (size_t index = 0; index < notes.size(); ++index)
        if (keep[index])
            filtered.push_back(notes[index]);
    notes = std::move(filtered);
}

int snapToScale(int pitch, int keyRoot, int scaleMode)
{
    const auto intervalsForScale = [&](int mode) -> const std::array<int, 7>&
    {
        static const std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
        static const std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
        static const std::array<int, 7> harmonicMinor { 0, 2, 3, 5, 7, 8, 11 };
        if (mode == 1)
            return major;
        if (mode == 2)
            return harmonicMinor;
        return minor;
    };

    const auto& intervals = intervalsForScale(scaleMode);
    const int clamped = std::clamp(pitch, 12, 72);
    int bestPitch = clamped;
    int bestDistance = 128;
    for (int candidate = std::max(12, clamped - 12); candidate <= std::min(72, clamped + 12); ++candidate)
    {
        const int pitchClass = ((candidate - keyRoot) % 12 + 12) % 12;
        if (std::find(intervals.begin(), intervals.end(), pitchClass) == intervals.end())
            continue;
        const int distance = std::abs(candidate - clamped);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestPitch = candidate;
        }
    }
    return bestPitch;
}

void cleanMonophonicSub(std::vector<Sub808NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const Sub808NoteEvent& lhs, const Sub808NoteEvent& rhs)
    {
        if (lhs.gridTick != rhs.gridTick)
            return lhs.gridTick < rhs.gridTick;
        return lhs.velocity > rhs.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const Sub808NoteEvent& lhs, const Sub808NoteEvent& rhs)
    {
        return lhs.gridTick == rhs.gridTick;
    }), notes.end());

    for (size_t index = 0; index + 1 < notes.size(); ++index)
    {
        auto& current = notes[index];
        const auto& next = notes[index + 1];
        const int allowedOverlapSteps = (current.glideToNext || current.isLegato) ? 1 : 0;
        const int gapSteps = stepIndexOf(next) - stepIndexOf(current);
        const int currentLengthSteps = current.lengthTicks / TimingGrid::Sixteenth;
        current.lengthTicks = std::max(1, std::min(currentLengthSteps, gapSteps + allowedOverlapSteps)) * TimingGrid::Sixteenth;
    }
}

int subSemanticPriority(const juce::String& semanticRole)
{
    if (semanticRole == "drill_sub_release")
        return 4;
    if (semanticRole == "drill_sub_move")
        return 3;
    if (semanticRole == "drill_sub_anchor")
        return 2;
    if (semanticRole == "drill_sub_hold")
        return 1;
    return 0;
}

bool isLowEndAnchorStep(const DrillPhraseBarPlan& bar, int stepInBar)
{
    for (const int step : bar.anchorMap.lowEndAnchorSteps)
    {
        if (step >= 0 && step == stepInBar)
            return true;
    }

    return false;
}

bool isPickupLikeSubStep(int stepInBar)
{
    return stepInBar >= 12;
}

int subNoteScore(const DrillPhraseBarPlan& bar, const Sub808NoteEvent& note)
{
    const int stepInBar = stepIndexOf(note) % 16;
    int score = subSemanticPriority(note.semanticRole) * 100 + note.velocity;
    if (stepInBar == 0)
        score += 40;
    if (isLowEndAnchorStep(bar, stepInBar))
        score += 60;
    if (bar.lowEnd == DrillLowEndIntent::Move && stepInBar >= 6 && stepInBar <= 13)
        score += 30;
    if (bar.lowEnd == DrillLowEndIntent::Release && isPickupLikeSubStep(stepInBar))
        score += 40;
    return score;
}

void sanitizeSubStarts(std::vector<Sub808NoteEvent>& notes, const DrillPhrasePlan& phrasePlan)
{
    std::vector<Sub808NoteEvent> filtered;
    filtered.reserve(notes.size());

    for (const auto& bar : phrasePlan.bars)
    {
        std::vector<Sub808NoteEvent> barNotes;
        for (const auto& note : notes)
        {
            if ((stepIndexOf(note) / 16) == bar.barIndex)
                barNotes.push_back(note);
        }

        std::sort(barNotes.begin(), barNotes.end(), [&](const Sub808NoteEvent& lhs, const Sub808NoteEvent& rhs)
        {
            const int lhsScore = subNoteScore(bar, lhs);
            const int rhsScore = subNoteScore(bar, rhs);
            if (lhsScore != rhsScore)
                return lhsScore > rhsScore;
            return lhs.gridTick < rhs.gridTick;
        });

        std::vector<Sub808NoteEvent> keptBar;
        keptBar.reserve(barNotes.size());
        for (const auto& note : barNotes)
        {
            if (static_cast<int>(keptBar.size()) >= maxSubStartsForBar(bar))
                break;

            const int stepInBar = stepIndexOf(note) % 16;
            const bool tooClose = std::any_of(keptBar.begin(), keptBar.end(), [&](const Sub808NoteEvent& existing)
            {
                return std::abs((stepIndexOf(existing) % 16) - stepInBar) < 4;
            });

            if (!tooClose)
                keptBar.push_back(note);
        }

        if (keptBar.empty() && !barNotes.empty())
            keptBar.push_back(barNotes.front());

        std::sort(keptBar.begin(), keptBar.end(), [](const Sub808NoteEvent& lhs, const Sub808NoteEvent& rhs)
        {
            return lhs.gridTick < rhs.gridTick;
        });

        filtered.insert(filtered.end(), keptBar.begin(), keptBar.end());
    }

    notes = std::move(filtered);
}

void sanitizeSubSlides(std::vector<Sub808NoteEvent>& notes, const DrillPhrasePlan& phrasePlan)
{
    for (auto& note : notes)
    {
        note.glideToNext = false;
        note.isLegato = false;
        note.isSlide = false;
    }

    int slideBudget = std::max(1, phrasePlan.phraseSpanBars / 4);
    for (size_t index = 0; index + 1 < notes.size(); ++index)
    {
        auto& current = notes[index];
        auto& next = notes[index + 1];
        const int gap = stepIndexOf(next) - stepIndexOf(current);
        const int interval = std::abs(next.pitch - current.pitch);
        const bool sameBar = (stepIndexOf(current) / 16) == (stepIndexOf(next) / 16);
        const bool phraseSlide = current.semanticRole == "drill_sub_release"
            || (current.semanticRole == "drill_sub_move" && current.lengthTicks <= 3 * TimingGrid::Sixteenth);

        if (slideBudget > 0 && sameBar && phraseSlide && gap >= 2 && gap <= 3 && interval >= 2 && interval <= 5)
        {
            current.glideToNext = true;
            current.isLegato = true;
            next.isSlide = true;
            current.lengthTicks = std::max(current.lengthTicks, (gap + 1) * TimingGrid::Sixteenth);
            --slideBudget;
        }
    }
}
} // namespace

void DrillPatternValidator::validate(PatternProject& project,
                                     const DrillPhrasePlan& phrasePlan,
                                     const std::unordered_set<TrackType>& mutableTracks) const
{
    if (auto* hat = findTrack(project, TrackType::HiHat); hat != nullptr && canTouch(mutableTracks, TrackType::HiHat) && hat->enabled && !hat->locked)
    {
        for (const auto& bar : phrasePlan.bars)
            ensureHatBackbone(*hat, bar);

        sortAndDedupeNotes(hat->notes);
        sanitizeHatProximity(*hat, phrasePlan);
        sortAndDedupeNotes(hat->notes);
        for (const auto& bar : phrasePlan.bars)
            trimNotesPerBar(hat->notes,
                            bar.barIndex,
                            std::max(hatMaxPerBar(bar.hatDensity), copiedReferenceHatsForBar(*hat, bar.barIndex)));

        for (auto& note : hat->notes)
        {
            if (note.semanticRole == "drill_hat_backbone")
                note.timingOffsetTicks = 0;
            else if (isReferenceHatSemantic(note.semanticRole))
                note.timingOffsetTicks = std::clamp(note.timingOffsetTicks, -120, 120);
            else
                note.timingOffsetTicks = std::clamp(note.timingOffsetTicks, -60, 90);
            note.velocity = std::clamp(note.velocity,
                                       isReferenceHatSemantic(note.semanticRole) ? 36 : 44,
                                       isReferenceHatSemantic(note.semanticRole) ? 118 : 108);
        }
    }

    if (auto* hatFx = findTrack(project, TrackType::HatFX); hatFx != nullptr && canTouch(mutableTracks, TrackType::HatFX) && hatFx->enabled && !hatFx->locked)
    {
        auto* hat = findTrack(project, TrackType::HiHat);
        sortAndDedupeNotes(hatFx->notes);
        if (hat != nullptr)
        {
            hatFx->notes.erase(std::remove_if(hatFx->notes.begin(), hatFx->notes.end(), [&](const NoteEvent& fx)
            {
                for (const auto& main : hat->notes)
                {
                    if (main.semanticRole != "drill_hat_backbone")
                        continue;
                    if (std::abs(HiResTiming::noteTick(main) - HiResTiming::noteTick(fx)) <= 80)
                        return true;
                }
                return false;
            }), hatFx->notes.end());
        }

        for (const auto& bar : phrasePlan.bars)
            trimNotesPerBar(hatFx->notes, bar.barIndex, bar.role == DrillPhraseBarRole::Release ? 3 : 2);

        for (auto& note : hatFx->notes)
        {
            note.timingOffsetTicks = std::clamp(note.timingOffsetTicks, -120, 120);
            note.velocity = std::clamp(note.velocity, 40, 96);
        }
    }

    if (auto* snare = findTrack(project, TrackType::Snare); snare != nullptr && canTouch(mutableTracks, TrackType::Snare) && snare->enabled && !snare->locked)
    {
        sortAndDedupeNotes(snare->notes);
        snare->notes.erase(std::remove_if(snare->notes.begin(), snare->notes.end(), [](const NoteEvent& note)
        {
            return note.semanticRole != "drill_snare_backbone";
        }), snare->notes.end());

        const auto* info = TrackRegistry::find(TrackType::Snare);
        const int pitch = info != nullptr ? info->defaultMidiNote : 38;
        for (const auto& bar : phrasePlan.bars)
        {
            for (const int snareStep : bar.anchorMap.snareAnchorSteps)
            {
                if (snareStep < 0)
                    continue;
                const int absoluteStep = bar.barIndex * 16 + snareStep;
                const auto found = std::find_if(snare->notes.begin(), snare->notes.end(), [&](const NoteEvent& note)
                {
                    return note.gridTick == tickForStep(absoluteStep) && note.semanticRole == "drill_snare_backbone";
                });
                if (found == snare->notes.end())
                {
                    NoteEvent note;
                    note.pitch = pitch;
                    note.gridTick = tickForStep(absoluteStep);
                    note.lengthTicks = TimingGrid::Sixteenth;
                    note.velocity = 104;
                    note.semanticRole = "drill_snare_backbone";
                    snare->notes.push_back(note);
                }
            }
        }

        sortAndDedupeNotes(snare->notes);
    }

    if (auto* clapGhost = findTrack(project, TrackType::ClapGhostSnare); clapGhost != nullptr && canTouch(mutableTracks, TrackType::ClapGhostSnare) && clapGhost->enabled && !clapGhost->locked)
    {
        sortAndDedupeNotes(clapGhost->notes);
        sanitizeClapGhostTrack(*clapGhost, phrasePlan);
        sortAndDedupeNotes(clapGhost->notes);

        for (auto& note : clapGhost->notes)
        {
            if (note.semanticRole == "drill_clap_layer")
            {
                note.timingOffsetTicks = std::clamp(note.timingOffsetTicks, 0, 24);
                note.velocity = std::clamp(note.velocity, 56, 92);
            }
            else
            {
                note.timingOffsetTicks = std::clamp(note.timingOffsetTicks, -18, 40);
                note.velocity = std::clamp(note.velocity, 34, 76);
                note.isGhost = true;
            }
        }
    }

    if (auto* kick = findTrack(project, TrackType::Kick); kick != nullptr && canTouch(mutableTracks, TrackType::Kick) && kick->enabled && !kick->locked)
    {
        sortAndDedupeNotes(kick->notes);
        kick->notes.erase(std::remove_if(kick->notes.begin(), kick->notes.end(), [&](const NoteEvent& note)
        {
            const int barIndex = stepIndexOf(note) / 16;
            const int stepInBar = stepIndexOf(note) % 16;
            if (barIndex < 0 || barIndex >= static_cast<int>(phrasePlan.bars.size()))
                return false;
            return collidesWithSnare(phrasePlan.bars[static_cast<size_t>(barIndex)], stepInBar);
        }), kick->notes.end());

        const auto* info = TrackRegistry::find(TrackType::Kick);
        const int pitch = info != nullptr ? info->defaultMidiNote : 36;
        for (const auto& bar : phrasePlan.bars)
        {
            const int absoluteAnchor = bar.barIndex * 16 + (bar.anchorMap.kickAnchorSteps[0] >= 0 ? bar.anchorMap.kickAnchorSteps[0] : 0);
            const auto found = std::find_if(kick->notes.begin(), kick->notes.end(), [&](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar.barIndex;
            });
            if (found == kick->notes.end())
            {
                NoteEvent note;
                note.pitch = pitch;
                note.gridTick = tickForStep(absoluteAnchor);
                note.lengthTicks = TimingGrid::Sixteenth;
                note.velocity = 108;
                note.semanticRole = "drill_kick_anchor";
                kick->notes.push_back(note);
            }

            trimNotesPerBar(kick->notes, bar.barIndex, maxKickNotesForBar(bar));
        }
    }

    if (auto* sub = findTrack(project, TrackType::Sub808); sub != nullptr && canTouch(mutableTracks, TrackType::Sub808) && sub->enabled && !sub->locked)
    {
        for (auto& note : sub->sub808Notes)
        {
            note.pitch = snapToScale(note.pitch, project.params.keyRoot, project.params.scaleMode);
            note.velocity = std::clamp(note.velocity, 72, 118);
            note.timingOffsetTicks = std::clamp(note.timingOffsetTicks, -24, 24);
        }

        sanitizeSubStarts(sub->sub808Notes, phrasePlan);
        sanitizeSubSlides(sub->sub808Notes, phrasePlan);
        cleanMonophonicSub(sub->sub808Notes);

        for (const auto& bar : phrasePlan.bars)
        {
            const int primarySnare = bar.anchorMap.snareAnchorSteps[0];
            for (auto& note : sub->sub808Notes)
            {
                if ((stepIndexOf(note) / 16) != bar.barIndex || primarySnare <= 0)
                    continue;

                const int stepInBar = stepIndexOf(note) % 16;
                const int lengthSteps = note.lengthTicks / TimingGrid::Sixteenth;
                if (stepInBar < primarySnare && stepInBar + lengthSteps > primarySnare)
                    note.lengthTicks = std::max(1, primarySnare - stepInBar) * TimingGrid::Sixteenth;
            }
        }

        if (sub->sub808Notes.empty())
        {
            const int step = phrasePlan.bars.empty() ? 0 : std::max(0, phrasePlan.bars.front().anchorMap.lowEndAnchorSteps[0]);
            Sub808NoteEvent note;
            note.pitch = 24 + project.params.keyRoot;
            note.gridTick = tickForStep(step);
            note.lengthTicks = 4 * TimingGrid::Sixteenth;
            note.velocity = 96;
            note.semanticRole = "drill_sub_anchor";
            sub->sub808Notes.push_back(note);
        }

        sub->sub808Settings.mono = true;
        sub->sub808Settings.cutItself = true;
        sub->sub808Settings.overlapMode = Sub808OverlapMode::Glide;
        sub->sub808Settings.glideTimeMs = std::clamp(sub->sub808Settings.glideTimeMs, 80, 180);
        sub->sub808Settings.scaleSnapPolicy = Sub808ScaleSnapPolicy::ForceToScale;
        sub->notes = toLegacyNoteEvents(sub->sub808Notes);
    }
}
} // namespace bbg