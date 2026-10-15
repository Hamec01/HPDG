#include "AtmosphericEngine.h"

#include <algorithm>
#include <cmath>

#include "PatternPerformanceTransformEngine.h"
#include "../Core/ProjectLaneAccess.h"
#include "../Core/Sub808Types.h"
#include "../Core/TrackRegistry.h"

namespace bbg
{
namespace
{
TrackState* findTrack(PatternProject& project, TrackType type)
{
    return ProjectLaneAccess::findTrackState(project, type);
}

juce::String describe(const AtmosPattern& p, const AtmosGenerationParams& params, int candidates, int passed, int pool, float best,
                      int selectedIndex)
{
    const auto& style = getAtmosStyleProfile(params.substyle);
    const auto& s = p.score;
    const auto& t = s.traits;
    juce::StringArray lines;
    lines.add("ATMOSPHERIC");
    lines.add("substyle: " + juce::String(style.name) + " | bars " + juce::String(p.bars) + " | seed " + juce::String(params.seed));
    lines.add("plan: " + p.plan);
    lines.add("candidates " + juce::String(candidates) + " | passed gates " + juce::String(passed) + " | near-best pool "
              + juce::String(pool) + " | best Q " + juce::String(best, 3) + " | selected #" + juce::String(selectedIndex) + " Q "
              + juce::String(s.quality, 3));
    lines.add("kicks / bar " + juce::String(t.kicksPerBar, 2) + " (" + juce::String(style.kicksPerBar, 1) + ") | snares / bar "
              + juce::String(t.snaresPerBar, 2) + " (" + juce::String(style.snaresPerBar, 1) + ") | hats / bar "
              + juce::String(t.hatsPerBar, 2) + " (" + juce::String(style.hatsPerBar, 1) + ") | kick off-beat "
              + juce::String(t.kickOffBeat, 2) + " | kick repeat " + juce::String(t.kickRepeat, 2)
              + (s.passedGates ? juce::String() : " | FAILED gate: " + s.failedGate));
    return lines.joinIntoString("\n");
}
} // namespace

const std::unordered_set<TrackType>& AtmosphericEngine::allLanes()
{
    // Every drum lane is written (lanes the grammar does not use are cleared), so a pattern made
    // in another genre leaves nothing behind.
    static const std::unordered_set<TrackType> lanes {
        TrackType::Kick, TrackType::GhostKick, TrackType::Snare, TrackType::HiHat, TrackType::HatFX,
        TrackType::OpenHat, TrackType::Ride, TrackType::Cymbal, TrackType::Perc
    };
    return lanes;
}

std::unordered_set<TrackType> AtmosphericEngine::laneGroupFor(TrackType trackType)
{
    switch (trackType)
    {
        case TrackType::Kick:
        case TrackType::GhostKick: return { TrackType::Kick, TrackType::GhostKick };
        case TrackType::HiHat:
        case TrackType::OpenHat:
        case TrackType::HatFX: return { TrackType::HiHat, TrackType::OpenHat, TrackType::HatFX };
        case TrackType::Snare:
        case TrackType::ClapGhostSnare: return { TrackType::Snare };
        default: break;
    }
    return { trackType };
}

AtmosGenerationParams AtmosphericEngine::paramsFromProject(const PatternProject& project, int seedSalt)
{
    AtmosGenerationParams params;
    params.seed = project.params.seed + seedSalt;
    params.bars = std::clamp(project.params.bars, 1, 16);
    params.bpm = project.params.bpm;
    params.density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    params.humanize = std::clamp(project.params.humanizeAmount, 0.0f, 1.0f);
    params.substyle = project.params.atmosphericSubstyle;
    return params;
}

AtmosPattern AtmosphericEngine::search(const AtmosGenerationParams& params, juce::String* debugReport, std::vector<AtmosPattern>* candidatesOut)
{
    const auto& style = getAtmosStyleProfile(params.substyle);
    const int count = std::clamp(params.candidateCount, 8, 256);
    std::vector<AtmosPattern> candidates;
    candidates.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        std::mt19937 rng(static_cast<std::mt19937::result_type>(static_cast<uint32_t>(params.seed) * 2654435761u
                                                                 + static_cast<uint32_t>(i) * 40503u + 0xa7a05u));
        auto pattern = AtmosphericGrammar::generateCandidate(params, style, rng);
        pattern.score = AtmosphericScorer::score(pattern, style, params.density);
        candidates.push_back(std::move(pattern));
    }

