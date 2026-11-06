// HPDG Generation Quality Lab (docs/ROADMAP_UPDATE.md, Phase 0 / 1).
//
// Drives the real product path (plugin processor: style preset -> tempo -> genre engine ->
// post-processing) for every production genre / substyle and measures the SAME metrics on the
// resulting pattern for all of them, so a change can be compared against a stored baseline.
//
//   HPDG_GenerationQualityLab [--seeds N] [--genre boombap|trap|dnb|techno|atmospheric|all]
//                             [--density default|matrix] [--bars 2|4|8|matrix]
//                             [--out <folder>] [--tag <name>] [--notes] [--key 0-11] [--perf s h] [--render N]
//                             [--substyle DarkTrap] [--bpm 150] [--rolls 0|1|2]
//
// --notes also writes <out>/<tag>_notes.csv: low-end, snare and hat lanes of every pattern (grid tick,
// timing offset, pitch, length, glide) for kick / bass alignment and register checks.
// --perf <swing delta> <humanize delta>: the notes are dumped after the live performance layer
// (PatternPerformanceTransformEngine, as when the Swing / Humanize knobs move after Generate).
// --render N: the first N seeds of every configuration are rendered as the plugin renders a lane for
// drag-and-drop (Kick, Sub808, full mix); the file paths go to <out>/<tag>_renders.csv.
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
#include "../Source/Engine/PatternPerformanceTransformEngine.h"
#include "../Source/Engine/StyleDefaults.h"
#include "../Source/Plugin/PluginProcessor.h"
#include "../Source/Bass808/BassSample.h"
#include "../Source/Bass808/BassEngine.h"
#include "../Source/Bass808/BassSettingsIO.h"
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
        { "atmospheric", GenreType::Atmospheric, 6, ParamIds::atmosphericSubstyle, getAtmosphericSubstyleNames() },
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
    if (argc > 1 && juce::String(argv[1]) == "--luxury-sound-check")
    {
        BoomBapGeneratorAudioProcessor p;
        p.prepareToPlay(44100.0, 512);
        setChoice(p.getApvts(), ParamIds::genre, 2.0f);
        setChoice(p.getApvts(), ParamIds::trapSubstyle, 5.0f);
        p.applySelectedStylePreset(false);
        if (!p.getSub808Sound().custom || p.getSub808Sound().name != "Luxury Spinz")
            return 1;
        const auto bassName = [&p] { const auto s = p.getSub808Sound();
            return s.custom ? s.name : p.getSelectedLaneSampleName(TrackType::Sub808); };
        std::set<juce::String> cycle;
        auto last = bassName();
        for (int n = 0; n < 18; ++n)
        {
            p.generatePattern();
            const auto name = bassName();
            if (name == last || !cycle.insert(name).second)
                return 2;
            last = name;
        }
        p.setTrackLocked(TrackType::Sub808, true);
        p.generatePattern();
        if (bassName() != last)
            return 8;
        p.setTrackLocked(TrackType::Sub808, false);
        juce::AudioFormatManager audioFormats;
        audioFormats.registerBasicFormats();
        int audibleDrums = 0;
        for (const auto lane : { TrackType::Snare, TrackType::Kick })
        {
            const auto drumNames = p.getLaneSampleNames(lane);
            for (int i = 0; i < drumNames.size(); ++i)
                if (drumNames[i].startsWith("Luxury "))
                {
                    p.selectLaneSampleIndex(lane, i);
                    const auto wav = p.createTemporaryWavFile(lane);
                    std::unique_ptr<juce::AudioFormatReader> reader(audioFormats.createReaderFor(wav));
                    if (reader == nullptr || reader->lengthInSamples <= 0)
                        return 9;
                    juce::AudioBuffer<float> audio(2, static_cast<int>(reader->lengthInSamples));
                    reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);
                    const float peak = audio.getMagnitude(0, audio.getNumSamples());
                    std::cout << drumNames[i] << " rendered peak " << peak << '\n';
                    if (!std::isfinite(peak) || peak < .001f)
                        return 10;
                    ++audibleDrums;
                }
        }
        if (audibleDrums != 19)
            return 11;
        const auto names = p.getLaneSampleNames(TrackType::Sub808);
        int luxurySamples = 0;
        for (int i = 0; i < names.size(); ++i)
            if (names[i].startsWith("Luxury 808"))
            {
                ++luxurySamples;
                p.selectLaneSampleIndex(TrackType::Sub808, i);
                const auto sample = p.getSub808BankSample();
                const double hz = names[i].endsWith("C2") ? 65.406391325 : 32.703195663;
                if (sample == nullptr || std::abs(1200.0 * std::log2(sample->rootHz / hz)) > 15.0)
                    return 3;
            }
        if (luxurySamples != 16)
            return 4;
        // Simulate a session whose bank index predates a library expansion.
        p.selectLaneSampleIndex(TrackType::Sub808, names.size() - 1);
        const auto wanted = p.getSelectedLaneSampleName(TrackType::Sub808);
        juce::MemoryBlock state;
        p.getStateInformation(state);
        auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
        if (xml == nullptr)
            return 5;
        auto tree = juce::ValueTree::fromXml(*xml);
        bool edited = false;
        const auto edit = [&](auto&& self, juce::ValueTree node) -> void
        {
            if (node["selected_sample_name"].toString() == wanted)
            {
                node.setProperty("selected_sample_index", 0, nullptr);
                edited = true;
            }
            for (auto child : node) self(self, child);
        };
        edit(edit, tree);
        if (!edited)
            return 6;
        juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), state);
        p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        if (p.getSelectedLaneSampleName(TrackType::Sub808) != wanted)
            return 7;
        // The audition used by the embedded Play/Stop button must release its voice,
        // and an unchanged edited sound must not be deep-compared on every callback.
        p.stopPreview();
        auto sound = p.getSub808Sound();
        sound.custom = true; sound.name = "Luxury Spinz"; sound.sampleWav.reset();
        for (const auto& preset : bassPresets())
            if (juce::String(preset.name)=="Luxury Spinz") sound.settings=bassSettingsToTree(preset.settings);
        p.setSub808Sound(sound);
        p.auditionSub808Note(36,110,8*TimingGrid::TicksPerBeat);
        juce::AudioBuffer<float> preview(2,512); juce::MidiBuffer midi;
        float previewPeak = 0.0f;
        for (int i=0;i<8;++i) { preview.clear(); midi.clear(); p.processBlock(preview,midi); previewPeak=std::max(previewPeak,preview.getMagnitude(0,512)); }
        const auto comparisons = p.getSub808SoundComparisonCount();
        for (int i=0;i<64;++i) { preview.clear(); midi.clear(); p.processBlock(preview,midi); }
        if (p.getSub808SoundComparisonCount()!=comparisons || previewPeak<.001f) return 12;
        p.releaseSub808AuditionNote(36);
        for (int i=0;i<32;++i) { preview.clear(); midi.clear(); p.processBlock(preview,midi); }
        if (preview.getMagnitude(0,512)>.001f) return 13;
        std::cout << "PASS: embedded audition starts/stops; stable sound deep comparisons across 64 callbacks = 0\n";
        std::cout << "PASS: Luxury 18-sound rotation/lock; 19 audible snare/kicks; 16 physical C roots; old bank index restored by name\n";
        return 0;
    }
    int seeds = 1000;
    juce::String genreFilter = "all";
    juce::String substyleFilter;
    juce::String densityMode = "default";
    juce::String barsMode = "4";
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile("quality-lab");
    juce::String tag = "run";
    bool dumpNotes = false;
    float perfSwingDelta = 0.0f, perfHumanizeDelta = 0.0f;
    int renderSeeds = 0;
    int keyRoot = -1;
    int rollMode = 1;
    float fixedBpm = 0.0f;
    juce::File sampleFile;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a(argv[i]);
        auto next = [&] { return i + 1 < argc ? juce::String(argv[++i]) : juce::String(); };
        if (a == "--seeds") seeds = std::max(1, next().getIntValue());
        else if (a == "--genre") genreFilter = next().toLowerCase();
        else if (a == "--substyle") substyleFilter = next();
        else if (a == "--density") densityMode = next();
        else if (a == "--bars") barsMode = next();
        else if (a == "--out") out = juce::File(next());
        else if (a == "--tag") tag = next();
        else if (a == "--notes") dumpNotes = true;
        else if (a == "--render") renderSeeds = next().getIntValue();
        else if (a == "--key") keyRoot = next().getIntValue(); // 0 = C ... 11 = B (after the style preset)
        else if (a == "--rolls") rollMode = std::clamp(next().getIntValue(), 0, 2);
        else if (a == "--bpm") fixedBpm = std::clamp(next().getFloatValue(), 20.0f, 400.0f);
        else if (a == "--sample") sampleFile=juce::File(next());
        else if (a == "--perf")
        {
            perfSwingDelta = next().getFloatValue();
            perfHumanizeDelta = next().getFloatValue();
        }
    }
    out.createDirectory();
    const auto prepareSample=[&](BoomBapGeneratorAudioProcessor& p)
    {
        if (sampleFile==juce::File()) return true;
        p.setAnalysisMode(AnalysisMode::GenerateFromSample);
        juce::String error;
        if (!p.analyzeAudioFile(sampleFile,&error,false))
        { std::cerr<<"Sample analysis failed: "<<error<<std::endl; return false; }
        const auto h=p.getSampleHarmony();
        std::cout<<"Analyzed "<<sampleFile.getFileName()<<": key "<<h.keyName()<<" segments "<<h.bass.size()
            <<" drum-loop confidence "<<p.getDrumBreakAnalysis().drumLoopConfidence<<std::endl;
        return true;
    };

    std::vector<float> densities = densityMode == "matrix" ? std::vector<float> { 0.2f, -1.0f, 0.8f } : std::vector<float> { -1.0f };
    std::vector<int> barCounts = barsMode == "matrix" ? std::vector<int> { 2, 4, 8 } : std::vector<int> { barsMode.getIntValue() > 0 ? barsMode.getIntValue() : 4 };

    std::ofstream patterns(out.getChildFile(tag + "_patterns.csv").getFullPathName().toStdString());
    patterns << "genre,substyle,density,bars,seed,bpm,ms,kicksPerBar,kickDownbeatRate,kickStrongBeatRatio,kickSyncopation,kickConsecutiveRate,"
                "kickSnareCollisionsPerBar,kickUniqueBarRatio,backbeatCoverage,snareGhostRate,extraSnaresPerBar,ghostLouderThanAnchor,"
                "snareInvalidPerBar,hatsPerBar,openHatsPerBar,hatEighthCoverage,hatMaxGapSteps,hatRollNotesPerBar,openClosedCollisions,"
                "barSimilarity,identicalBarRate,lastBarDifference,velocityStd,identicalVelocityRate,anchorTimingMeanAbs,"
                "secondaryTimingMeanAbs,timingOutlierRate,bassNotesPerBar,bassOnKickRate,eventsPerBar,syncopation,hardFailures,failure,skeletonHash,kickHash,snareHash,hatHash,otherHash,firstTwoHash,firstTwoKickHash,kickBars,hatBars,kickAllBars,hatAllBars,hatAllPerBar,displacedBackbeatPerBar,snareBars,bassOnSnareRate,drumBars,topBars\n";

    std::ofstream notesCsv;
    if (dumpNotes)
    {
        notesCsv.open(out.getChildFile(tag + "_notes.csv").getFullPathName().toStdString());
        notesCsv << "genre,substyle,bars,seed,bpm,key,lane,gridTick,offset,pitch,length,velocity,glide,density,role\n";
    }

    std::ofstream rendersCsv;
    if (renderSeeds > 0)
    {
        rendersCsv.open(out.getChildFile(tag + "_renders.csv").getFullPathName().toStdString());
        rendersCsv << "genre,substyle,seed,bpm,key,lane,file\n";
    }

    juce::String json = "{\n  \"tag\": \"" + tag + "\",\n  \"seeds\": " + juce::String(seeds) + ",\n  \"configurations\": [\n";
    bool firstConfig = true;

    for (const auto& g : genres())
    {
        if (genreFilter != "all" && genreFilter != g.key)
            continue;
        for (int sub = 0; sub < g.substyles.size(); ++sub)
        {
            if (substyleFilter.isNotEmpty() && !g.substyles[sub].equalsIgnoreCase(substyleFilter))
                continue;
            for (const float density : densities)
                for (const int bars : barCounts)
                {
                    // A fresh processor per configuration: identical starting state for every run.
                    BoomBapGeneratorAudioProcessor processor;
                    processor.prepareToPlay(44100.0, 512);
                    auto& apvts = processor.getApvts();
                    setChoice(apvts, ParamIds::trapRollMode, static_cast<float>(rollMode));
                    setChoice(apvts, ParamIds::genre, static_cast<float>(g.choice));
                    setChoice(apvts, g.substyleParam, static_cast<float>(sub));
                    processor.applySelectedStylePreset(false);
                    if (fixedBpm > 0.0f)
                    {
                        setChoice(apvts, ParamIds::bpmLock, 1.0f);
                        setChoice(apvts, ParamIds::bpm, fixedBpm);
                    }
                    setChoice(apvts, ParamIds::bars, static_cast<float>(barsChoice(bars)));
                    processor.syncBarsFromState();
                    if (density >= 0.0f)
                        setChoice(apvts, ParamIds::densityAmount, density);
                    setChoice(apvts, ParamIds::seedLock, 1.0f);
                    if (keyRoot >= 0)
                        setChoice(apvts, ParamIds::keyRoot, static_cast<float>(keyRoot % 12));
                    if (!prepareSample(processor)) return 20;
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
                        int trueRollNotes = 0;
                        for (const auto& track : project.tracks)
                            if (track.type == TrackType::HiHat || track.type == TrackType::HatFX)
                                for (const auto& note : track.notes)
                                    trueRollNotes += note.semanticRole.contains("roll") ? 1 : 0;
                        auto m = measure(project, g.genre);
                        if (seed <= renderSeeds)
                            for (const auto lane : { std::optional<TrackType>(TrackType::Kick), std::optional<TrackType>(TrackType::Sub808), std::optional<TrackType>() })
                            {
                                const auto temporary = processor.createTemporaryWavFile(lane);
                                const auto file = out.getChildFile(tag+"_"+g.key+"_"+juce::String(sub)+"_seed"+juce::String(seed)
                                    +"_"+(lane.has_value() ? (*lane==TrackType::Kick ? "kick" : "bass") : "mix")+".wav");
                                if (!temporary.copyFileTo(file)) return 21;
                                rendersCsv << g.key << "," << sub << "," << seed << "," << project.params.bpm << "," << project.params.keyRoot
                                           << (project.params.scaleMode == 1 ? "M" : "m") << ","
                                           << (lane.has_value() ? (*lane == TrackType::Kick ? "kick" : "bass") : "mix") << ","
                                           << file.getFullPathName() << "\n";
                            }
                        auto dumped = project;
                        if (dumpNotes && (perfSwingDelta != 0.0f || perfHumanizeDelta != 0.0f))
                        {
                            auto live = dumped.params;
                            live.swingPercent = juce::jlimit(50.0f, 75.0f, live.swingPercent + perfSwingDelta);
                            live.humanizeAmount = juce::jlimit(0.0f, 1.0f, live.humanizeAmount + perfHumanizeDelta);
                            PatternPerformanceTransformEngine::backfillMissingPerformanceBaseParams(dumped, dumped.params);
                            dumped.params = live;
                            PatternPerformanceTransformEngine::applyPerformanceFromBase(dumped, live);
                        }
                        if (dumpNotes)
                            for (const auto lane : { TrackType::Kick, TrackType::GhostKick, TrackType::Snare, TrackType::ClapGhostSnare, TrackType::Sub808,
                                                     TrackType::HiHat, TrackType::HatFX, TrackType::OpenHat })
                                for (const auto& n : notesOf(dumped, lane))
                                    notesCsv << g.key << "," << sub << "," << bars << "," << seed << "," << project.params.bpm << ","
                                             << project.params.keyRoot << (project.params.scaleMode == 1 ? "M" : "m") << ","
                                             << (lane == TrackType::Kick ? "kick" : lane == TrackType::GhostKick ? "ghostkick" : lane == TrackType::Snare ? "snare"
                                                 : lane == TrackType::ClapGhostSnare ? "clapghost" : lane == TrackType::HiHat ? "hat"
                                                 : lane == TrackType::HatFX ? "hatfx" : lane == TrackType::OpenHat ? "openhat" : "bass")
                                             << "," << n.gridTick << "," << n.timingOffsetTicks
                                             << "," << n.pitch << "," << n.lengthTicks << "," << n.velocity << "," << (n.glideToNext ? 1 : 0)
                                             << "," << effectiveDensity << "," << n.semanticRole.replace(",", " ") << "\n";
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
                            { "trueRollNotesPerBar", static_cast<float>(trueRollNotes) / bars },
                            { "patternsWithRoll", trueRollNotes >= 3 ? 1.0f : 0.0f },
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
                            { "bassOnKickRate", m.bassOnKickRate }, { "bassMeanLength", m.bassMeanLength },
                            { "bassGapFill", m.bassGapFill }, { "bassShortRate", m.bassShortRate },
                            { "eventsPerBar", m.eventsPerBar }, { "syncopation", m.syncopation },
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
                                 << m.firstTwoBars.hashCode64() << ',' << m.firstTwoKick.hashCode64() << ',' << m.kickBarsText << ',' << m.hatBarsText << ',' << m.kickAllBarsText << ',' << m.hatAllBarsText << ',' << m.hatAllNotesPerBar << ',' << m.displacedBackbeatPerBar << ',' << m.snareBarsText << ',' << m.bassOnSnareRate << ',' << m.drumBarsText << ',' << m.topBarsText << '\n';
                    }

                    // Determinism: a fresh processor replaying seeds 1..10 must give the same patterns.
                    int determinismFailures = 0;
                    {
                        BoomBapGeneratorAudioProcessor a, b;
                        for (auto* p : { &a, &b })
                        {
                            p->prepareToPlay(44100.0, 512);
                            auto& s = p->getApvts();
                            setChoice(s, ParamIds::trapRollMode, static_cast<float>(rollMode));
                            setChoice(s, ParamIds::genre, static_cast<float>(g.choice));
                            setChoice(s, g.substyleParam, static_cast<float>(sub));
                            p->applySelectedStylePreset(false);
                            if (fixedBpm > 0.0f)
                            {
                                setChoice(s, ParamIds::bpmLock, 1.0f);
                                setChoice(s, ParamIds::bpm, fixedBpm);
                            }
                            setChoice(s, ParamIds::bars, static_cast<float>(barsChoice(bars)));
                            p->syncBarsFromState();
                            if (density >= 0.0f)
                                setChoice(s, ParamIds::densityAmount, density);
                            if (!prepareSample(*p)) return 20;
                            setChoice(s, ParamIds::seedLock, 1.0f);
                        }
                        // b has generated before (another seed, three times): its session history differs,
                        // the same seed + settings must still give the same pattern (RULE 13).
                        for (int warmUp = 0; warmUp < 3; ++warmUp)
                        {
                            setChoice(b.getApvts(), ParamIds::seed, 7777.0f);
                            b.generatePattern();
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
    }
    json << "\n  ]\n}\n";
    out.getChildFile(tag + "_summary.json").replaceWithText(json);
    std::cout << "wrote " << out.getChildFile(tag + "_summary.json").getFullPathName() << std::endl;
    return 0;
}
