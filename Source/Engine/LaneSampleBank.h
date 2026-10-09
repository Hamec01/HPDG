#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "SampleLibraryManager.h"
#include "../Analysis/SampleRootDetector.h"

namespace bbg
{
class LaneSampleBank
{
public:
    struct LaneState
    {
        std::vector<LaneSampleInfo> infos;
        std::vector<std::shared_ptr<juce::AudioBuffer<float>>> buffers;
        int selectedIndex = 0;
        juce::String selectedName;
        int selectedRootPitchClass = 0;
        double selectedRootCents = 0.0;   // the selected sample's offset from that note (from its sound)
        int selectedRootMidi = -1;        // its settled note with octave, -1 when unknown (display only)
        std::vector<SampleRoot> roots;    // per file, from the sound (bass / 808 lane only)
        std::vector<double> sampleRates; // each file's own rate (no resampling on load)
    };

    // A fully decoded sample set, built off the audio/project lock (disk I/O lives here).
    struct PreparedLibrary
    {
        std::array<LaneState, 11> states;
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> retiredBuffers;
    };

    // Pitch class of a melodic one-shot, read from the "<name> - <note>" convention used by the
    // bass / 808 kits (e.g. "BoomBap Bass - Puma - C", "Kit 808 - F#"). 0 (C) when absent.
    static int rootPitchClassFromName(const juce::String& name);
    // Same, -1 when the name states no note.
    static int rootPitchClassFromNameOrNone(const juce::String& name);
    // The note a melodic one-shot is tuned to: its name and its sound together
    // (docs/audit/SAMPLE_ANALYSIS_STAGE.md, step 9). The sound decides octave and cents; the name's
    // pitch class is kept when the sound lands within 75 cents of it, the sound wins when it
    // clearly says otherwise (mislabelled samples), the name alone is used when the sound is unclear.
    struct ResolvedRoot
    {
        int pitchClass = 0;
        double cents = 0.0;
        int midi = -1;
    };
    static ResolvedRoot resolveRoot(int namePitchClass, const SampleRoot& sound);

    LaneSampleBank();

    // Blocking: scans + decodes every WAV. Equivalent to adoptPreparedLibrary(prepareLibrary(...)).
    void applyLibrary(const SampleLibraryManager& library);

    // Heavy, thread-agnostic, touches no bank state: call it WITHOUT holding the project lock.
    static std::unique_ptr<PreparedLibrary> prepareLibrary(const SampleLibraryManager& library);

    // Cheap swap (no disk I/O): call it under the project lock. Keeps the previous buffers alive
    // until the next swap so voices still ringing out on the audio thread never free them.
    void adoptPreparedLibrary(std::unique_ptr<PreparedLibrary> prepared);

    bool selectIndex(TrackType track, int index);
    bool selectNext(TrackType track);
    bool selectNextMatchingAnyTag(TrackType track, const std::vector<juce::String>& preferredTags);
    bool selectPrevious(TrackType track);

    int getSelectedIndex(TrackType track) const;
    juce::String getSelectedName(TrackType track) const;
    int getSelectedRootPitchClass(TrackType track) const;
    double getSelectedRootCents(TrackType track) const;
    int getSelectedRootMidi(TrackType track) const;
    // Rate the selected file was recorded at; the player scales its read speed by
    // fileRate / deviceRate so pitch and length are right for 44.1 / 48 / 96 / 192 kHz files.
    double getSelectedSampleRate(TrackType track) const;
    const juce::AudioBuffer<float>* getSelectedBuffer(TrackType track) const;
    std::shared_ptr<const juce::AudioBuffer<float>> getSelectedBufferShared(TrackType track) const;
    bool hasSamples(TrackType track) const;
    bool hasSamplesMatchingAnyTag(TrackType track, const std::vector<juce::String>& preferredTags) const;

private:
    static void updateSelectedRoot(LaneState& state);
    static bool loadWavToBuffer(const juce::File& file,
                                juce::AudioFormatManager& formatManager,
                                std::shared_ptr<juce::AudioBuffer<float>>& outBuffer,
                                double& outSampleRate);

    std::array<LaneState, 11> states;
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> retiredBuffers;
    juce::AudioFormatManager formatManager;
};
} // namespace bbg
