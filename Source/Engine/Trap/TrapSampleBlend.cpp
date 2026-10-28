#include "TrapSampleBlend.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "../../Core/ProjectLaneAccess.h"
#include "../../Core/ProjectStateController.h"
#include "../../Core/TimingGrid.h"
#include "TrapAlgebraEngine.h"
#include "TrapTempoContext.h"

namespace bbg
{
namespace
{
constexpr int kMaxKickSnapTicks = 70;     // a loop kick further off the 1/16 grid is not a grid kick
constexpr int kMaxHatSnapTicks = 40;      // a loop hat further off 1/32 and 1/16-triplet is smeared
constexpr int kMinHatGapTicks = 110;      // closer than ~1/32: two readings of one hat
constexpr int kMinSampleHatsPerBar = 4;   // fewer: the transcription missed the hat line there
constexpr int kBassOnKickTicks = 60;

struct StyleGrid
{
    TrapTempoContext tempo;
    int styleTicksPerHostBar = 64;

    std::pair<int, int> barAndLocal(int hostTick) const
    {
        const int styleTick = static_cast<int>(static_cast<long long>(hostTick) * styleTicksPerHostBar / TimingGrid::TicksPerBar4_4);
        return { styleTick / 64, styleTick % 64 };
    }
    int styleBars(int loopTicks) const
    {
        return std::max(1, static_cast<int>(static_cast<long long>(loopTicks) * styleTicksPerHostBar / TimingGrid::TicksPerBar4_4 / 64));
    }
    static int maxKicks(int styleBar) { return styleBar % 4 == 3 ? 4 : 3; }
};

// The loop's notes on the pattern grid, repeated under the whole pattern.
std::vector<NoteEvent> mapLoop(const std::vector<NoteEvent>& notes, double ratio, int sampleTicks, int loopTicks)
{
    std::vector<NoteEvent> out;
    if (sampleTicks <= 0)
        return out;
    for (int base = 0; base < loopTicks; base += sampleTicks)
        for (auto note : notes)
        {
            const int tick = base + static_cast<int>(std::lround((note.gridTick + note.timingOffsetTicks) * ratio));
            if (tick < 0 || tick >= loopTicks || tick - base >= sampleTicks)
                continue;
            note.gridTick = tick;
            note.timingOffsetTicks = 0;
            out.push_back(note);
        }
    return out;
}

int snapHat(int tick)
{
    const int to32 = static_cast<int>(std::lround(tick / 120.0)) * 120;
    const int toTriplet = static_cast<int>(std::lround(tick / 160.0)) * 160;
    return std::abs(tick - to32) <= std::abs(tick - toTriplet) ? to32 : toTriplet;
}

int medianVelocity(const std::vector<NoteEvent>& notes, int fallback)
{
    std::vector<int> v;
    for (const auto& n : notes)
        if (!n.isGhost)
            v.push_back(n.velocity);
    if (v.empty())
        return fallback;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
    return v[v.size() / 2];
}
} // namespace

TrapSampleBlendReport TrapSampleBlend::apply(PatternProject& project,
                                             const ExtractedPatternData& extracted,
                                             double sampleBpm,
                                             double patternBpm)
{
    TrapSampleBlendReport report;
    if (project.params.genre != GenreType::Trap || sampleBpm <= 20.0 || patternBpm <= 20.0 || extracted.bars <= 0)
        return report;
    report.applied = true;

    const double ratio = std::pow(2.0, std::round(std::log2(patternBpm / sampleBpm)));
    const int loopTicks = std::max(1, project.params.bars) * TimingGrid::TicksPerBar4_4;
    const int sampleTicks = static_cast<int>(std::lround(extracted.bars * TimingGrid::TicksPerBar4_4 * ratio));
    const int intensity = std::clamp(project.intensity, 1, 5);
    StyleGrid grid;
    grid.tempo = resolveTrapTempo(project.params.bpm, project.params.bars);
    grid.styleTicksPerHostBar = static_cast<int>(64.0 * grid.tempo.clockMultiplier);

    const auto laneOf = [&](TrackType lane) { return extracted.laneNotes[static_cast<size_t>(trackTypeIndex(lane))]; };

    // Kicks: per style bar, the loop's kicks on Trap's positions, or the engine's.
    std::vector<int> engineKickTicks;
    std::vector<int> finalKickTicks;
    auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
    if (kick != nullptr && !kick->locked && kick->enabled && !kick->notes.empty())
    {
        for (const auto& n : kick->notes)
            if (!n.isGhost)
                engineKickTicks.push_back(n.gridTick);

        std::map<int, std::vector<NoteEvent>> sampleByBar;
        for (auto n : mapLoop(laneOf(TrackType::Kick), ratio, sampleTicks, loopTicks))
        {
            if (n.isGhost)
                continue;
            const int slot = static_cast<int>(std::lround(n.gridTick / static_cast<double>(TimingGrid::Sixteenth))) * TimingGrid::Sixteenth;
            const auto [bar, local] = grid.barAndLocal(slot);
            if (std::abs(n.gridTick - slot) > kMaxKickSnapTicks || slot >= loopTicks
                || !TrapAlgebraEngine::isMainKickTickAllowed(local, grid.tempo.doubleTime)
                || (intensity == 1 && slot % TimingGrid::TicksPerBeat != 0))
            {
                ++report.sampleKicksDropped;
                continue;
            }
            n.gridTick = slot;
            auto& barNotes = sampleByBar[bar];
            if (std::none_of(barNotes.begin(), barNotes.end(), [&](const NoteEvent& o) { return o.gridTick == slot; }))
                barNotes.push_back(n);
        }

        const int pitch = kick->notes.front().pitch;
        const int length = kick->notes.front().lengthTicks;
        const int median = medianVelocity(kick->notes, 112);
        std::vector<NoteEvent> notes;
        for (int bar = 0; bar < grid.styleBars(loopTicks); ++bar)
        {
            auto it = sampleByBar.find(bar);
            if (it == sampleByBar.end() || it->second.empty())
            {
                for (const auto& n : kick->notes)
                    if (grid.barAndLocal(n.gridTick).first == bar)
                        notes.push_back(n);
                ++report.kickBarsKept;
                continue;
            }
            auto barNotes = it->second;
            // Over the engine's cap: the downbeat first, then the loudest.
            std::stable_sort(barNotes.begin(), barNotes.end(), [&](const NoteEvent& a, const NoteEvent& b)
            {
                const bool aDown = grid.barAndLocal(a.gridTick).second == 0;
                const bool bDown = grid.barAndLocal(b.gridTick).second == 0;
                if (aDown != bDown)
                    return aDown;
                return a.velocity > b.velocity;
            });
            if (static_cast<int>(barNotes.size()) > StyleGrid::maxKicks(bar))
                barNotes.resize(static_cast<size_t>(StyleGrid::maxKicks(bar)));
            for (auto n : barNotes)
            {
                n.pitch = pitch;
                n.lengthTicks = length;
                n.velocity = juce::jlimit(1, 127, std::clamp(n.velocity, median - 14, median + 6));
                n.semanticRole = "trap_sample_kick";
                notes.push_back(n);
            }
            ++report.kickBarsFromSample;
        }
        std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
        for (const auto& n : notes)
            if (!n.isGhost)
                finalKickTicks.push_back(n.gridTick);
        if (report.kickBarsFromSample > 0)
        {
            // Ghost kicks of the engine stay (they are not counted against the cap).
            for (const auto& n : kick->notes)
                if (n.isGhost)
                    notes.push_back(n);
            ProjectStateController::setTrackNotes(project, TrackType::Kick, notes);
            report.changedTracks.insert(TrackType::Kick);
        }
    }

    // Hats: per bar, the loop's hats on the 1/32 / triplet grid, or the engine's. Intensity 1-2
    // keep the engine's simplified pulse.
    auto* hat = ProjectLaneAccess::findTrackState(project, TrackType::HiHat);
    if (hat != nullptr && !hat->locked && hat->enabled && intensity >= 3)
    {
        std::map<int, std::vector<NoteEvent>> sampleByBar;
        for (auto n : mapLoop(laneOf(TrackType::HiHat), ratio, sampleTicks, loopTicks))
        {
            const int snapped = snapHat(n.gridTick);
            if (std::abs(n.gridTick - snapped) > kMaxHatSnapTicks || snapped >= loopTicks)
                continue;
            n.gridTick = snapped;
            sampleByBar[snapped / TimingGrid::TicksPerBar4_4].push_back(n);
        }

        const int median = medianVelocity(hat->notes, 90);
        std::vector<NoteEvent> notes;
        bool changed = false;
        for (int bar = 0; bar < std::max(1, project.params.bars); ++bar)
        {
            auto barNotes = sampleByBar[bar];
            std::sort(barNotes.begin(), barNotes.end(), [](const NoteEvent& a, const NoteEvent& b)
            {
                return a.gridTick != b.gridTick ? a.gridTick < b.gridTick : a.velocity > b.velocity;
            });
            std::vector<NoteEvent> clean;
            for (const auto& n : barNotes)
            {
                if (!clean.empty() && n.gridTick - clean.back().gridTick < kMinHatGapTicks)
                {
                    if (n.velocity > clean.back().velocity)
                        clean.back() = n; // keep the stronger reading of a doubled hat
                    continue;
                }
                clean.push_back(n);
            }
            if (static_cast<int>(clean.size()) < kMinSampleHatsPerBar)
            {
                for (const auto& n : hat->notes)
                    if (n.gridTick / TimingGrid::TicksPerBar4_4 == bar)
                        notes.push_back(n);
                ++report.hatBarsKept;
                continue;
            }
            const int pitch = hat->notes.empty() ? 42 : hat->notes.front().pitch;
            for (auto n : clean)
            {
                n.pitch = pitch;
                n.lengthTicks = TimingGrid::Sixteenth / 2;
                n.velocity = juce::jlimit(1, 127, std::clamp(n.velocity, median - 30, median + 12));
                n.semanticRole = (n.gridTick % 240 != 0) ? "trap_sample_hat_roll" : "trap_sample_hat";
                notes.push_back(n);
            }
            ++report.hatBarsFromSample;
            changed = true;
        }
        if (changed)
        {
            ProjectStateController::setTrackNotes(project, TrackType::HiHat, notes);
            report.changedTracks.insert(TrackType::HiHat);
        }
    }

    // 808: Trap's 808 sits on the kicks. Kicks the loop moved take their 808 with them.
    auto* sub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808);
    if (report.changedTracks.count(TrackType::Kick) > 0 && sub != nullptr && !sub->locked && !sub->notes.empty())
    {
        const auto near = [](const std::vector<int>& ticks, int tick)
        {
            return std::any_of(ticks.begin(), ticks.end(), [&](int t) { return std::abs(t - tick) <= kBassOnKickTicks; });
        };
        std::vector<NoteEvent> notes;
        for (const auto& n : sub->notes)
        {
            if (near(engineKickTicks, n.startTick()) && !near(finalKickTicks, n.startTick()))
            {
                ++report.bassRemoved;
                continue;
            }
            notes.push_back(n);
        }
        std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
        const auto source = sub->notes;
        // Dark: the 808 is not on every kick (Metro Boomin's rule) - only the first kick of a bar takes one.
        const bool dark = project.params.trapSubstyle == 1;
        std::unordered_set<int> barsWithBass;
        for (const int tick : finalKickTicks)
        {
            if (dark && !barsWithBass.insert(grid.barAndLocal(tick).first).second)
                continue;
            if (std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& n) { return std::abs(n.startTick() - tick) <= kBassOnKickTicks; }))
                continue;
            // The pitch the 808 had at that moment (else its first note).
            const NoteEvent* sounding = &source.front();
            for (const auto& n : source)
                if (n.startTick() <= tick)
                    sounding = &n;
            NoteEvent added = *sounding;
            added.gridTick = tick;
            added.timingOffsetTicks = 0;
            added.isSlide = false;
            added.glideToNext = false;
            added.semanticRole = "trap_sample_808";
            notes.push_back(added);
            ++report.bassAdded;
        }
        std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
        // Monophonic: a note rings until the next one at most; an added note rings until it.
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const int next = i + 1 < notes.size() ? notes[i + 1].startTick() : loopTicks;
            if (notes[i].semanticRole == "trap_sample_808" || notes[i].endTick() > next)
                notes[i].lengthTicks = std::max(TimingGrid::Sixteenth / 2, next - notes[i].startTick());
        }
        if (report.bassAdded > 0 || report.bassRemoved > 0)
        {
            ProjectStateController::setTrackNotes(project, TrackType::Sub808, notes);
            report.changedTracks.insert(TrackType::Sub808);
        }
    }

    return report;
}

