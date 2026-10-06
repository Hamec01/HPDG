// HPDG Scorer Audit (docs/ROADMAP_UPDATE.md sections 8 and 11).
//
//   HPDG_ScorerAudit scorers   [--seeds N] [--genre ...] [--out <folder>]
//       Every scored candidate of each engine (the same code path generate() runs), measured
//       with the shared metrics (Tests/QualityMetrics.h). Within each seed's pool candidates are
//       ranked by the engine's own quality: top 10 % / middle / bottom 10 % feature means and the
//       within-seed rank correlation (Spearman) of quality with each metric. A scorer is valid
//       when high quality goes with better structure, not with hidden pathologies.
//
//   HPDG_ScorerAudit candidates [--seeds N] [--genre ...] [--out <folder>]
//       Candidate counts 16 / 32 / 48 / 64 / 96 / 128: selected quality, failures, duplicate rate,
//       p50 / p95 / worst time per generate() call (RULE 17).
//
// Engine parameters mirror the production engines' defaults for each substyle (style default
// density / swing / humanize / BPM). Generation itself is not changed.

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>

#include "../Source/Engine/BoomBap/BoomBapClassicAlgebraGenerator.h"
#include "../Source/Engine/BoomBap/BoomBapStyleProfile.h"
#include "../Source/Engine/DnBEngine.h"
#include "../Source/Engine/StyleDefaults.h"
#include "../Source/Engine/TechnoEngine.h"
#include "../Source/Engine/Trap/TrapAlgebraEngine.h"
#include "QualityMetrics.h"

using namespace bbg;
using namespace bbg::quality;

