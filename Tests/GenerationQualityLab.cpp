// HPDG Generation Quality Lab (docs/ROADMAP_UPDATE.md, Phase 0 / 1).
//
// Drives the real product path (plugin processor: style preset -> tempo -> genre engine ->
// post-processing) for every production genre / substyle and measures the SAME metrics on the
// resulting pattern for all of them, so a change can be compared against a stored baseline.
//
//   HPDG_GenerationQualityLab [--seeds N] [--genre boombap|trap|dnb|techno|all]
//                             [--density default|matrix] [--bars 2|4|8|matrix]
//                             [--out <folder>] [--tag <name>]
//
// Writes <out>/<tag>_patterns.csv (one row per generated pattern) and <out>/<tag>_summary.json
// (one entry per configuration: means / percentiles / rates). Generation does not change.

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

#include <juce_events/juce_events.h>

#include "../Source/Core/ProjectLaneAccess.h"
#include "../Source/Engine/StyleDefaults.h"
#include "../Source/Plugin/PluginProcessor.h"

using namespace bbg;

namespace
{
constexpr int kStep = 240;     // 1/16
constexpr int kBar = 3840;

struct GenreInfo
{
    const char* key;
    GenreType genre;
    int choice;                // genre parameter choice
    const char* substyleParam;
    juce::StringArray substyles;
};

std::vector<GenreInfo> genres()
{
    return {
        { "boombap", GenreType::BoomBap, 0, ParamIds::boombapSubstyle, getBoomBapSubstyleNames() },
        { "trap", GenreType::Trap, 2, ParamIds::trapSubstyle, getTrapSubstyleNames() },
        { "dnb", GenreType::DnB, 4, ParamIds::dnbSubstyle, getDnBSubstyleNames() },
        { "techno", GenreType::Techno, 5, ParamIds::technoSubstyle, getTechnoSubstyleNames() },
    };
}

// The genre's backbone snare positions (16th steps in a bar).
std::vector<int> backbeatSteps(GenreType genre)
{
    if (genre == GenreType::Trap)
        return { 8 };
    return { 4, 12 };
}

const std::vector<TrackType>& drumLanes()
{
    static const std::vector<TrackType> lanes { TrackType::Kick, TrackType::GhostKick, TrackType::Snare, TrackType::ClapGhostSnare,
                                                TrackType::HiHat, TrackType::HatFX, TrackType::OpenHat, TrackType::Ride,
                                                TrackType::Cymbal, TrackType::Perc };
    return lanes;
}

float metricWeight(int step)
{
    step = ((step % 16) + 16) % 16;
    if (step == 0) return 1.0f;
    if (step == 8) return 0.75f;
    if (step % 4 == 0) return 0.6f;
    if (step % 2 == 0) return 0.35f;
    return 0.15f;
}

struct Metrics
{
    // kick
    float kicksPerBar = 0, kickDownbeatRate = 0, kickStrongBeatRatio = 0, kickSyncopation = 0, kickConsecutiveRate = 0;
    float kickSnareCollisionsPerBar = 0, kickUniqueBarRatio = 0;
    // snare
    float backbeatCoverage = 0, snareGhostRate = 0, extraSnaresPerBar = 0, ghostLouderThanAnchor = 0, snareInvalidPerBar = 0;
    // hats
    float hatsPerBar = 0, hatEighthCoverage = 0, hatMaxGapSteps = 0, hatRollNotesPerBar = 0, openClosedCollisions = 0, openHatsPerBar = 0;
    // phrase
    float barSimilarity = 0, lastBarDifference = 0, identicalBarRate = 0;
    // dynamics
    float velocityStd = 0, identicalVelocityRate = 0;
    // timing
    float anchorTimingMeanAbs = 0, secondaryTimingMeanAbs = 0, timingOutlierRate = 0;
    // bass
    float bassNotesPerBar = 0, bassOnKickRate = 0;
    // overall
    float eventsPerBar = 0, syncopation = 0, bpm = 0;
    int hardFailures = 0;
    juce::String failure;
    double ms = 0;
    juce::String skeleton;      // drums, 32nd resolution, no velocity / offset
    juce::String kickSkeleton;
    std::set<std::pair<int, int>> skeletonSet;
};

std::vector<NoteEvent> notesOf(const PatternProject& project, TrackType lane)
{
    const auto* state = ProjectLaneAccess::findTrackState(project, lane);
    return state != nullptr && state->enabled && !state->muted ? state->notes : std::vector<NoteEvent> {};
}

// The 16th a note belongs to (a 32nd at x.5 stays in step x: rounding moved the last 32nd of
// a fill onto the next bar's downbeat and reported "snare on 1").
int stepOf(const NoteEvent& n) { return n.gridTick >= 0 ? n.gridTick / kStep : 0; }
bool onStep(const NoteEvent& n) { return n.gridTick % kStep == 0; }

Metrics measure(const PatternProject& project, GenreType genre)
{
    Metrics m;
    const int bars = std::max(1, project.params.bars);
    const float fb = static_cast<float>(bars);
    m.bpm = project.params.bpm;

    const auto kick = notesOf(project, TrackType::Kick);
    const auto snare = notesOf(project, TrackType::Snare);
    const auto hats = notesOf(project, TrackType::HiHat);
    const auto open = notesOf(project, TrackType::OpenHat);
    const auto bass = notesOf(project, TrackType::Sub808);

    // --- kick
    std::vector<std::set<int>> kickBars(static_cast<size_t>(bars));
    for (const auto& n : kick)
        if (!n.isGhost && stepOf(n) / 16 < bars)
            kickBars[static_cast<size_t>(stepOf(n) / 16)].insert(stepOf(n) % 16);
    int kicks = 0, downbeats = 0, strong = 0, consecutive = 0;
    for (const auto& bar : kickBars)
    {
        kicks += static_cast<int>(bar.size());
        downbeats += bar.count(0) > 0 ? 1 : 0;
        for (const int s : bar)
        {
            strong += s % 4 == 0 ? 1 : 0;
            consecutive += bar.count(s + 1) > 0 ? 1 : 0;
        }
    }
    m.kicksPerBar = kicks / fb;
    m.kickDownbeatRate = downbeats / fb;
    m.kickStrongBeatRatio = kicks > 0 ? strong / static_cast<float>(kicks) : 0.0f;
    m.kickConsecutiveRate = kicks > 0 ? consecutive / static_cast<float>(kicks) : 0.0f;
    std::set<std::set<int>> uniqueKickBars(kickBars.begin(), kickBars.end());
    m.kickUniqueBarRatio = uniqueKickBars.size() / fb;

    // LHL syncopation of one lane set per bar.
    auto syncopationOf = [&](const std::vector<std::set<int>>& lanes)
    {
        float total = 0.0f;
        for (const auto& on : lanes)
            for (const int n : on)
            {
                int r = n + 1;
                while (r < 16 && metricWeight(r) <= metricWeight(n))
                    ++r;
                const bool silent = r >= 16 ? false : on.count(r) == 0;
                if (silent)
                    total += metricWeight(r) - metricWeight(n);
            }
        return total / fb;
    };
    m.kickSyncopation = syncopationOf(kickBars);

    // --- snare
    std::vector<std::set<int>> snareBars(static_cast<size_t>(bars));
    int ghosts = 0, anchors = 0, ghostLoud = 0, invalid = 0;
    const auto backbeat = backbeatSteps(genre);
    for (const auto& n : snare)
    {
        const int s = stepOf(n);
        if (s / 16 >= bars)
            continue;
        if (n.isGhost)
        {
            ++ghosts;
            // the anchor of the same bar
            int anchorVelocity = 0;
            for (const auto& a : snare)
                if (!a.isGhost && stepOf(a) / 16 == s / 16)
                    anchorVelocity = std::max(anchorVelocity, a.velocity);
            ghostLoud += anchorVelocity > 0 && n.velocity >= anchorVelocity ? 1 : 0;
            continue;
        }
        ++anchors;
        snareBars[static_cast<size_t>(s / 16)].insert(s % 16);
        if (genre != GenreType::Trap && onStep(n) && (s % 16 == 0 || s % 16 == 8))
            ++invalid;
    }
    int covered = 0;
    for (const auto& bar : snareBars)
        for (const int b : backbeat)
            covered += bar.count(b) > 0 ? 1 : 0;
    m.backbeatCoverage = covered / (fb * static_cast<float>(backbeat.size()));
    m.snareGhostRate = (ghosts + anchors) > 0 ? ghosts / static_cast<float>(ghosts + anchors) : 0.0f;
    m.extraSnaresPerBar = std::max(0.0f, (anchors - covered) / fb);
    m.ghostLouderThanAnchor = static_cast<float>(ghostLoud);
    m.snareInvalidPerBar = invalid / fb;
    for (int bar = 0; bar < bars; ++bar)
        for (const int s : kickBars[static_cast<size_t>(bar)])
            m.kickSnareCollisionsPerBar += snareBars[static_cast<size_t>(bar)].count(s) > 0 ? 1.0f : 0.0f;
    m.kickSnareCollisionsPerBar /= fb;

    // --- hats
    std::vector<std::set<int>> hatBars(static_cast<size_t>(bars));
    int rollNotes = 0;
    for (const auto& n : hats)
    {
        if (n.gridTick % kStep != 0)
            ++rollNotes;
        if (stepOf(n) / 16 < bars)
            hatBars[static_cast<size_t>(stepOf(n) / 16)].insert(stepOf(n) % 16);
    }
    rollNotes += static_cast<int>(notesOf(project, TrackType::HatFX).size());
    std::set<int> openSteps;
    for (const auto& n : open)
        openSteps.insert(stepOf(n));
    m.hatsPerBar = hats.size() / fb;
    m.openHatsPerBar = open.size() / fb;
    m.hatRollNotesPerBar = rollNotes / fb;
    int eighths = 0, maxGap = 0;
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto& on = hatBars[static_cast<size_t>(bar)];
        for (int s = 0; s < 16; s += 2)
            eighths += (on.count(s) > 0 || on.count(s + 1) > 0 || openSteps.count(bar * 16 + s) > 0) ? 1 : 0;
    }
    std::vector<int> allHat;
    for (const auto& n : hats)
        allHat.push_back(stepOf(n));
    for (const auto& n : open)
        allHat.push_back(stepOf(n));
    std::sort(allHat.begin(), allHat.end());
    allHat.erase(std::unique(allHat.begin(), allHat.end()), allHat.end());
    for (size_t i = 0; i < allHat.size(); ++i)
    {
        const int next = i + 1 < allHat.size() ? allHat[i + 1] : allHat.front() + bars * 16;
        maxGap = std::max(maxGap, next - allHat[i]);
    }
    if (allHat.empty())
        maxGap = bars * 16;
    m.hatEighthCoverage = eighths / (fb * 8.0f);
    m.hatMaxGapSteps = static_cast<float>(maxGap);
    for (const auto& n : hats)
        m.openClosedCollisions += openSteps.count(stepOf(n)) > 0 ? 1.0f : 0.0f;