bool applyTrapStyle808Window(PatternProject& project)
{
    if (project.params.genre != GenreType::Trap || !TrapAlgebraEngine::usesStyleWindow808(project.params.trapSubstyle))
        return false;
    auto* sub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808);
    if (sub == nullptr || sub->locked || sub->notes.empty())
        return false;
    auto notes = sub->notes;
    bool moved = false;
    for (auto& n : notes)
    {
        const int pitch = TrapAlgebraEngine::styleWindow808Pitch(n.pitch, project.params.keyRoot);
        moved = moved || pitch != n.pitch;
        n.pitch = pitch;
    }
    if (moved)
        ProjectStateController::setTrackNotes(project, TrackType::Sub808, notes);
    return moved;
}

juce::String describeTrapSampleBlendReport(const TrapSampleBlendReport& report)
{
    if (!report.applied)
        return "Trap sample blend: inactive";
    return "Trap sample blend: kick bars from sample " + juce::String(report.kickBarsFromSample)
        + " / engine " + juce::String(report.kickBarsKept)
        + " (dropped " + juce::String(report.sampleKicksDropped) + " off-grammar)"
        + " | hat bars from sample " + juce::String(report.hatBarsFromSample)
        + " / engine " + juce::String(report.hatBarsKept)
        + " | 808 +" + juce::String(report.bassAdded) + " / -" + juce::String(report.bassRemoved);
}
} // namespace bbg