    // Bad candidates never win by chance; randomness chooses only inside the quality zone.
    std::vector<size_t> eligible;
    for (size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i].score.passedGates)
            eligible.push_back(i);
    const int passed = static_cast<int>(eligible.size());
    if (eligible.empty())
        for (size_t i = 0; i < candidates.size(); ++i)
            eligible.push_back(i);
    float best = -1.0e9f;
    for (const auto i : eligible)
        best = std::max(best, candidates[i].score.quality);
    std::vector<size_t> pool;
    std::vector<double> weights;
    double total = 0.0;
    for (const auto i : eligible)
        if (candidates[i].score.quality >= best - params.nearBestTolerance)
        {
            pool.push_back(i);
            weights.push_back(std::exp((candidates[i].score.quality - best) / std::max(0.001f, params.temperature)));
            total += weights.back();
        }
    std::mt19937 selection(static_cast<std::mt19937::result_type>(static_cast<uint32_t>(params.seed) * 747796405u + 0xa7a5u));
    double roll = std::uniform_real_distribution<double>(0.0, total)(selection);
    size_t chosen = pool.front();
    for (size_t k = 0; k < pool.size(); ++k)
    {
        roll -= weights[k];
        if (roll <= 0.0)
        {
            chosen = pool[k];
            break;
        }
    }
    if (debugReport != nullptr)
        *debugReport = describe(candidates[chosen], params, count, passed, static_cast<int>(pool.size()), best, static_cast<int>(chosen));
    if (candidatesOut != nullptr)
    {
        auto selected = candidates[chosen];
        *candidatesOut = std::move(candidates);
        return selected;
    }
    return std::move(candidates[chosen]);
}

void AtmosphericEngine::writePattern(PatternProject& project, const AtmosPattern& pattern, const std::unordered_set<TrackType>& lanes,
                                     const juce::String& debugReport)
{
    std::unordered_set<TrackType> written;
    for (const auto lane : lanes)
    {
        auto* track = findTrack(project, lane);
        if (track == nullptr || track->locked)
            continue;
        track->notes.clear();
        track->sub808Notes.clear();
        written.insert(lane);
        if (!track->enabled)
            continue;

        const auto* info = TrackRegistry::find(lane);
        const int pitch = info != nullptr ? info->defaultMidiNote : 36;
        for (const auto& e : pattern.events)
        {
            if (e.lane != lane)
                continue;
            NoteEvent note;
            note.pitch = pitch;
            note.gridTick = e.bar * TimingGrid::TicksPerBar4_4 + e.step * AtmosGrid::kPpqPerStep;
            note.timingOffsetTicks = e.micro;
            if (note.gridTick + note.timingOffsetTicks < 0)
                note.timingOffsetTicks = -note.gridTick;
            note.lengthTicks = lane == TrackType::OpenHat ? 2 * AtmosGrid::kPpqPerStep : AtmosGrid::kPpqPerStep;
            note.velocity = juce::jlimit(1, 127, e.velocity);
            note.isGhost = e.ghost;
            note.semanticRole = toString(e.role);
            track->notes.push_back(note);
        }
        std::sort(track->notes.begin(), track->notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
        track->subProfile = getAtmosStyleProfile(project.params.atmosphericSubstyle).name;
    }

    // Clap Ghost is a hidden lane: the clap lives on Snare.
    if (auto* track = findTrack(project, TrackType::ClapGhostSnare); track != nullptr && !track->locked && lanes.count(TrackType::Snare) > 0)
    {
        track->notes.clear();
        written.insert(TrackType::ClapGhostSnare);
    }

    project.phraseLengthBars = pattern.bars;
    project.phraseRoleSummary = pattern.plan;
    project.generationDebugReport = debugReport;
    PatternPerformanceTransformEngine::captureBasePatterns(project, written);
}

juce::String AtmosphericEngine::writeBass(PatternProject& project, int seedSalt)
{
    auto* bass = findTrack(project, TrackType::Sub808);
    if (bass == nullptr || bass->locked)
        return {};
    bass->sub808Notes.clear();
    if (!bass->enabled)
    {
        bass->notes.clear();
        PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::Sub808 });
        return {};
    }

    const int bars = std::clamp(project.params.bars, 1, 16);
    std::vector<std::vector<int>> kicks(static_cast<size_t>(bars));
    if (const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick); kick != nullptr)
        for (const auto& n : kick->notes)
        {
            const int step = static_cast<int>(std::lround(n.gridTick / static_cast<double>(AtmosGrid::kPpqPerStep)));
            const int bar = step / AtmosGrid::kStepsPerBar;
            if (bar >= 0 && bar < bars && !n.isGhost)
                kicks[static_cast<size_t>(bar)].push_back(step % AtmosGrid::kStepsPerBar);
        }

    AtmosBassParams params;
    params.seed = project.params.seed + seedSalt;
    params.bars = bars;
    params.substyle = project.params.atmosphericSubstyle;
    params.keyRoot = std::clamp(project.params.keyRoot, 0, 11);
    params.scaleMode = std::clamp(project.params.scaleMode, 0, 2);
    params.amount = std::clamp(bass->sub808Settings.bassAmount, 0, 2);
    params.density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);

    juce::String report;
    const auto line = AtmosphericBassGenerator::search(params, kicks, &report);
    bass->notes = AtmosphericBassGenerator::toNotes(line);
    bass->sub808Notes = toSub808NoteEvents(bass->notes); // the bass lane's canonical (glide-aware) form
    bass->subProfile = getAtmosStyleProfile(project.params.atmosphericSubstyle).name;
    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::Sub808 });
    return report;
}

