// Lane strength bench: what the Strength knob (LaneStrength::apply) does to a lane when turned
// left, over many seeds of the real product path. Same command before and after a change (RULE 4).
//
//   HPDG_LaneStrengthLab [--seeds N] [--genre trap|boombap|dnb|techno|atmospheric] [--lane hihat|hataccent|openhat]
//   HPDG_LaneStrengthLab --intensity [--seeds N] [--genre ...] [--down]   (--down: levels 1-3 only, no reserves)
//
// --intensity: the rack Intensity 1-5 through the processor (setIntensity + Generate, same seed, lane knobs at 0): notes per
// bar of every lane, and the share of bars that still have a kick / a snare / an 808.
//
// Per strength (0, -25, -50, -75, -100 %) and substyle, averaged over seeds:
//   hats/bar        notes left per bar
//   rolls/bar       notes left that belong to a roll (semantic role "roll")
//   brokenRolls     share of the original rolls that lost some notes but not all (a stutter fragment)
//   gridKept        share of the original straight-grid hats (on 8ths) still there
//   quarterKept     share of the original hats on quarter notes still there
//   unevenGrid      share of bars whose straight 8th pattern differs from the one most bars play
//   offGridLeft     share of the left notes that are off the 8th grid (16ths, rolls)
#include <iostream>
#include <map>
#include <set>

#include <juce_events/juce_events.h>

#include "../Source/Core/ProjectLaneAccess.h"
#include "../Source/Engine/LaneStrength.h"
#include "../Source/Engine/StyleDefaults.h"
#include "../Source/Plugin/PluginProcessor.h"

using namespace bbg;

namespace
{
void setChoice(juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}

bool isRoll(const NoteEvent& n) { return n.semanticRole.containsIgnoreCase("roll"); }

// Rolls = runs of roll notes closer than a 16th to the previous one.
std::vector<std::vector<int>> rollGroups(const std::vector<NoteEvent>& notes)
{
    std::vector<std::vector<int>> groups;
    int lastTick = -100000;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (!isRoll(notes[i]))
            continue;
        if (groups.empty() || notes[i].gridTick - lastTick >= TimingGrid::Sixteenth)
            groups.emplace_back();
        groups.back().push_back(notes[i].gridTick);
        lastTick = notes[i].gridTick;
    }
    return groups;
}

struct Acc
{
    double sum = 0.0;
    int count = 0;
    void add(double v) { sum += v; ++count; }
    double mean() const { return count > 0 ? sum / count : 0.0; }
};

const std::vector<std::pair<TrackType, const char*>> kLanes {
    { TrackType::Kick, "kick" }, { TrackType::GhostKick, "ghostKick" }, { TrackType::Snare, "snare" },
    { TrackType::ClapGhostSnare, "clapGhost" }, { TrackType::HiHat, "hihat" }, { TrackType::HatFX, "hatAccent" },
    { TrackType::OpenHat, "openHat" }, { TrackType::Ride, "ride" }, { TrackType::Cymbal, "cymbal" },
    { TrackType::Perc, "perc" }, { TrackType::Sub808, "sub808" },
};