namespace
{
const std::array<TrackType, 11> kLaneOrder { TrackType::HiHat, TrackType::HatFX, TrackType::OpenHat, TrackType::Snare,
                                             TrackType::ClapGhostSnare, TrackType::Kick, TrackType::GhostKick, TrackType::Ride,
                                             TrackType::Cymbal, TrackType::Perc, TrackType::Sub808 };

struct Candidate
{
    float quality = 0.0f;
    bool valid = true;
    Metrics metrics;
};

struct Pool
{
    std::vector<Candidate> candidates;
    int selected = 0;
    double ms = 0.0;
};

PatternProject emptyProject(GenreType genre, int bars)
{
    auto project = createDefaultProject();
    project.params.genre = genre;
    project.params.bars = bars;
    for (auto& track : project.tracks)
    {
        track.enabled = true;
        track.muted = false;
        track.notes.clear();
    }
    return project;
}

void addNote(PatternProject& project, TrackType lane, int gridTick, int offset, int velocity, bool ghost)
{
    if (auto* track = ProjectLaneAccess::findTrackState(project, lane))
    {
        NoteEvent n;
        n.gridTick = std::max(0, gridTick);
        n.timingOffsetTicks = offset;
        n.velocity = velocity;
        n.isGhost = ghost;
        track->notes.push_back(n);
    }
}

void sortNotes(PatternProject& project)
{
    for (auto& track : project.tracks)
        std::sort(track.notes.begin(), track.notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
}

const GenreStyleDefaults& defaults(GenreType genre, int sub) { return getGenreStyleDefaults(genre, sub); }

// --- engines ---------------------------------------------------------------------------------
Pool boomBapPool(int sub, int seed, int bars, int count)
{
    const auto& d = defaults(GenreType::BoomBap, sub);
    const auto& style = getBoomBapProfile(sub);
    BoomBapClassicAlgebraParams params;
    params.seed = seed;
    params.bars = bars;
    params.bpm = d.bpmDefault;
    params.density = d.densityDefault;
    params.swing = std::clamp(d.swingDefault / 100.0f, 0.50f, 0.75f);
    params.humanize = std::clamp(d.humanizeDefault * (0.65f + d.timingDefault * 0.55f), 0.0f, 1.0f);
    params.variation = std::clamp(style.barVariationAmount + d.densityDefault * 0.24f, 0.0f, 1.0f);
    params.candidateCount = count;
    params.substyle = style.substyle;

    std::vector<BoomBapClassicAlgebraPattern> all;
    const auto start = std::chrono::steady_clock::now();
    const auto chosen = BoomBapClassicAlgebraGenerator().generate(params, &all);
    Pool pool;
    pool.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    pool.selected = chosen.selectedCandidateIndex;
    for (const auto& c : all)
    {
        auto project = emptyProject(GenreType::BoomBap, bars);
        for (int lane = 0; lane < BoomBapClassicLanes::Count; ++lane)
            for (const auto& n : c.notesByLane[static_cast<size_t>(lane)])
                addNote(project, kLaneOrder[static_cast<size_t>(lane)], n.tick64 * 60, n.microTimingTicks, n.velocity,
                        n.role == BoomBapClassicRole::Ghost || lane == BoomBapClassicLanes::ClapGhost || lane == BoomBapClassicLanes::KickGhost);
        sortNotes(project);
        pool.candidates.push_back({ c.selectionQuality, !c.score.hardTrapLeak, measure(project, GenreType::BoomBap) });
    }
    return pool;
}

TrapAlgebraSubstyle trapSubstyle(int sub)
{
    switch (sub)
    {
        case 1: return TrapAlgebraSubstyle::DarkTrap;
        case 2: return TrapAlgebraSubstyle::CloudTrap;
        case 3: return TrapAlgebraSubstyle::RageTrap;
        case 4: return TrapAlgebraSubstyle::MemphisTrap;
        case 5: return TrapAlgebraSubstyle::LuxuryTrap;
        default: return TrapAlgebraSubstyle::ATLClassic;
    }
}

Pool trapPool(int sub, int seed, int bars, int count)
{
    const auto& d = defaults(GenreType::Trap, sub);
    TrapAlgebraParams params;
    params.seed = seed;
    params.bars = bars;
    params.bpm = d.bpmDefault;
    params.density = d.densityDefault;
    params.swing = std::clamp(d.swingDefault / 100.0f, 0.50f, 0.60f);
    params.humanize = d.humanizeDefault;
    params.variation = std::max(d.velocityDefault, d.timingDefault);
    params.temperature = 0.40f;
    params.qMin = 0.62f;
    params.candidateCount = count;
    params.substyle = trapSubstyle(sub);

    std::vector<TrapAlgebraPattern> all;
    const auto start = std::chrono::steady_clock::now();
    const auto chosen = TrapAlgebraEngine().generate(params, &all);
    Pool pool;
    pool.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    pool.selected = chosen.selectedCandidateIndex;
    for (const auto& c : all)
    {
        auto project = emptyProject(GenreType::Trap, bars);
        for (int lane = 0; lane < TrapAlgebraLanes::Count; ++lane)
            for (const auto& n : c.matrix.notesForLane(lane))
                addNote(project, kLaneOrder[static_cast<size_t>(lane)], n.tick64 * 60, n.microTimingTicks, n.velocity,
                        lane == TrapAlgebraLanes::ClapGhost || lane == TrapAlgebraLanes::KickGhost);
        sortNotes(project);
        pool.candidates.push_back({ c.selectionQuality, c.acceptedByThreshold, measure(project, GenreType::Trap) });
    }
    return pool;
}

template <typename Pattern>
int findSelected(const std::vector<Pattern>& all, const Pattern& chosen)
{
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].score.quality == chosen.score.quality && all[i].events.size() == chosen.events.size())
            return static_cast<int>(i);
    return 0;
}