    // --- whole drum skeleton: phrase similarity, duplicates
    std::vector<std::set<std::pair<int, int>>> barSets(static_cast<size_t>(bars));
    int events = 0;
    std::vector<int> velocities;
    int identicalVelocityPairs = 0, velocityPairs = 0;
    double anchorAbs = 0, secondaryAbs = 0;
    int anchorCount = 0, secondaryCount = 0, outliers = 0;
    std::vector<std::set<int>> allLaneBars(static_cast<size_t>(bars));
    for (const auto lane : drumLanes())
    {
        const auto notes = notesOf(project, lane);
        int previousVelocity = -1;
        for (const auto& n : notes)
        {
            const int s32 = static_cast<int>(std::lround(n.gridTick / 120.0));
            const int bar = s32 / 32;
            if (bar >= bars)
                continue;
            ++events;
            barSets[static_cast<size_t>(bar)].insert({ static_cast<int>(lane), s32 % 32 });
            m.skeletonSet.insert({ static_cast<int>(lane), s32 });
            velocities.push_back(n.velocity);
            if (previousVelocity >= 0)
            {
                ++velocityPairs;
                identicalVelocityPairs += previousVelocity == n.velocity ? 1 : 0;
            }
            previousVelocity = n.velocity;
            const bool anchor = (lane == TrackType::Kick || lane == TrackType::Snare) && !n.isGhost;
            const int offset = std::abs(n.timingOffsetTicks);
            (anchor ? anchorAbs : secondaryAbs) += offset;
            (anchor ? anchorCount : secondaryCount) += 1;
            outliers += offset > 60 ? 1 : 0;
            if (lane != TrackType::Cymbal && lane != TrackType::HatFX)
                allLaneBars[static_cast<size_t>(bar)].insert(stepOf(n) % 16 + 16 * static_cast<int>(lane));
        }
    }
    m.eventsPerBar = events / fb;
    float jaccardSum = 0.0f;
    int identical = 0;
    for (int bar = 1; bar < bars; ++bar)
    {
        const auto& a = barSets[static_cast<size_t>(bar - 1)];
        const auto& b = barSets[static_cast<size_t>(bar)];
        int inter = 0;
        for (const auto& x : a)
            inter += b.count(x) > 0 ? 1 : 0;
        const int uni = static_cast<int>(a.size() + b.size()) - inter;
        jaccardSum += uni > 0 ? inter / static_cast<float>(uni) : 1.0f;
        identical += a == b ? 1 : 0;
    }
    m.barSimilarity = bars > 1 ? jaccardSum / (fb - 1.0f) : 1.0f;
    m.identicalBarRate = bars > 1 ? identical / (fb - 1.0f) : 1.0f;
    if (bars > 1)
    {
        const auto& a = barSets.front();
        const auto& b = barSets.back();
        int inter = 0;
        for (const auto& x : a)
            inter += b.count(x) > 0 ? 1 : 0;
        const int uni = static_cast<int>(a.size() + b.size()) - inter;
        m.lastBarDifference = uni > 0 ? 1.0f - inter / static_cast<float>(uni) : 0.0f;
    }
    if (!velocities.empty())
    {
        double mean = 0;
        for (const int v : velocities)
            mean += v;
        mean /= velocities.size();
        double var = 0;
        for (const int v : velocities)
            var += (v - mean) * (v - mean);
        m.velocityStd = static_cast<float>(std::sqrt(var / velocities.size()));
    }
    m.identicalVelocityRate = velocityPairs > 0 ? identicalVelocityPairs / static_cast<float>(velocityPairs) : 0.0f;
    m.anchorTimingMeanAbs = anchorCount > 0 ? static_cast<float>(anchorAbs / anchorCount) : 0.0f;
    m.secondaryTimingMeanAbs = secondaryCount > 0 ? static_cast<float>(secondaryAbs / secondaryCount) : 0.0f;
    m.timingOutlierRate = events > 0 ? outliers / static_cast<float>(events) : 0.0f;
    m.syncopation = syncopationOf(allLaneBars.empty() ? kickBars : kickBars) + syncopationOf(snareBars) + 0.5f * syncopationOf(hatBars);