void AtmosphericEngine::generate(PatternProject& project)
{
    juce::String report;
    const auto pattern = search(paramsFromProject(project, 0), &report); // seed + settings decide a Generate (RULE 13)
    writePattern(project, pattern, allLanes(), report);
    const auto bassReport = writeBass(project, 7);
    if (bassReport.isNotEmpty())
        project.generationDebugReport << "\n" << bassReport;
}

void AtmosphericEngine::regenerateTrack(PatternProject& project, TrackType trackType)
{
    regenerateTrackVariation(project, trackType);
}

void AtmosphericEngine::generateTrackNew(PatternProject& project, TrackType trackType)
{
    if (trackType == TrackType::Sub808)
    {
        project.generationDebugReport = writeBass(project, project.generationCounter * 17 + 0x55);
        return;
    }
    const auto lanes = laneGroupFor(trackType);
    if (std::none_of(lanes.begin(), lanes.end(), [](TrackType t) { return allLanes().count(t) > 0; }))
        return;
    juce::String report;
    const auto pattern = search(paramsFromProject(project, static_cast<int>(trackType) * 131 + project.generationCounter * 17), &report);
    writePattern(project, pattern, lanes, report);
}

void AtmosphericEngine::regenerateTrackVariation(PatternProject& project, TrackType trackType)
{
    generateTrackNew(project, trackType);
}

void AtmosphericEngine::mutatePattern(PatternProject& project)
{
    juce::String report;
    const auto pattern = search(paramsFromProject(project, project.mutationCounter * 911 + 17), &report);
    writePattern(project, pattern, allLanes(), report);
    const auto bassReport = writeBass(project, project.mutationCounter * 911 + 23);
    if (bassReport.isNotEmpty())
        project.generationDebugReport << "\n" << bassReport;
}

void AtmosphericEngine::mutateTrack(PatternProject& project, TrackType trackType)
{
    if (trackType == TrackType::Sub808)
    {
        project.generationDebugReport = writeBass(project, project.mutationCounter * 29 + 0x77);
        return;
    }
    const auto lanes = laneGroupFor(trackType);
    if (std::none_of(lanes.begin(), lanes.end(), [](TrackType t) { return allLanes().count(t) > 0; }))
        return;
    juce::String report;
    const auto pattern = search(paramsFromProject(project, static_cast<int>(trackType) * 199 + project.mutationCounter * 29), &report);
    writePattern(project, pattern, lanes, report);
}
} // namespace bbg