Pool dnbPool(int sub, int seed, int bars, int count)
{
    const auto& d = defaults(GenreType::DnB, sub);
    auto project = emptyProject(GenreType::DnB, bars);
    project.params.seed = seed;
    project.params.bpm = d.bpmDefault;
    project.params.densityAmount = d.densityDefault;
    project.params.swingPercent = d.swingDefault;
    project.params.humanizeAmount = d.humanizeDefault;
    project.params.timingAmount = d.timingDefault;
    project.params.dnbSubstyle = sub;
    auto params = DnBEngine::paramsFromProject(project, 0);
    params.candidateCount = count;

    std::vector<DnBPattern> all;
    const auto start = std::chrono::steady_clock::now();
    const auto chosen = DnBEngine::search(params, nullptr, &all);
    Pool pool;
    pool.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    pool.selected = findSelected(all, chosen);
    for (const auto& c : all)
    {
        auto p = emptyProject(GenreType::DnB, bars);
        for (const auto& e : c.events)
            addNote(p, e.lane, e.bar * 3840 + e.tick * DnBGrid::kPpqPerTick, e.micro, e.velocity, e.ghost);
        sortNotes(p);
        pool.candidates.push_back({ c.score.quality, c.score.passedGates, measure(p, GenreType::DnB) });
    }
    return pool;
}

Pool technoPool(int sub, int seed, int bars, int count)
{
    const auto& d = defaults(GenreType::Techno, sub);
    auto project = emptyProject(GenreType::Techno, bars);
    project.params.seed = seed;
    project.params.bpm = d.bpmDefault;
    project.params.densityAmount = d.densityDefault;
    project.params.swingPercent = d.swingDefault;
    project.params.humanizeAmount = d.humanizeDefault;
    project.params.technoSubstyle = sub;
    auto params = TechnoEngine::paramsFromProject(project, 0);
    params.candidateCount = count;

    std::vector<TechnoPattern> all;
    const auto start = std::chrono::steady_clock::now();
    const auto chosen = TechnoEngine::search(params, nullptr, &all);
    Pool pool;
    pool.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    pool.selected = findSelected(all, chosen);
    for (const auto& c : all)
    {
        auto p = emptyProject(GenreType::Techno, bars);
        for (const auto& e : c.events)
            addNote(p, e.lane, e.bar * 3840 + e.step * TechnoGrid::kPpqPerStep + e.subTick, e.micro, e.velocity, e.ghost);
        sortNotes(p);
        pool.candidates.push_back({ c.score.quality, c.score.passedGates, measure(p, GenreType::Techno) });
    }
    return pool;
}

struct GenreEntry
{
    const char* key;
    GenreType genre;
    juce::StringArray substyles;
    int productionCount;
    Pool (*pool)(int, int, int, int);
};

std::vector<GenreEntry> entries()
{
    return {
        { "boombap", GenreType::BoomBap, getBoomBapSubstyleNames(), 64, boomBapPool },
        { "trap", GenreType::Trap, getTrapSubstyleNames(), 32, trapPool },
        { "dnb", GenreType::DnB, getDnBSubstyleNames(), 48, dnbPool },
        { "techno", GenreType::Techno, getTechnoSubstyleNames(), 48, technoPool },
    };
}

// The metrics compared across score groups (name, accessor, "better" direction note).
struct MetricDef
{
    const char* name;
    float (*get)(const Metrics&);
};

