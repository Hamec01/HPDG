#include "DnBEngine.h"

#include <algorithm>
#include <cmath>

#include "PatternPerformanceTransformEngine.h"
#include "../Core/ProjectLaneAccess.h"
#include "../Core/TrackRegistry.h"
#include "DnB/DnBScorer.h"

namespace bbg
{
namespace
{
TrackState* findTrack(PatternProject& project, TrackType type)
{
    return ProjectLaneAccess::findTrackState(project, type);
}

int noteLengthFor(const DnBEvent& e)
{
    return juce::jmax(TimingGrid::ThirtySecond, e.length * DnBGrid::kPpqPerTick);
}

juce::String describe(const DnBPattern& p, const DnBGenerationParams& params, int candidates, int passed, int pool,
                      float best, int selectedIndex)
{
    const auto& style = getDnBStyleProfile(params.substyle);
    const auto& s = p.score;
    juce::StringArray lines;
    lines.add("DNB ALGEBRA");
    lines.add("substyle: " + juce::String(style.name) + " | bars " + juce::String(p.bars) + " | seed " + juce::String(params.seed));
    lines.add("topology: " + p.topology + " | carrier: " + toString(p.carrier));
    juce::String roles;
    for (size_t i = 0; i < p.barRoles.size(); ++i)
        roles << (i > 0 ? " " : "") << juce::String::charToString(p.barLetters[i]) << ":" << toString(p.barRoles[i]);
    lines.add("bar roles: " + roles);
    lines.add("candidates " + juce::String(candidates) + " | passed gates " + juce::String(passed) + " | near-best pool "
              + juce::String(pool) + " | best Q " + juce::String(best, 3) + " | selected #" + juce::String(selectedIndex)
              + " Q " + juce::String(s.quality, 3));
    lines.add("core " + juce::String(s.core, 3) + " = anchor " + juce::String(s.anchorClarity, 2) + " meter " + juce::String(s.meterStability, 2)
              + " interlock " + juce::String(s.interlock, 2) + " sync " + juce::String(s.syncopation, 3) + " (fit " + juce::String(s.syncopationFit, 2)
              + ") hierarchy " + juce::String(s.velocityHierarchy, 2));
    lines.add("secondary " + juce::String(s.secondary, 3) + " = forward " + juce::String(s.forwardMotion, 2) + " (fit " + juce::String(s.forwardFit, 2)
              + ") ghosts " + juce::String(s.ghostContext, 2) + " space " + juce::String(s.negativeSpace, 2) + " (fit " + juce::String(s.negativeSpaceFit, 2)
              + ") repetition " + juce::String(s.repetition, 2) + " (fit " + juce::String(s.repetitionFit, 2) + ") variation "
              + juce::String(s.variationFit, 2) + " resolution " + juce::String(s.phraseResolution, 2));
    lines.add("penalties " + juce::String(s.penalties, 3) + " | pruned secondary notes " + juce::String(p.prunedSecondary)
              + (s.passedGates ? juce::String() : " | FAILED gate: " + s.failedGate));
    return lines.joinIntoString("\n");
}
} // namespace

const std::unordered_set<TrackType>& DnBEngine::allLanes()
{
    static const std::unordered_set<TrackType> lanes {
        TrackType::Kick, TrackType::GhostKick, TrackType::Snare, TrackType::HiHat, TrackType::HatFX,
        TrackType::OpenHat, TrackType::Ride, TrackType::Cymbal
    };
    return lanes;
}

// Lanes that form one gesture are written together so they never disagree.
std::unordered_set<TrackType> DnBEngine::laneGroupFor(TrackType trackType)
{
    switch (trackType)
    {
        case TrackType::Kick:
        case TrackType::GhostKick: return { TrackType::Kick, TrackType::GhostKick };
        case TrackType::HiHat:
        case TrackType::HatFX: return { TrackType::HiHat, TrackType::HatFX };
        case TrackType::Snare:
        case TrackType::ClapGhostSnare: return { TrackType::Snare };
        default: break;
    }
    return { trackType };
}

DnBGenerationParams DnBEngine::paramsFromProject(const PatternProject& project, int seedSalt)
{
    DnBGenerationParams params;
    params.seed = project.params.seed + seedSalt;
    params.bars = std::clamp(project.params.bars, 1, 16);
    params.bpm = project.params.bpm;
    params.density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    params.swingPercent = project.params.swingPercent;
    params.humanize = std::clamp(project.params.humanizeAmount, 0.0f, 1.0f);
    params.variation = std::clamp(0.3f + project.params.timingAmount * 0.4f, 0.0f, 1.0f);
    params.substyle = project.params.dnbSubstyle;
    return params;
}

DnBPattern DnBEngine::search(const DnBGenerationParams& params, juce::String* debugReport)
{
    const auto& style = getDnBStyleProfile(params.substyle);
    const int count = std::clamp(params.candidateCount, 8, 256);

    std::vector<DnBPattern> candidates;
    candidates.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        std::mt19937 rng(static_cast<std::mt19937::result_type>(static_cast<uint32_t>(params.seed) * 2654435761u
                                                                 + static_cast<uint32_t>(i) * 40503u + 0x444e42u));
        auto pattern = DnBGrammar::generateCandidate(params, style, rng);
        pattern.score = DnBScorer::score(pattern, style);
        pattern.prunedSecondary = DnBScorer::pruneSecondary(pattern, style);
        if (pattern.prunedSecondary > 0)
        {
            DnBGrammar::repair(pattern);
            pattern.score = DnBScorer::score(pattern, style);
        }
        candidates.push_back(std::move(pattern));
    }

