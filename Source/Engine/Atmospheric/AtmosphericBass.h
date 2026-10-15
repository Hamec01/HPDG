#pragma once

#include <array>
#include <vector>

#include "AtmosphericTypes.h"
#include "../../Core/NoteEvent.h"

namespace bbg
{
// Atmospheric bass (ATMOS_ENGINE_SPEC.md, section 4): a one-bar motif per substyle (Pulse: between
// the four-on-the-floor kicks; Two-Step: a held sub with a late answer; Broken: a melodic line in
// thirds), each note a harmonic role (root, octave, fifth, third, approach) against the bar's root
// from a 4-bar minor progression, repeated over the phrase. Candidates (motif x progression) are
// scored on the passport traits; Pulse notes end before the next kick.
struct AtmosBassParams
{
    int seed = 1;
    int bars = 4;
    int substyle = 0;
    int keyRoot = 0;
    int scaleMode = 0;  // 0 minor, 1 major, 2 harmonic minor
    int amount = 1;     // lane buttons [1][2][3] = 0, 1, 2
    float density = 0.5f;
    int candidateCount = 32;
    // Every motif is a playable groove: the passport fit only leans the choice (a wide pool), so
    // Generate presses give different lines.
    float nearBestTolerance = 0.50f;
    float temperature = 0.25f;
};

struct AtmosBassNote
{
    int step = 0; // absolute 16th (bar * 16 + step)
    int lengthSteps = 1;
    int pitch = 34;
    int velocity = 100;
};

struct AtmosBassTraits
{
    float notesPerBar = 0.0f;
    float pitchChangesPerBar = 0.0f;
    float medianBeats = 0.0f;
    float longShare = 0.0f;
    std::array<float, 5> intervals {};    // repeat, step, third, fourth / fifth, octave
    std::array<float, 4> kickRelation {}; // on, after, before, away
};

struct AtmosBassLine
{
    std::vector<AtmosBassNote> notes;
    std::vector<int> barRoots; // pitch class per bar
    AtmosBassTraits traits;
    float quality = 0.0f;
    bool passedGates = false;
    juce::String failedGate;
};

class AtmosphericBassGenerator
{
public:
    // kickSteps: the kick's 16th positions per bar (the drums the bass plays against).
    static AtmosBassLine search(const AtmosBassParams& params, const std::vector<std::vector<int>>& kickSteps, juce::String* report = nullptr);
    static AtmosBassTraits measure(const std::vector<AtmosBassNote>& notes, int bars, const std::vector<std::vector<int>>& kickSteps);
    static std::vector<NoteEvent> toNotes(const AtmosBassLine& line);
};
} // namespace bbg