const std::vector<MetricDef>& metricDefs()
{
    static const std::vector<MetricDef> defs {
        { "hardFailure", [](const Metrics& m) { return m.hardFailures > 0 ? 1.0f : 0.0f; } },
        { "kicksPerBar", [](const Metrics& m) { return m.kicksPerBar; } },
        { "kickSyncopation", [](const Metrics& m) { return m.kickSyncopation; } },
        { "kickConsecutiveRate", [](const Metrics& m) { return m.kickConsecutiveRate; } },
        { "kickSnareCollisionsPerBar", [](const Metrics& m) { return m.kickSnareCollisionsPerBar; } },
        { "backbeatCoverage", [](const Metrics& m) { return m.backbeatCoverage; } },
        { "snareGhostRate", [](const Metrics& m) { return m.snareGhostRate; } },
        { "ghostLouderThanAnchor", [](const Metrics& m) { return m.ghostLouderThanAnchor; } },
        { "hatEighthCoverage", [](const Metrics& m) { return m.hatEighthCoverage; } },
        { "hatMaxGapSteps", [](const Metrics& m) { return m.hatMaxGapSteps; } },
        { "openClosedCollisions", [](const Metrics& m) { return m.openClosedCollisions; } },
        { "barSimilarity", [](const Metrics& m) { return m.barSimilarity; } },
        { "identicalBarRate", [](const Metrics& m) { return m.identicalBarRate; } },
        { "eventsPerBar", [](const Metrics& m) { return m.eventsPerBar; } },
        { "velocityStd", [](const Metrics& m) { return m.velocityStd; } },
        { "timingOutlierRate", [](const Metrics& m) { return m.timingOutlierRate; } },
        { "syncopation", [](const Metrics& m) { return m.syncopation; } },
    };
    return defs;
}

std::vector<double> ranks(const std::vector<double>& v)
{
    std::vector<size_t> order(v.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return v[a] < v[b]; });
    std::vector<double> r(v.size());
    for (size_t i = 0; i < order.size();)
    {
        size_t j = i;
        while (j + 1 < order.size() && v[order[j + 1]] == v[order[i]])
            ++j;
        for (size_t k = i; k <= j; ++k)
            r[order[k]] = 0.5 * (i + j);
        i = j + 1;
    }
    return r;
}

double spearman(const std::vector<double>& x, const std::vector<double>& y)
{
    const auto rx = ranks(x);
    const auto ry = ranks(y);
    const double n = static_cast<double>(x.size());
    const double mx = std::accumulate(rx.begin(), rx.end(), 0.0) / n;
    const double my = std::accumulate(ry.begin(), ry.end(), 0.0) / n;
    double sxy = 0, sxx = 0, syy = 0;
    for (size_t i = 0; i < rx.size(); ++i)
    {
        sxy += (rx[i] - mx) * (ry[i] - my);
        sxx += (rx[i] - mx) * (rx[i] - mx);
        syy += (ry[i] - my) * (ry[i] - my);
    }
    return sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : std::nan("");
}

juce::String num(double v, int digits = 3) { return std::isfinite(v) ? juce::String(v, digits) : juce::String("null"); }

