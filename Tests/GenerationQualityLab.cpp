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
#include "QualityMetrics.h"

using namespace bbg;
using namespace bbg::quality;

namespace
{

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
                "secondaryTimingMeanAbs,timingOutlierRate,bassNotesPerBar,bassOnKickRate,eventsPerBar,syncopation,hardFailures,failure,skeletonHash,kickHash,snareHash,hatHash,otherHash,firstTwoHash,firstTwoKickHash,kickBars,hatBars,kickAllBars,hatAllBars,hatAllPerBar\n";

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
                                 << m.bassOnKickRate << ',' << m.eventsPerBar << ',' << m.syncopation << ',' << m.hardFailures << ',' << m.failure << ',' << m.skeleton.hashCode64() << ','
                                 << m.kickSkeleton.hashCode64() << ',' << m.snareSkeleton.hashCode64() << ','
                                 << m.hatSkeleton.hashCode64() << ',' << m.otherSkeleton.hashCode64() << ','
                                 << m.firstTwoBars.hashCode64() << ',' << m.firstTwoKick.hashCode64() << ',' << m.kickBarsText << ',' << m.hatBarsText << ',' << m.kickAllBarsText << ',' << m.hatAllBarsText << ',' << m.hatAllNotesPerBar << '\n';
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
