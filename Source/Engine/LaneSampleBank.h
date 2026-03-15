#pragma once

#include <array>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "SampleLibraryManager.h"

namespace bbg
{
class LaneSampleBank
{
public:
    LaneSampleBank();

    void applyLibrary(const SampleLibraryManager& library);

    bool selectIndex(TrackType track, int index);
    bool selectNext(TrackType track);
    bool selectPrevious(TrackType track);

    int getSelectedIndex(TrackType track) const;
    juce::String getSelectedName(TrackType track) const;
    const juce::AudioBuffer<float>* getSelectedBuffer(TrackType track) const;
    bool hasSamples(TrackType track) const;

private:
    struct LaneState
    {
        std::vector<LaneSampleInfo> infos;
        std::vector<std::shared_ptr<juce::AudioBuffer<float>>> buffers;
        int selectedIndex = 0;
        juce::String selectedName;
    };

    static bool loadWavToBuffer(const juce::File& file,
                                juce::AudioFormatManager& formatManager,
                                std::shared_ptr<juce::AudioBuffer<float>>& outBuffer);

    std::array<LaneState, 11> states;
    juce::AudioFormatManager formatManager;
};
} // namespace bbg
