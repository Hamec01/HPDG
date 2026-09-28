// Regression safety net for the TIMING GRID V2 migration (NoteEvent -> tick-native).
// Not a permanent test: run once against pre-migration code (BASELINE build) and once against
// post-migration code, then diff the two text dumps. Every generated note's true tick (via
// HiResTiming::noteTick, which stays semantically stable across the migration), pitch, velocity,
// ghost flag and semantic role must match exactly for the same genre/substyle/seed.
//
// Build once against HEAD (git stash the migration files, `cmake --build build --config Release
// --target HPDG_TimingBaselineDump`, run, save output as baseline.txt), then again after the
// migration is complete (git stash pop, rebuild, run, save as after.txt) and `diff baseline.txt
// after.txt`.
#include <cstdio>
#include <vector>

#include "../Source/Core/PatternProject.h"
#include "../Source/Core/TrackRegistry.h"
#include "../Source/Engine/BoomBapEngine.h"
#include "../Source/Engine/DrillEngine.h"
#include "../Source/Engine/HiResTiming.h"
#include "../Source/Engine/RapEngine.h"
#include "../Source/Engine/TrapEngine.h"
#include "../Source/Utils/TimingHelpers.h"

using namespace bbg;

namespace
{
// NOTE: this one line differs between the BASELINE run (pre-migration NoteEvent: `.length` is a
// count of 1/16 steps) and the AFTER run (post-migration NoteEvent: `.lengthTicks` is exact).
// Toggle it by hand between the two runs — everything else in this file is representation-agnostic.
int lengthTicksOf(const NoteEvent& note)
{
    return note.lengthTicks; // AFTER-migration line. Baseline line: return note.length * ticksPerStep();
}

void dumpTrack(const char* genre, int substyle, int seed, const TrackState& track)
{
    auto notes = track.notes;
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (HiResTiming::noteTick(a) != HiResTiming::noteTick(b))
            return HiResTiming::noteTick(a) < HiResTiming::noteTick(b);
        return a.pitch < b.pitch;
    });

    for (const auto& note : notes)
    {
        std::printf("%s,%d,%d,%s,%d,%d,%d,%d,%d,%s\n",
                   genre,
                   substyle,
                   seed,
                   TrackRegistry::find(track.type) != nullptr ? TrackRegistry::find(track.type)->displayName.toRawUTF8() : "?",
                   HiResTiming::noteTick(note),
                   lengthTicksOf(note),
                   note.pitch,
                   note.velocity,
                   note.isGhost ? 1 : 0,
                   note.semanticRole.toRawUTF8());
    }
}

PatternProject makeProject(GenreType genre, int substyle, int seed, int bars, float bpm)
{
    auto project = createDefaultProject();
    project.params.genre = genre;
    project.params.bars = bars;
    project.params.seed = seed;
    project.params.bpm = bpm;
    project.params.densityAmount = 0.55f;
    project.params.swingPercent = 57.0f;
    project.params.humanizeAmount = 0.35f;
    project.params.timingAmount = 0.40f;
    project.params.velocityAmount = 0.50f;
    project.params.boombapSubstyle = substyle;
    project.params.rapSubstyle = substyle;
    project.params.trapSubstyle = substyle;
    project.params.drillSubstyle = substyle;

    for (auto& track : project.tracks)
        track.enabled = true;

    return project;
}

const int kSeeds[] { 1, 7, 42, 1000, 99999 };
}

int main()
{
    for (const int seed : kSeeds)
    {
        for (int substyle = 0; substyle < 4; ++substyle)
        {
            auto project = makeProject(GenreType::BoomBap, substyle, seed, 2, 90.0f);
            BoomBapEngine engine;
            engine.generate(project);
            for (const auto& track : project.tracks)
                dumpTrack("boombap", substyle, seed, track);
        }

        for (int substyle = 0; substyle < 3; ++substyle)
        {
            auto project = makeProject(GenreType::Rap, substyle, seed, 2, 92.0f);
            RapEngine engine;
            engine.generate(project);
            for (const auto& track : project.tracks)
                dumpTrack("rap", substyle, seed, track);
        }

        for (int substyle = 0; substyle < 3; ++substyle)
        {
            auto project = makeProject(GenreType::Trap, substyle, seed, 4, 140.0f);
            TrapEngine engine;
            engine.generate(project);
            for (const auto& track : project.tracks)
                dumpTrack("trap", substyle, seed, track);
        }

        for (int substyle = 0; substyle < 3; ++substyle)
        {
            auto project = makeProject(GenreType::Drill, substyle, seed, 4, 140.0f);
            DrillEngine engine;
            engine.generate(project);
            for (const auto& track : project.tracks)
                dumpTrack("drill", substyle, seed, track);
        }
    }

    return 0;
}