    // Bad candidates never win by chance: only gate-passing ones (all, if none passed).
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

    // Near-best pool + softmax at a low temperature: diversity inside the quality zone.
    std::vector<size_t> pool;
    for (const auto i : eligible)
        if (candidates[i].score.quality >= best - params.nearBestTolerance)
            pool.push_back(i);
    std::vector<double> weights;
    double total = 0.0;
    for (const auto i : pool)
    {
        const double w = std::exp((candidates[i].score.quality - best) / std::max(0.001f, params.temperature));
        weights.push_back(w);
        total += w;
    }
    std::mt19937 selection(static_cast<std::mt19937::result_type>(static_cast<uint32_t>(params.seed) * 747796405u + 0x53454cu));
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
    return std::move(candidates[chosen]);
}

void DnBEngine::writePattern(PatternProject& project, const DnBPattern& pattern, const std::unordered_set<TrackType>& lanes,
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
            // An open hat replaces (chokes) the closed hat on the same step.
            if (lane == TrackType::HiHat && pattern.has(TrackType::OpenHat, e.bar, e.tick))
                continue;
            NoteEvent note;
            note.pitch = pitch;
            note.gridTick = e.bar * TimingGrid::TicksPerBar4_4 + e.tick * DnBGrid::kPpqPerTick;
            note.timingOffsetTicks = e.micro;
            if (note.gridTick + note.timingOffsetTicks < 0)
                note.timingOffsetTicks = -note.gridTick;
            note.lengthTicks = noteLengthFor(e);
            note.velocity = juce::jlimit(1, 127, e.velocity);
            note.isGhost = e.ghost;
            note.semanticRole = toString(e.role);
            track->notes.push_back(note);
        }
        std::sort(track->notes.begin(), track->notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
        track->subProfile = getDnBStyleProfile(project.params.dnbSubstyle).name;
    }

    // Clap Ghost / Perc are hidden lanes: DnB keeps them empty (ghost snares live on Snare).
    for (const auto hidden : { TrackType::ClapGhostSnare, TrackType::Perc })
        if (auto* track = findTrack(project, hidden); track != nullptr && !track->locked && lanes.count(TrackType::Snare) > 0)
        {
            track->notes.clear();
            written.insert(hidden);
        }

    project.phraseLengthBars = pattern.bars;
    juce::String roles;
    for (size_t i = 0; i < pattern.barRoles.size(); ++i)
        roles << (i > 0 ? " | " : "") << toString(pattern.barRoles[i]);
    project.phraseRoleSummary = roles;
    project.generationDebugReport = debugReport;
    PatternPerformanceTransformEngine::captureBasePatterns(project, written);
}