double barsWithNotes(const std::vector<NoteEvent>& notes, int bars)
{
    std::set<int> withNotes;
    for (const auto& n : notes)
        if (!n.isGhost)
            withNotes.insert((n.gridTick / TimingGrid::TicksPerBar4_4) % bars);
    return static_cast<double>(withNotes.size()) / bars;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    int seeds = 1000;
    juce::String genreKey = "trap";
    juce::String laneKey = "hihat";
    bool intensityMode = false;
    bool downOnly = false;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a(argv[i]);
        auto next = [&] { return i + 1 < argc ? juce::String(argv[++i]) : juce::String(); };
        if (a == "--seeds") seeds = std::max(1, next().getIntValue());
        else if (a == "--genre") genreKey = next().toLowerCase();
        else if (a == "--lane") laneKey = next().toLowerCase();
        else if (a == "--intensity") intensityMode = true;
        else if (a == "--down") downOnly = true;
    }

    struct G { const char* key; int choice; const char* param; juce::StringArray subs; };
    const std::vector<G> genres {
        { "boombap", 0, ParamIds::boombapSubstyle, getBoomBapSubstyleNames() },
        { "trap", 2, ParamIds::trapSubstyle, getTrapSubstyleNames() },
        { "dnb", 4, ParamIds::dnbSubstyle, getDnBSubstyleNames() },
        { "techno", 5, ParamIds::technoSubstyle, getTechnoSubstyleNames() },
        { "atmospheric", 6, ParamIds::atmosphericSubstyle, getAtmosphericSubstyleNames() },
    };
    if (intensityMode)
    {
        std::cout << "genre,substyle,intensity,eventsPerBar,kickBars,snareBars,bassBars";
        for (const auto& [type, name] : kLanes)
            std::cout << "," << name;
        std::cout << "\n";
        for (const auto& g : genres)
        {
            if (genreKey != g.key)
                continue;
            for (int sub = 0; sub < g.subs.size(); ++sub)
            {
                BoomBapGeneratorAudioProcessor processor;
                processor.prepareToPlay(44100.0, 512);
                auto& apvts = processor.getApvts();
                setChoice(apvts, ParamIds::genre, static_cast<float>(g.choice));
                setChoice(apvts, g.param, static_cast<float>(sub));
                processor.applySelectedStylePreset(false);
                setChoice(apvts, ParamIds::bars, 2.0f); // 4 bars
                processor.syncBarsFromState();
                setChoice(apvts, ParamIds::seedLock, 1.0f);
                std::map<int, std::map<juce::String, Acc>> acc;
                for (int seed = 1; seed <= seeds; ++seed)
                {
                    setChoice(apvts, ParamIds::seed, static_cast<float>(seed));
                    for (const int level : { 3, 2, 1, 4, 5 })
                    {
                        if (downOnly && level > 3)
                            continue;
                        // The mode applies at generation: the same seed generated in each mode.
                        processor.setIntensity(level);
                        processor.generatePattern();
                        const auto project = processor.getProjectSnapshot();
                        const int bars = std::max(1, project.params.bars);
                        auto& a = acc[level];
                        double events = 0.0;
                        for (const auto& [type, name] : kLanes)
                        {
                            const auto* track = ProjectLaneAccess::findTrackState(project, type);
                            const double perBar = track != nullptr && track->enabled ? static_cast<double>(track->notes.size()) / bars : 0.0;
                            a[name].add(perBar);
                            events += perBar;
                            if (track != nullptr && track->enabled && type == TrackType::Kick) a["kickBars"].add(barsWithNotes(track->notes, bars));
                            if (track != nullptr && track->enabled && type == TrackType::Snare) a["snareBars"].add(barsWithNotes(track->notes, bars));
                            if (track != nullptr && track->enabled && type == TrackType::Sub808) a["bassBars"].add(barsWithNotes(track->notes, bars));
                        }
                        a["events"].add(events);
                    }
                }
                for (const int level : { 1, 2, 3, 4, 5 })
                {
                    if (downOnly && level > 3)
                        continue;
                    auto& a = acc[level];
                    std::cout << g.key << "," << g.subs[sub] << "," << level << "," << juce::String(a["events"].mean(), 2) << ","
                              << juce::String(a["kickBars"].mean(), 3) << "," << juce::String(a["snareBars"].mean(), 3) << ","
                              << juce::String(a["bassBars"].mean(), 3);
                    for (const auto& [type, name] : kLanes)
                        std::cout << "," << juce::String(a[name].mean(), 2);
                    std::cout << "\n";
                }
            }
        }
        return 0;
    }

    const auto lane = laneKey == "hataccent" ? TrackType::HatFX : laneKey == "openhat" ? TrackType::OpenHat : TrackType::HiHat;
    const std::vector<float> strengths { 0.0f, -0.25f, -0.5f, -0.75f, -1.0f };

    std::cout << "genre,substyle,strength,hatsPerBar,rollsPerBar,brokenRolls,gridKept,quarterKept,unevenGrid,offGridLeft\n";
    for (const auto& g : genres)
    {
        if (genreKey != g.key)
            continue;
        for (int sub = 0; sub < g.subs.size(); ++sub)
        {
            BoomBapGeneratorAudioProcessor processor;
            processor.prepareToPlay(44100.0, 512);
            auto& apvts = processor.getApvts();
            setChoice(apvts, ParamIds::genre, static_cast<float>(g.choice));
            setChoice(apvts, g.param, static_cast<float>(sub));
            processor.applySelectedStylePreset(false);
            setChoice(apvts, ParamIds::bars, 2.0f); // 4 bars
            processor.syncBarsFromState();
            setChoice(apvts, ParamIds::seedLock, 1.0f);

            std::map<float, std::map<juce::String, Acc>> acc;
            for (int seed = 1; seed <= seeds; ++seed)
            {
                setChoice(apvts, ParamIds::seed, static_cast<float>(seed));
                processor.generatePattern();
                const auto project = processor.getProjectSnapshot();
                const auto* track = ProjectLaneAccess::findTrackState(project, lane);
                if (track == nullptr || track->baseNotes.empty())
                    continue;
                auto base = track->baseNotes;
                std::sort(base.begin(), base.end(), [](const auto& a, const auto& b) { return a.gridTick < b.gridTick; });
                const int bars = std::max(1, project.params.bars);
                const int loopTicks = bars * TimingGrid::TicksPerBar4_4;
                const auto baseRolls = rollGroups(base);
                std::set<int> baseGrid, baseQuarter;
                for (const auto& n : base)
                {
                    if (isRoll(n))
                        continue;
                    if (n.gridTick % (2 * TimingGrid::Sixteenth) == 0) baseGrid.insert(n.gridTick);
                    if (n.gridTick % (4 * TimingGrid::Sixteenth) == 0) baseQuarter.insert(n.gridTick);
                }

                for (const float s : strengths)
                {
                    const auto out = LaneStrength::apply(lane, base, s, {}, loopTicks, project.params.genre);
                    std::set<int> left;
                    int rolls = 0, offGrid = 0;
                    for (const auto& n : out)
                    {
                        left.insert(n.gridTick);
                        rolls += isRoll(n) ? 1 : 0;
                        offGrid += (isRoll(n) || n.gridTick % (2 * TimingGrid::Sixteenth) != 0) ? 1 : 0;
                    }
                    int broken = 0;
                    for (const auto& r : baseRolls)
                    {
                        const auto kept = std::count_if(r.begin(), r.end(), [&](int t) { return left.count(t) > 0; });
                        broken += (kept > 0 && kept < static_cast<long long>(r.size())) ? 1 : 0;
                    }
                    auto keptShare = [&](const std::set<int>& from)
                    {
                        if (from.empty()) return 1.0;
                        return static_cast<double>(std::count_if(from.begin(), from.end(), [&](int t) { return left.count(t) > 0; })) / from.size();
                    };
                    // Bar patterns on the 8th grid: share of bars that differ from the most common one.
                    std::map<int, int> barMasks;
                    std::vector<int> masks(static_cast<size_t>(bars), 0);
                    for (const auto& n : out)
                        if (!isRoll(n) && n.gridTick % (2 * TimingGrid::Sixteenth) == 0)
                            masks[static_cast<size_t>(n.gridTick / TimingGrid::TicksPerBar4_4) % bars] |= 1 << ((n.gridTick % TimingGrid::TicksPerBar4_4) / (2 * TimingGrid::Sixteenth));
                    int modal = 0;
                    for (const int m : masks) modal = std::max(modal, ++barMasks[m]);

                    auto& a = acc[s];
                    a["hats"].add(static_cast<double>(out.size()) / bars);
                    a["rolls"].add(static_cast<double>(rolls) / bars);
                    if (!baseRolls.empty()) a["broken"].add(static_cast<double>(broken) / baseRolls.size());
                    a["grid"].add(keptShare(baseGrid));
                    a["quarter"].add(keptShare(baseQuarter));
                    a["uneven"].add(1.0 - static_cast<double>(modal) / bars);
                    a["offgrid"].add(out.empty() ? 0.0 : static_cast<double>(offGrid) / out.size());
                }
            }
            for (const float s : strengths)
            {
                auto& a = acc[s];
                std::cout << g.key << "," << g.subs[sub] << "," << juce::roundToInt(s * 100.0f) << ","
                          << juce::String(a["hats"].mean(), 2) << "," << juce::String(a["rolls"].mean(), 2) << ","
                          << juce::String(a["broken"].mean(), 3) << "," << juce::String(a["grid"].mean(), 3) << ","
                          << juce::String(a["quarter"].mean(), 3) << "," << juce::String(a["uneven"].mean(), 3) << ","
                          << juce::String(a["offgrid"].mean(), 3) << "\n";
            }
        }
    }
    return 0;
}