    // --- bass
    std::set<int> kickSteps;
    for (const auto& n : kick)
        kickSteps.insert(stepOf(n));
    int onKick = 0;
    for (const auto& n : bass)
        onKick += kickSteps.count(stepOf(n)) > 0 ? 1 : 0;
    m.bassNotesPerBar = bass.size() / fb;
    m.bassOnKickRate = bass.empty() ? 0.0f : onKick / static_cast<float>(bass.size());

    // --- skeleton strings (duplicate detection)
    for (const auto& [lane, s32] : m.skeletonSet)
        m.skeleton << lane << ":" << s32 << " ";
    for (const auto& bar : kickBars)
    {
        for (const int s : bar)
            m.kickSkeleton << s << ",";
        m.kickSkeleton << "|";
    }

    // --- critical structural failures (baseline counts, not gates)
    juce::StringArray fails;
    if (genre == GenreType::BoomBap || genre == GenreType::DnB)
    {
        if (m.backbeatCoverage < 0.999f) fails.add("backbeat missing");
        if (m.kicksPerBar > (genre == GenreType::DnB ? 5.0f : 6.0f)) fails.add("kick spam");
        if (m.ghostLouderThanAnchor > 0) fails.add("ghost louder than anchor");
        if (m.snareInvalidPerBar > 0) fails.add("snare on 1/3");
    }
    else if (genre == GenreType::Trap)
    {
        if (m.backbeatCoverage < 0.999f) fails.add("snare on 3 missing");
        int kickOnSnare = 0;
        for (const auto& bar : kickBars)
            kickOnSnare += bar.count(8) > 0 ? 1 : 0;
        if (kickOnSnare > bars / 2) fails.add("kick on protected snare");
        if (hats.empty()) fails.add("no hat carrier");
    }
    else if (genre == GenreType::Techno)
    {
        int missing = 0;
        for (int bar = 0; bar + 1 < bars || (bars == 1 && bar == 0); ++bar)
            for (int beat = 0; beat < 4; ++beat)
                missing += kickBars[static_cast<size_t>(bar)].count(beat * 4) == 0 ? 1 : 0;
        if (missing > 0) fails.add("kick axis broken");
        int percOnKick = 0;
        for (const auto& n : notesOf(project, TrackType::Perc))
            percOnKick += kickSteps.count(stepOf(n)) > 0 ? 1 : 0;
        if (percOnKick > 0) fails.add("perc on kick");
    }
    if (m.bassOnKickRate > 0.0f && genre == GenreType::Techno)
        fails.add("bass on kick");
    m.hardFailures = fails.size();
    m.failure = fails.joinIntoString(";");
    return m;
}

