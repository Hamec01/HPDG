#include "LaneSampleBank.h"

#include <algorithm>

namespace bbg
{
LaneSampleBank::LaneSampleBank()
{
    formatManager.registerBasicFormats();
}

void LaneSampleBank::applyLibrary(const SampleLibraryManager& library)
{
    const std::array<TrackType, 11> tracks {
        TrackType::HiHat,
        TrackType::HatFX,
        TrackType::OpenHat,
        TrackType::Snare,
        TrackType::ClapGhostSnare,
        TrackType::Kick,
        TrackType::GhostKick,
        TrackType::Sub808,
        TrackType::Ride,
        TrackType::Cymbal,
        TrackType::Perc
    };

    for (const auto track : tracks)
    {
        auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
        state.infos = library.getSamples(track);
        state.buffers.clear();
        state.buffers.resize(state.infos.size());

        for (size_t i = 0; i < state.infos.size(); ++i)
            loadWavToBuffer(state.infos[i].file, formatManager, state.buffers[i]);

        if (state.infos.empty())
        {
            state.selectedIndex = 0;
            state.selectedName = "(empty)";
            continue;
        }

        state.selectedIndex = juce::jlimit(0, static_cast<int>(state.infos.size()) - 1, state.selectedIndex);
        state.selectedName = state.infos[static_cast<size_t>(state.selectedIndex)].name;
    }
}

bool LaneSampleBank::selectIndex(TrackType track, int index)
{
    auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    if (state.infos.empty())
    {
        state.selectedIndex = 0;
        state.selectedName = "(empty)";
        return false;
    }

    state.selectedIndex = juce::jlimit(0, static_cast<int>(state.infos.size()) - 1, index);
    state.selectedName = state.infos[static_cast<size_t>(state.selectedIndex)].name;
    return true;
}

bool LaneSampleBank::selectNext(TrackType track)
{
    auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    if (state.infos.empty())
        return false;

    const int next = (state.selectedIndex + 1) % static_cast<int>(state.infos.size());
    return selectIndex(track, next);
}

bool LaneSampleBank::selectPrevious(TrackType track)
{
    auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    if (state.infos.empty())
        return false;

    int prev = state.selectedIndex - 1;
    if (prev < 0)
        prev = static_cast<int>(state.infos.size()) - 1;

    return selectIndex(track, prev);
}

int LaneSampleBank::getSelectedIndex(TrackType track) const
{
    const auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    return state.selectedIndex;
}

juce::String LaneSampleBank::getSelectedName(TrackType track) const
{
    const auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    if (state.infos.empty())
        return "(empty)";

    return state.selectedName;
}

const juce::AudioBuffer<float>* LaneSampleBank::getSelectedBuffer(TrackType track) const
{
    const auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    if (state.buffers.empty())
        return nullptr;

    const int index = juce::jlimit(0, static_cast<int>(state.buffers.size()) - 1, state.selectedIndex);
    const auto& ptr = state.buffers[static_cast<size_t>(index)];
    return ptr != nullptr ? ptr.get() : nullptr;
}

bool LaneSampleBank::hasSamples(TrackType track) const
{
    const auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    return !state.infos.empty();
}

bool LaneSampleBank::loadWavToBuffer(const juce::File& file,
                                     juce::AudioFormatManager& formatManager,
                                     std::shared_ptr<juce::AudioBuffer<float>>& outBuffer)
{
    outBuffer.reset();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return false;

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    buffer->clear();
    if (!reader->read(buffer.get(), 0, static_cast<int>(reader->lengthInSamples), 0, true, true))
        return false;

    outBuffer = std::move(buffer);
    return true;
}
} // namespace bbg