DnBDrumFrame DnBEngine::drumsFromProject(const PatternProject& project)
{
    const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
    const auto* snare = ProjectLaneAccess::findTrackState(project, TrackType::Snare);
    return DnBDrumFrame::fromNotes(kick != nullptr ? kick->notes : std::vector<NoteEvent> {},
                                   snare != nullptr ? snare->notes : std::vector<NoteEvent> {},
                                   std::clamp(project.params.bars, 1, 16));
}

juce::String DnBEngine::writeBass(PatternProject& project, const DnBDrumFrame& drums, int seedSalt)
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

    DnBBassParams params;
    params.seed = project.params.seed + seedSalt;
    params.bars = std::clamp(project.params.bars, 1, 16);
    params.substyle = project.params.dnbSubstyle;
    params.keyRoot = std::clamp(project.params.keyRoot, 0, 11);
    params.scaleMode = std::clamp(project.params.scaleMode, 0, 2);
    params.amount = std::clamp(bass->sub808Settings.bassAmount, 0, 2);
    params.density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    const auto lens = DnBSampleLens::build(project.sampleContext, params.bars, project.params.bpm);

    juce::String report;
    const auto line = DnBBassGenerator::search(params, drums, lens, &report);
    bass->notes = DnBBassGenerator::toNotes(line);
    bass->subProfile = getDnBStyleProfile(project.params.dnbSubstyle).name;
    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::Sub808 });
    return report;
}

void DnBEngine::generate(PatternProject& project)
{
    juce::String report;
    const auto pattern = search(paramsFromProject(project, project.generationCounter * 131), &report);
    writePattern(project, pattern, allLanes(), report);
    const auto bassReport = writeBass(project, drumsFromProject(project), project.generationCounter * 131 + 7);
    if (bassReport.isNotEmpty())
        project.generationDebugReport << "\n" << bassReport;
}

void DnBEngine::regenerateTrack(PatternProject& project, TrackType trackType)
{
    regenerateTrackVariation(project, trackType);
}

void DnBEngine::generateTrackNew(PatternProject& project, TrackType trackType)
{
    if (trackType == TrackType::Sub808)
    {
        // RG on the bass: a new line against the drums that are there now.
        project.generationDebugReport = writeBass(project, drumsFromProject(project), project.generationCounter * 17 + 0x55);
        return;
    }
    const auto lanes = laneGroupFor(trackType);
    if (std::none_of(lanes.begin(), lanes.end(), [](TrackType t) { return allLanes().count(t) > 0; }))
        return; // e.g. the bass lane: not part of the DnB drum generator
    juce::String report;
    const auto pattern = search(paramsFromProject(project, static_cast<int>(trackType) * 131 + project.generationCounter * 17), &report);
    writePattern(project, pattern, lanes, report);
}

void DnBEngine::regenerateTrackVariation(PatternProject& project, TrackType trackType)
{
    generateTrackNew(project, trackType);
}

void DnBEngine::mutatePattern(PatternProject& project)
{
    auto params = paramsFromProject(project, project.mutationCounter * 911 + 17);
    params.variation = std::min(1.0f, params.variation + 0.15f);
    juce::String report;
    const auto pattern = search(params, &report);
    writePattern(project, pattern, allLanes(), report);
    const auto bassReport = writeBass(project, drumsFromProject(project), project.mutationCounter * 911 + 23);
    if (bassReport.isNotEmpty())
        project.generationDebugReport << "\n" << bassReport;
}

void DnBEngine::mutateTrack(PatternProject& project, TrackType trackType)
{
    if (trackType == TrackType::Sub808)
    {
        project.generationDebugReport = writeBass(project, drumsFromProject(project), project.mutationCounter * 29 + 0x77);
        return;
    }
    const auto lanes = laneGroupFor(trackType);
    if (std::none_of(lanes.begin(), lanes.end(), [](TrackType t) { return allLanes().count(t) > 0; }))
        return;
    auto params = paramsFromProject(project, static_cast<int>(trackType) * 199 + project.mutationCounter * 29);
    params.variation = std::min(1.0f, params.variation + 0.15f);
    juce::String report;
    const auto pattern = search(params, &report);
    writePattern(project, pattern, lanes, report);
}
} // namespace bbg