struct Stat
{
    std::vector<double> values;
    void add(double v) { values.push_back(v); }
    double mean() const
    {
        double s = 0;
        for (const double v : values)
            s += v;
        return values.empty() ? 0.0 : s / values.size();
    }
    double pct(double q) const
    {
        if (values.empty())
            return 0.0;
        auto v = values;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, static_cast<size_t>(std::lround(q * (v.size() - 1))))];
    }
};

void setChoice(juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}

int barsChoice(int bars)
{
    switch (bars)
    {
        case 1: return 0;
        case 2: return 1;
        case 8: return 3;
        case 16: return 4;
        default: return 2;
    }
}

juce::String jsonNumber(double v)
{
    return std::isfinite(v) ? juce::String(v, 4) : juce::String("null");
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    int seeds = 1000;
    juce::String genreFilter = "all";
    juce::String densityMode = "default";
    juce::String barsMode = "4";
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile("quality-lab");
    juce::String tag = "run";
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a(argv[i]);
        auto next = [&] { return i + 1 < argc ? juce::String(argv[++i]) : juce::String(); };
        if (a == "--seeds") seeds = std::max(1, next().getIntValue());
        else if (a == "--genre") genreFilter = next().toLowerCase();
        else if (a == "--density") densityMode = next();
        else if (a == "--bars") barsMode = next();
        else if (a == "--out") out = juce::File(next());
        else if (a == "--tag") tag = next();
    }
    out.createDirectory();

    std::vector<float> densities = densityMode == "matrix" ? std::vector<float> { 0.2f, -1.0f, 0.8f } : std::vector<float> { -1.0f };
    std::vector<int> barCounts = barsMode == "matrix" ? std::vector<int> { 2, 4, 8 } : std::vector<int> { barsMode.getIntValue() > 0 ? barsMode.getIntValue() : 4 };

    std::ofstream patterns(out.getChildFile(tag + "_patterns.csv").getFullPathName().toStdString());
    patterns << "genre,substyle,density,bars,seed,bpm,ms,kicksPerBar,kickDownbeatRate,kickStrongBeatRatio,kickSyncopation,kickConsecutiveRate,"
                "kickSnareCollisionsPerBar,kickUniqueBarRatio,backbeatCoverage,snareGhostRate,extraSnaresPerBar,ghostLouderThanAnchor,"
                "snareInvalidPerBar,hatsPerBar,openHatsPerBar,hatEighthCoverage,hatMaxGapSteps,hatRollNotesPerBar,openClosedCollisions,"
                "barSimilarity,identicalBarRate,lastBarDifference,velocityStd,identicalVelocityRate,anchorTimingMeanAbs,"
                "secondaryTimingMeanAbs,timingOutlierRate,bassNotesPerBar,bassOnKickRate,eventsPerBar,syncopation,hardFailures,failure\n";

    juce::String json = "{\n  \"tag\": \"" + tag + "\",\n  \"seeds\": " + juce::String(seeds) + ",\n  \"configurations\": [\n";
    bool firstConfig = true;

    for (const auto& g : genres())
    {
        if (genreFilter != "all" && genreFilter != g.key)
            continue;
        for (int sub = 0; sub < g.substyles.size(); ++sub)
            for (const float density : densities)
                for (const int bars : barCounts)
                {
                    // A fresh processor per configuration: identical starting state for every run.
                    BoomBapGeneratorAudioProcessor processor;
                    processor.prepareToPlay(44100.0, 512);
                    auto& apvts = processor.getApvts();
                    setChoice(apvts, ParamIds::genre, static_cast<float>(g.choice));
                    setChoice(apvts, g.substyleParam, static_cast<float>(sub));
                    processor.applySelectedStylePreset(false);
                    setChoice(apvts, ParamIds::bars, static_cast<float>(barsChoice(bars)));
                    processor.syncBarsFromState();
                    if (density >= 0.0f)
                        setChoice(apvts, ParamIds::densityAmount, density);
                    setChoice(apvts, ParamIds::seedLock, 1.0f);
                    const float effectiveDensity = apvts.getRawParameterValue(ParamIds::densityAmount)->load();

                    std::map<juce::String, Stat> stats;
                    std::set<juce::String> seenSkeletons, seenKicks;
                    std::vector<std::set<std::pair<int, int>>> recent;
                    int exactDuplicates = 0, nearDuplicates = 0, kickDuplicates = 0, failures = 0;
                    std::map<juce::String, int> failureKinds;
                    Stat timings;

                    for (int seed = 1; seed <= seeds; ++seed)
                    {
                        setChoice(apvts, ParamIds::seed, static_cast<float>(seed));
                        const auto start = std::chrono::steady_clock::now();
                        processor.generatePattern();
                        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                        const auto project = processor.getProjectSnapshot();
                        auto m = measure(project, g.genre);
                        m.ms = ms;
                        timings.add(ms);

                        exactDuplicates += seenSkeletons.insert(m.skeleton).second ? 0 : 1;
                        kickDuplicates += seenKicks.insert(m.kickSkeleton).second ? 0 : 1;
                        bool near = false;
                        for (const auto& previous : recent)
                        {
                            int inter = 0;
                            for (const auto& x : m.skeletonSet)
                                inter += previous.count(x) > 0 ? 1 : 0;
                            const int uni = static_cast<int>(previous.size() + m.skeletonSet.size()) - inter;
                            if (uni > 0 && inter / static_cast<double>(uni) >= 0.9)
                            {
                                near = true;
                                break;
                            }
                        }
                        nearDuplicates += near ? 1 : 0;
                        recent.push_back(m.skeletonSet);
                        if (recent.size() > 200)
                            recent.erase(recent.begin());
                        // The first failing pattern of a configuration is printed (to judge whether
                        // the flag is a real musical failure or a too-strict metric).
                        if (m.hardFailures > 0 && failures == 0)
                        {
                            std::cout << "  first failure: seed " << seed << " [" << m.failure << "] snare:";
                            for (const auto& n : notesOf(project, TrackType::Snare))
                                std::cout << " " << n.gridTick / 240.0 << (n.isGhost ? "g" : "") << "/" << n.velocity << "/" << n.semanticRole;
                            std::cout << std::endl;
                        }
                        failures += m.hardFailures > 0 ? 1 : 0;
                        for (const auto& kind : juce::StringArray::fromTokens(m.failure, ";", {}))
                            if (kind.isNotEmpty())
                                ++failureKinds[kind];

                        const std::pair<const char*, float> values[] {
                            { "bpm", m.bpm }, { "kicksPerBar", m.kicksPerBar }, { "kickDownbeatRate", m.kickDownbeatRate },
                            { "kickStrongBeatRatio", m.kickStrongBeatRatio }, { "kickSyncopation", m.kickSyncopation },
                            { "kickConsecutiveRate", m.kickConsecutiveRate }, { "kickSnareCollisionsPerBar", m.kickSnareCollisionsPerBar },
                            { "kickUniqueBarRatio", m.kickUniqueBarRatio }, { "backbeatCoverage", m.backbeatCoverage },
                            { "snareGhostRate", m.snareGhostRate }, { "extraSnaresPerBar", m.extraSnaresPerBar },
                            { "hatsPerBar", m.hatsPerBar }, { "openHatsPerBar", m.openHatsPerBar }, { "hatEighthCoverage", m.hatEighthCoverage },
                            { "hatMaxGapSteps", m.hatMaxGapSteps }, { "hatRollNotesPerBar", m.hatRollNotesPerBar },
                            { "openClosedCollisions", m.openClosedCollisions }, { "barSimilarity", m.barSimilarity },
                            { "identicalBarRate", m.identicalBarRate }, { "lastBarDifference", m.lastBarDifference },
                            { "velocityStd", m.velocityStd }, { "identicalVelocityRate", m.identicalVelocityRate },
                            { "anchorTimingMeanAbs", m.anchorTimingMeanAbs }, { "secondaryTimingMeanAbs", m.secondaryTimingMeanAbs },
                            { "timingOutlierRate", m.timingOutlierRate }, { "bassNotesPerBar", m.bassNotesPerBar },
                            { "bassOnKickRate", m.bassOnKickRate }, { "eventsPerBar", m.eventsPerBar }, { "syncopation", m.syncopation },
                        };
                        for (const auto& [name, value] : values)
                            stats[name].add(value);

                        patterns << g.key << ',' << g.substyles[sub].replace(",", " ") << ',' << effectiveDensity << ',' << bars << ',' << seed << ','
                                 << m.bpm << ',' << ms << ',' << m.kicksPerBar << ',' << m.kickDownbeatRate << ',' << m.kickStrongBeatRatio << ','
                                 << m.kickSyncopation << ',' << m.kickConsecutiveRate << ',' << m.kickSnareCollisionsPerBar << ',' << m.kickUniqueBarRatio << ','
                                 << m.backbeatCoverage << ',' << m.snareGhostRate << ',' << m.extraSnaresPerBar << ',' << m.ghostLouderThanAnchor << ','
                                 << m.snareInvalidPerBar << ',' << m.hatsPerBar << ',' << m.openHatsPerBar << ',' << m.hatEighthCoverage << ','
                                 << m.hatMaxGapSteps << ',' << m.hatRollNotesPerBar << ',' << m.openClosedCollisions << ',' << m.barSimilarity << ','
                                 << m.identicalBarRate << ',' << m.lastBarDifference << ',' << m.velocityStd << ',' << m.identicalVelocityRate << ','
                                 << m.anchorTimingMeanAbs << ',' << m.secondaryTimingMeanAbs << ',' << m.timingOutlierRate << ',' << m.bassNotesPerBar << ','
                                 << m.bassOnKickRate << ',' << m.eventsPerBar << ',' << m.syncopation << ',' << m.hardFailures << ',' << m.failure << '\n';
                    }

                    // Determinism: a fresh processor replaying seeds 1..10 must give the same patterns.
                    int determinismFailures = 0;
                    {
                        BoomBapGeneratorAudioProcessor a, b;
                        for (auto* p : { &a, &b })
                        {
                            p->prepareToPlay(44100.0, 512);
                            auto& s = p->getApvts();
                            setChoice(s, ParamIds::genre, static_cast<float>(g.choice));
                            setChoice(s, g.substyleParam, static_cast<float>(sub));
                            p->applySelectedStylePreset(false);
                            setChoice(s, ParamIds::bars, static_cast<float>(barsChoice(bars)));
                            p->syncBarsFromState();
                            if (density >= 0.0f)
                                setChoice(s, ParamIds::densityAmount, density);
                            setChoice(s, ParamIds::seedLock, 1.0f);
                        }
                        for (int seed : { 1, 2, 3, 10, 25, 42, 100, 256, 512, 999 })
                        {
                            for (auto* p : { &a, &b })
                            {
                                setChoice(p->getApvts(), ParamIds::seed, static_cast<float>(seed));
                                p->generatePattern();
                            }
                            determinismFailures += measure(a.getProjectSnapshot(), g.genre).skeleton != measure(b.getProjectSnapshot(), g.genre).skeleton ? 1 : 0;
                        }
                    }

                    const juce::String name = juce::String(g.key) + " / " + g.substyles[sub] + " / density " + juce::String(effectiveDensity, 2)
                        + " / " + juce::String(bars) + " bars";
                    std::cout << name << ": failures " << failures << "/" << seeds << " | exact dup " << exactDuplicates << " | near dup "
                              << nearDuplicates << " | kick dup " << kickDuplicates << " | determinism " << determinismFailures
                              << " | kicks/bar " << juce::String(stats["kicksPerBar"].mean(), 2) << " | backbeat " << juce::String(stats["backbeatCoverage"].mean(), 3)
                              << " | events/bar " << juce::String(stats["eventsPerBar"].mean(), 1) << " | p50 " << juce::String(timings.pct(0.5), 1)
                              << " ms p95 " << juce::String(timings.pct(0.95), 1) << " ms" << std::endl;

                    json << (firstConfig ? "" : ",\n") << "    {\n      \"genre\": \"" << g.key << "\", \"substyle\": \"" << g.substyles[sub]
                         << "\", \"density\": " << jsonNumber(effectiveDensity) << ", \"bars\": " << bars << ",\n"
                         << "      \"hardFailureRate\": " << jsonNumber(failures / static_cast<double>(seeds))
                         << ", \"exactDuplicateRate\": " << jsonNumber(exactDuplicates / static_cast<double>(seeds))
                         << ", \"nearDuplicateRate\": " << jsonNumber(nearDuplicates / static_cast<double>(seeds))
                         << ", \"kickSkeletonDuplicateRate\": " << jsonNumber(kickDuplicates / static_cast<double>(seeds))
                         << ", \"determinismFailures\": " << determinismFailures << ",\n"
                         << "      \"timeMs\": { \"p50\": " << jsonNumber(timings.pct(0.5)) << ", \"p95\": " << jsonNumber(timings.pct(0.95))
                         << ", \"max\": " << jsonNumber(timings.pct(1.0)) << " },\n      \"failureKinds\": {";
                    bool firstKind = true;
                    for (const auto& [kind, count] : failureKinds)
                    {
                        json << (firstKind ? " " : ", ") << "\"" << kind << "\": " << count;
                        firstKind = false;
                    }
                    json << " },\n      \"metrics\": {\n";
                    bool firstMetric = true;
                    for (const auto& [metric, stat] : stats)
                    {
                        json << (firstMetric ? "" : ",\n") << "        \"" << metric << "\": { \"mean\": " << jsonNumber(stat.mean())
                             << ", \"p05\": " << jsonNumber(stat.pct(0.05)) << ", \"p50\": " << jsonNumber(stat.pct(0.5))
                             << ", \"p95\": " << jsonNumber(stat.pct(0.95)) << " }";
                        firstMetric = false;
                    }
                    json << "\n      }\n    }";
                    firstConfig = false;
                }
    }
    json << "\n  ]\n}\n";
    out.getChildFile(tag + "_summary.json").replaceWithText(json);
    std::cout << "wrote " << out.getChildFile(tag + "_summary.json").getFullPathName() << std::endl;
    return 0;
}
