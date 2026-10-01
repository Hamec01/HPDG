#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "SampleLibraryManager.h"

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
    };

    // A fully decoded sample set, built off the audio/project lock (disk I/O lives here).
    struct PreparedLibrary
    {
        std::array<LaneState, 11> states;
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> retiredBuffers;
    };

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
    const juce::AudioBuffer<float>* getSelectedBuffer(TrackType track) const;
    std::shared_ptr<const juce::AudioBuffer<float>> getSelectedBufferShared(TrackType track) const;
    bool hasSamples(TrackType track) const;
    bool hasSamplesMatchingAnyTag(TrackType track, const std::vector<juce::String>& preferredTags) const;

private:
    static bool loadWavToBuffer(const juce::File& file,
                                juce::AudioFormatManager& formatManager,
                                std::shared_ptr<juce::AudioBuffer<float>>& outBuffer);

    std::array<LaneState, 11> states;
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> retiredBuffers;
    juce::AudioFormatManager formatManager;
};
} // namespace bbg