int runScorers(int seeds, const juce::String& genreFilter, const juce::File& out)
{
    juce::String json = "{\n  \"seeds\": " + juce::String(seeds) + ",\n  \"configurations\": [\n";
    bool firstConfig = true;
    for (const auto& g : entries())
    {
        if (genreFilter != "all" && genreFilter != g.key)
            continue;
        for (int sub = 0; sub < g.substyles.size(); ++sub)
        {
            const auto& defs = metricDefs();
            std::vector<double> top(defs.size(), 0.0), mid(defs.size(), 0.0), bottom(defs.size(), 0.0), selected(defs.size(), 0.0);
            std::vector<double> rhoSum(defs.size(), 0.0);
            std::vector<int> rhoCount(defs.size(), 0);
            int topN = 0, midN = 0, bottomN = 0, invalidTop = 0, failingTop = 0, candidatesTotal = 0, invalidTotal = 0;
            for (int seed = 1; seed <= seeds; ++seed)
            {
                const auto pool = g.pool(sub, seed, 4, g.productionCount);
                const auto& c = pool.candidates;
                if (c.size() < 10)
                    continue;
                std::vector<size_t> order(c.size());
                std::iota(order.begin(), order.end(), 0);
                std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return c[a].quality > c[b].quality; });
                const size_t decile = std::max<size_t>(1, c.size() / 10);
                candidatesTotal += static_cast<int>(c.size());
                for (size_t r = 0; r < order.size(); ++r)
                {
                    const auto& cand = c[order[r]];
                    invalidTotal += cand.valid ? 0 : 1;
                    auto& bucket = r < decile ? top : (r >= order.size() - decile ? bottom : mid);
                    (r < decile ? topN : (r >= order.size() - decile ? bottomN : midN)) += 1;
                    if (r < decile)
                    {
                        invalidTop += cand.valid ? 0 : 1;
                        failingTop += cand.metrics.hardFailures > 0 ? 1 : 0;
                    }
                    for (size_t k = 0; k < defs.size(); ++k)
                        bucket[k] += defs[k].get(cand.metrics);
                }
                for (size_t k = 0; k < defs.size(); ++k)
                    selected[k] += defs[k].get(c[static_cast<size_t>(pool.selected)].metrics);
                std::vector<double> q;
                for (const auto& cand : c)
                    q.push_back(cand.quality);
                for (size_t k = 0; k < defs.size(); ++k)
                {
                    std::vector<double> v;
                    for (const auto& cand : c)
                        v.push_back(defs[k].get(cand.metrics));
                    const double rho = spearman(q, v);
                    if (std::isfinite(rho))
                    {
                        rhoSum[k] += rho;
                        ++rhoCount[k];
                    }
                }
            }
            std::cout << g.key << " / " << g.substyles[sub] << ": candidates " << candidatesTotal << " (invalid " << invalidTotal
                      << ") | top-10% invalid " << invalidTop << " / with structural failures " << failingTop << "\n";
            std::cout << "    metric                      top10%    middle  bottom10%  selected   rho(quality)\n";
            for (size_t k = 0; k < defs.size(); ++k)
                std::cout << "    " << juce::String(defs[k].name).paddedRight(' ', 26) << juce::String(top[k] / std::max(1, topN), 3).paddedLeft(' ', 9)
                          << juce::String(mid[k] / std::max(1, midN), 3).paddedLeft(' ', 10) << juce::String(bottom[k] / std::max(1, bottomN), 3).paddedLeft(' ', 10)
                          << juce::String(selected[k] / seeds, 3).paddedLeft(' ', 10)
                          << num(rhoCount[k] > 0 ? rhoSum[k] / rhoCount[k] : std::nan(""), 2).paddedLeft(' ', 12) << "\n";

            json << (firstConfig ? "" : ",\n") << "    { \"genre\": \"" << g.key << "\", \"substyle\": \"" << g.substyles[sub]
                 << "\", \"candidates\": " << candidatesTotal << ", \"invalid\": " << invalidTotal << ", \"topInvalid\": " << invalidTop
                 << ", \"topStructuralFailures\": " << failingTop << ", \"metrics\": {";
            for (size_t k = 0; k < defs.size(); ++k)
                json << (k > 0 ? ", " : " ") << "\"" << defs[k].name << "\": { \"top\": " << num(top[k] / std::max(1, topN), 4)
                     << ", \"middle\": " << num(mid[k] / std::max(1, midN), 4) << ", \"bottom\": " << num(bottom[k] / std::max(1, bottomN), 4)
                     << ", \"selected\": " << num(selected[k] / seeds, 4) << ", \"rho\": " << num(rhoCount[k] > 0 ? rhoSum[k] / rhoCount[k] : std::nan(""), 4) << " }";
            json << " } }";
            firstConfig = false;
        }
    }
    json << "\n  ]\n}\n";
    out.getChildFile("scorer_validation.json").replaceWithText(json);
    return 0;
}

