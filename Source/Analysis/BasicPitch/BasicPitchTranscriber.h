#pragma once

#include <vector>

namespace bbg
{
// Polyphonic audio -> notes: a C++ port of Spotify's Basic Pitch (Bittner et al., ICASSP 2022,
// Apache-2.0; docs/THIRD-PARTY-NOTICES.txt) with no runtime dependency. Same windows, CQT, network
// and note decoding as basic_pitch.inference.predict with its defaults; checked against the Python
// outputs by Tests/BasicPitchLab.cpp.
struct BasicPitchFrames
{
    static constexpr int kNotes = 88;     // MIDI 21 (A0) .. 108
    static constexpr int kContours = 264; // 3 bins per semitone
    static constexpr double kFramesPerSecond = 22050.0 / 256.0;

    int frames = 0;
    std::vector<float> note;    // frames x 88: a note sounds
    std::vector<float> onset;   // frames x 88: a note starts
    std::vector<float> contour; // frames x 264: pitch salience

    float noteAt(int frame, int pitchIndex) const { return note[static_cast<size_t>(frame * kNotes + pitchIndex)]; }
};

struct BasicPitchNote
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int midi = 0;
    float amplitude = 0.0f; // mean note activation over the note, 0..1
};

struct BasicPitchSettings
{
    float onsetThreshold = 0.5f;
    float frameThreshold = 0.3f;
    double minimumNoteMs = 127.70;
    bool inferOnsets = true;
    bool melodiaTrick = true;
};

// A key read from transcribed notes (tools/transcription_key.py arm B2): pitch-class histogram
// (duration x amplitude) against the Albrecht-Shanahan profiles SampleHarmonyAnalyzer decides with,
// minor prior 0.1, + bassWeight x the share of the bass (lowest sounding note) on the root,
// + openingWeight on the first bass note below C4. 380 tonal loops: MIREX 0.690 / exact 62.6 %
// (the plugin's own key 0.652 / 56.3 %).
struct NoteKey
{
    bool valid = false;
    int root = 0;      // pitch class, 0 = C
    int mode = 0;      // 0 minor, 1 major (GeneratorParams::scaleMode)
    float score = 0.0f;  // best score (correlation + bonuses + minor prior)
    float margin = 0.0f; // best score - second best
    bool fifthFitsBetter = false; // a key a fifth away (same mode) correlates better with the notes
};

NoteKey estimateKeyFromNotes(const std::vector<BasicPitchNote>& notes, float bassWeight = 0.2f, float openingWeight = 0.5f);

class BasicPitchTranscriber
{
public:
    static constexpr double kSampleRate = 22050.0;

    // audio: mono at 22.05 kHz (resampleTo22050 for other rates). threads > 1 runs the 2 s windows in parallel.
    static BasicPitchFrames infer(const float* audio, int numSamples, int threads = 1);
    static std::vector<BasicPitchNote> notes(const BasicPitchFrames& frames, const BasicPitchSettings& settings = {});

    static std::vector<float> resampleTo22050(const float* mono, int numSamples, double sampleRate);
};
} // namespace bbg
