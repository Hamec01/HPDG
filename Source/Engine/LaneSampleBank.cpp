#include "LaneSampleBank.h"

#include <algorithm>

namespace bbg
{
namespace
{
bool sampleInfoMatchesAnyTag(const LaneSampleInfo& info, const std::vector<juce::String>& preferredTags)
{
    if (preferredTags.empty())
        return false;

    const auto haystack = info.name + " " + info.file.getFullPathName();
    return std::any_of(preferredTags.begin(), preferredTags.end(), [&haystack](const juce::String& tag)
    {
        return tag.isNotEmpty() && haystack.containsIgnoreCase(tag);
    });
}
} // namespace

LaneSampleBank::LaneSampleBank()
{
    formatManager.registerBasicFormats();
}

void LaneSampleBank::applyLibrary(const SampleLibraryManager& library)
{
    adoptPreparedLibrary(prepareLibrary(library));
}

std::unique_ptr<LaneSampleBank::PreparedLibrary> LaneSampleBank::prepareLibrary(const SampleLibraryManager& library)
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

    juce::AudioFormatManager localFormatManager;
    localFormatManager.registerBasicFormats();

    auto prepared = std::make_unique<PreparedLibrary>();
    for (const auto track : tracks)
    {
        auto& state = prepared->states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
        state.infos = library.getSamples(track);
        state.buffers.resize(state.infos.size());

        for (size_t i = 0; i < state.infos.size(); ++i)
            loadWavToBuffer(state.infos[i].file, localFormatManager, state.buffers[i]);
    }

    return prepared;
}

void LaneSampleBank::adoptPreparedLibrary(std::unique_ptr<PreparedLibrary> prepared)
{
    if (prepared == nullptr)
        return;

    // Release the buffers retired by the previous swap (this runs on the message thread), then
    // retire the current ones: voices still playing them hold their own references, and this
    // list guarantees the last reference is not dropped on the audio thread.
    retiredBuffers.clear();

    for (size_t laneIndex = 0; laneIndex < states.size(); ++laneIndex)
    {
        auto& state = states[laneIndex];
        for (auto& buffer : state.buffers)
            if (buffer != nullptr)
                retiredBuffers.push_back(std::move(buffer));

        auto& incoming = prepared->states[laneIndex];
        const int previousSelection = state.selectedIndex;
        state.infos = std::move(incoming.infos);
        state.buffers = std::move(incoming.buffers);

        if (state.infos.empty())
        {
            state.selectedIndex = 0;
            state.selectedName = "(empty)";
            continue;
        }

        state.selectedIndex = juce::jlimit(0, static_cast<int>(state.infos.size()) - 1, previousSelection);
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

bool LaneSampleBank::selectNextMatchingAnyTag(TrackType track, const std::vector<juce::String>& preferredTags)
{
    auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    if (state.infos.empty())
        return false;

    if (preferredTags.empty())
        return selectNext(track);

    std::vector<int> matchingIndices;
    matchingIndices.reserve(state.infos.size());

    for (int i = 0; i < static_cast<int>(state.infos.size()); ++i)
    {
        if (sampleInfoMatchesAnyTag(state.infos[static_cast<size_t>(i)], preferredTags))
            matchingIndices.push_back(i);
    }

    if (matchingIndices.empty())
        return selectNext(track);

    int next = matchingIndices.front();
    for (const int index : matchingIndices)
    {
        if (index > state.selectedIndex)
        {
            next = index;
            break;
        }
    }

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

std::shared_ptr<const juce::AudioBuffer<float>> LaneSampleBank::getSelectedBufferShared(TrackType track) const
{
    const auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    if (state.buffers.empty())
        return {};

    const int index = juce::jlimit(0, static_cast<int>(state.buffers.size()) - 1, state.selectedIndex);
    return state.buffers[static_cast<size_t>(index)];
}

bool LaneSampleBank::hasSamples(TrackType track) const
{
    const auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    return !state.infos.empty();
}

bool LaneSampleBank::hasSamplesMatchingAnyTag(TrackType track, const std::vector<juce::String>& preferredTags) const
{
    const auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    return std::any_of(state.infos.begin(), state.infos.end(), [&preferredTags](const LaneSampleInfo& info)
    {
        return sampleInfoMatchesAnyTag(info, preferredTags);
    });
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