int runCandidates(int seeds, const juce::String& genreFilter, const juce::File& out)
{
    const std::vector<int> counts { 16, 32, 48, 64, 96, 128 };
    juce::String json = "{\n  \"seeds\": " + juce::String(seeds) + ",\n  \"configurations\": [\n";
    bool firstConfig = true;
    for (const auto& g : entries())
    {
        if (genreFilter != "all" && genreFilter != g.key)
            continue;
        for (int sub = 0; sub < g.substyles.size(); ++sub)
        {
            std::cout << g.key << " / " << g.substyles[sub] << " (production " << g.productionCount << ")\n"
                      << "    count  selQ     bestQ    fail%   exactDup%  p50ms   p95ms   worstMs\n";
            for (const int count : counts)
            {
                std::vector<double> times;
                double selQ = 0.0, bestQ = 0.0;
                int failures = 0, duplicates = 0;
                std::set<juce::String> seen;
                for (int seed = 1; seed <= seeds; ++seed)
                {
                    const auto pool = g.pool(sub, seed, 4, count);
                    times.push_back(pool.ms);
                    const auto& chosen = pool.candidates[static_cast<size_t>(pool.selected)];
                    selQ += chosen.quality;
                    float best = -1.0e9f;
                    for (const auto& c : pool.candidates)
                        if (c.valid)
                            best = std::max(best, c.quality);
                    bestQ += best;
                    failures += chosen.metrics.hardFailures > 0 ? 1 : 0;
                    duplicates += seen.insert(chosen.metrics.skeleton).second ? 0 : 1;
                }
                std::sort(times.begin(), times.end());
                auto pct = [&](double q) { return times[std::min(times.size() - 1, static_cast<size_t>(std::lround(q * (times.size() - 1))))]; };
                std::cout << "    " << juce::String(count).paddedLeft(' ', 5) << juce::String(selQ / seeds, 4).paddedLeft(' ', 9)
                          << juce::String(bestQ / seeds, 4).paddedLeft(' ', 9) << juce::String(100.0 * failures / seeds, 1).paddedLeft(' ', 8)
                          << juce::String(100.0 * duplicates / seeds, 1).paddedLeft(' ', 10) << juce::String(pct(0.5), 2).paddedLeft(' ', 8)
                          << juce::String(pct(0.95), 2).paddedLeft(' ', 8) << juce::String(times.back(), 2).paddedLeft(' ', 9) << "\n";
                json << (firstConfig ? "" : ",\n") << "    { \"genre\": \"" << g.key << "\", \"substyle\": \"" << g.substyles[sub] << "\", \"count\": " << count
                     << ", \"selectedQuality\": " << num(selQ / seeds, 5) << ", \"bestQuality\": " << num(bestQ / seeds, 5)
                     << ", \"failureRate\": " << num(static_cast<double>(failures) / seeds, 4) << ", \"exactDuplicateRate\": " << num(static_cast<double>(duplicates) / seeds, 4)
                     << ", \"p50Ms\": " << num(pct(0.5), 3) << ", \"p95Ms\": " << num(pct(0.95), 3) << ", \"worstMs\": " << num(times.back(), 3) << " }";
                firstConfig = false;
            }
        }
    }
    json << "\n  ]\n}\n";
    out.getChildFile("candidate_count.json").replaceWithText(json);
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "usage: HPDG_ScorerAudit scorers|candidates [--seeds N] [--genre boombap|trap|dnb|techno|all] [--out <folder>]\n";
        return 1;
    }
    const juce::String mode(argv[1]);
    int seeds = mode == "scorers" ? 200 : 200;
    juce::String genre = "all";
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile("docs/audit/phase1");
    for (int i = 2; i < argc; ++i)
    {
        const juce::String a(argv[i]);
        if (a == "--seeds" && i + 1 < argc) seeds = std::max(1, juce::String(argv[++i]).getIntValue());
        else if (a == "--genre" && i + 1 < argc) genre = juce::String(argv[++i]).toLowerCase();
        else if (a == "--out" && i + 1 < argc) out = juce::File(juce::String(argv[++i]));
    }
    out.createDirectory();
    if (mode == "scorers")
        return runScorers(seeds, genre, out);
    if (mode == "candidates")
        return runCandidates(seeds, genre, out);
    return 1;
}
