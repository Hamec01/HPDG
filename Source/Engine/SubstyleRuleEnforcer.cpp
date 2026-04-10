#include "SubstyleRuleEnforcer.h"

#include <algorithm>

#include "StyleDefaults.h"

namespace bbg
{
namespace
{
int stepInBar(int step)
{
    const int normalized = step % 16;
    return normalized < 0 ? normalized + 16 : normalized;
}

bool noteSequencesEqual(const std::vector<NoteEvent>& left, const std::vector<NoteEvent>& right)
{
    if (left.size() != right.size())
        return false;

    for (size_t index = 0; index < left.size(); ++index)
    {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.pitch != rhs.pitch
            || lhs.step != rhs.step
            || lhs.length != rhs.length
            || lhs.velocity != rhs.velocity
            || lhs.microOffset != rhs.microOffset
            || lhs.isGhost != rhs.isGhost
            || lhs.semanticRole != rhs.semanticRole
            || lhs.isSlide != rhs.isSlide
            || lhs.isLegato != rhs.isLegato
            || lhs.glideToNext != rhs.glideToNext)
        {
            return false;
        }
    }

    return true;
}

float classicPriority(TrackType lane, const NoteEvent& note)
{
    float score = static_cast<float>(note.velocity) / 127.0f;

    if (note.semanticRole.containsIgnoreCase("backbone") || note.semanticRole.containsIgnoreCase("anchor"))
        score += 1.8f;
    if (note.semanticRole.containsIgnoreCase("sample_copy"))
        score += 1.1f;
    if (stepInBar(note.step) >= 12)
        score += 0.2f;
    if (lane == TrackType::ClapGhostSnare || lane == TrackType::GhostKick)
        score += note.isGhost ? 0.15f : 0.0f;

    return score;
}

void dedupeAndSort(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& left, const NoteEvent& right)
    {
        if (left.step != right.step)
            return left.step < right.step;
        if (left.pitch != right.pitch)
            return left.pitch < right.pitch;
        return left.velocity > right.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& left, const NoteEvent& right)
    {
        return left.step == right.step && left.pitch == right.pitch;
    }), notes.end());
}

int prunePerBar(TrackState& track, int bars, int maxPerBar)
{
    std::vector<NoteEvent> filtered;
    filtered.reserve(track.notes.size());
    int pruned = 0;

    for (int bar = 0; bar < bars; ++bar)
    {
        std::vector<NoteEvent> barNotes;
        for (const auto& note : track.notes)
        {
            if ((note.step / 16) == bar)
                barNotes.push_back(note);
        }

        std::stable_sort(barNotes.begin(), barNotes.end(), [&](const NoteEvent& left, const NoteEvent& right)
        {
            const float leftScore = classicPriority(track.type, left);
            const float rightScore = classicPriority(track.type, right);
            if (std::abs(leftScore - rightScore) > 0.001f)
                return leftScore > rightScore;
            if (left.step != right.step)
                return left.step < right.step;
            return left.pitch < right.pitch;
        });

        if (static_cast<int>(barNotes.size()) > maxPerBar)
        {
            pruned += static_cast<int>(barNotes.size()) - maxPerBar;
            barNotes.resize(static_cast<size_t>(maxPerBar));
        }

        dedupeAndSort(barNotes);
        filtered.insert(filtered.end(), barNotes.begin(), barNotes.end());
    }

    dedupeAndSort(filtered);
    track.notes = std::move(filtered);
    return pruned;
}
}

juce::String describeSubstyleRuleReport(const SubstyleRuleReport& report)
{
    if (!report.applied)
        return "Classic rules: inactive";

    return "Classic rules: pruned " + juce::String(report.prunedNotes) + " decorative notes";
}

SubstyleRuleReport SubstyleRuleEnforcer::enforce(PatternProject& project)
{
    SubstyleRuleReport report;

    const auto& style = getGenreStyleDefaults(project.params.genre, getSelectedSubstyleIndex(project.params));
    if (!style.substyleName.containsIgnoreCase("Classic"))
        return report;

    report.applied = true;
    const int bars = juce::jmax(1, project.params.bars);

    const auto snareTrackIt = std::find_if(project.tracks.begin(), project.tracks.end(), [](const TrackState& track)
    {
        return track.type == TrackType::Snare;
    });

    std::unordered_set<int> protectedSnareSteps;
    if (snareTrackIt != project.tracks.end())
    {
        for (const auto& note : snareTrackIt->notes)
            protectedSnareSteps.insert(note.step);
    }

    for (auto& track : project.tracks)
    {
        if (track.locked || track.notes.empty())
            continue;

        const auto beforeNotes = track.notes;

        if (track.type == TrackType::ClapGhostSnare)
        {
            track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
            {
                return protectedSnareSteps.count(note.step) > 0;
            }), track.notes.end());
            report.prunedNotes += static_cast<int>(beforeNotes.size()) - static_cast<int>(track.notes.size());
            report.prunedNotes += prunePerBar(track, bars, 1);
        }
        else if (track.type == TrackType::GhostKick)
        {
            report.prunedNotes += prunePerBar(track, bars, 1);
        }
        else if (track.type == TrackType::OpenHat
                 || track.type == TrackType::Ride
                 || track.type == TrackType::Cymbal
                 || track.type == TrackType::Perc
                 || track.type == TrackType::HatFX)
        {
            report.prunedNotes += prunePerBar(track, bars, 1);
        }
        else if ((track.type == TrackType::HiHat || track.type == TrackType::Ride)
                 && project.params.genre != GenreType::Trap)
        {
            report.prunedNotes += prunePerBar(track, bars, 12);
        }

        if (!noteSequencesEqual(beforeNotes, track.notes))
            report.changedTracks.insert(track.type);
    }

    return report;
}
} // namespace bbg