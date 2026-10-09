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

    const auto haystack = info.name + " " + info.sourceName + " " + info.file.getFullPathName();
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

void LaneSampleBank::updateSelectedRoot(LaneState& state)
{
    const auto& selected = state.infos[static_cast<size_t>(state.selectedIndex)];
    const int namePitchClass = rootPitchClassFromNameOrNone(selected.sourceName.isNotEmpty() ? selected.sourceName : selected.name);
    const auto sound = static_cast<size_t>(state.selectedIndex) < state.roots.size() ? state.roots[static_cast<size_t>(state.selectedIndex)] : SampleRoot {};
    const auto root = resolveRoot(namePitchClass, sound);
    state.selectedRootPitchClass = root.pitchClass;
    state.selectedRootCents = root.cents;
    state.selectedRootMidi = root.midi;
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
        state.sampleRates.assign(state.infos.size(), 44100.0);

        for (size_t i = 0; i < state.infos.size(); ++i)
            loadWavToBuffer(state.infos[i].file, localFormatManager, state.buffers[i], state.sampleRates[i]);

        // Bass / 808 one-shots: the note each one is tuned to, from its sound (off the audio /
        // project lock, once per load).
        state.roots.assign(state.infos.size(), SampleRoot {});
        if (track == TrackType::Sub808)
            for (size_t i = 0; i < state.infos.size(); ++i)
                if (state.buffers[i] != nullptr)
                    state.roots[i] = SampleRootDetector::detect(*state.buffers[i], state.sampleRates[i]);
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
        state.sampleRates = std::move(incoming.sampleRates);
        state.roots = std::move(incoming.roots);

        if (state.infos.empty())
        {
            state.selectedIndex = 0;
            state.selectedName = "(empty)";
            state.selectedRootPitchClass = 0;
            state.selectedRootCents = 0.0;
            state.selectedRootMidi = -1;
            continue;
        }

        state.selectedIndex = juce::jlimit(0, static_cast<int>(state.infos.size()) - 1, previousSelection);
        state.selectedName = state.infos[static_cast<size_t>(state.selectedIndex)].name;
        updateSelectedRoot(state);
    }
}

bool LaneSampleBank::selectIndex(TrackType track, int index)
{
    auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    if (state.infos.empty())
    {
        state.selectedIndex = 0;
        state.selectedName = "(empty)";
        state.selectedRootPitchClass = 0;
        state.selectedRootCents = 0.0;
        state.selectedRootMidi = -1;
        return false;
    }

    state.selectedIndex = juce::jlimit(0, static_cast<int>(state.infos.size()) - 1, index);
    state.selectedName = state.infos[static_cast<size_t>(state.selectedIndex)].name;
    updateSelectedRoot(state);
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

int LaneSampleBank::rootPitchClassFromName(const juce::String& name)
{
    return std::max(0, rootPitchClassFromNameOrNone(name));
}

int LaneSampleBank::rootPitchClassFromNameOrNone(const juce::String& name)
{
    const auto base = juce::File::createFileWithoutCheckingPath(name).getFileNameWithoutExtension().trim();
    const auto token = base.fromLastOccurrenceOf("-", false, false).trim().toUpperCase();
    if (token.isEmpty() || token.length() > 4)
        return -1;

    static constexpr int letterPitch[] { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const auto letter = token[0];
    if (letter < 'A' || letter > 'G')
        return -1;

    int pitchClass = letterPitch[letter - 'A'];
    int index = 1;
    if (index < token.length() && (token[index] == '#' || token[index] == 'S'))
    {
        ++pitchClass;
        ++index;
    }
    else if (index < token.length() && token[index] == 'B')
    {
        --pitchClass;
        ++index;
    }
    // Whatever follows may only be an octave number ("C1", "F#2").
    for (; index < token.length(); ++index)
        if (!juce::CharacterFunctions::isDigit(token[index]))
            return -1;

    return (pitchClass % 12 + 12) % 12;
}

LaneSampleBank::ResolvedRoot LaneSampleBank::resolveRoot(int namePitchClass, const SampleRoot& sound)
{
    ResolvedRoot root;
    const bool soundClear = sound.valid && sound.confidence >= 0.5f;
    if (namePitchClass >= 0)
    {
        root.pitchClass = namePitchClass;
        if (sound.valid)
        {
            // The octave of the name's note nearest the sound, and how far the sound sits from it.
            const double offset = std::remainder(sound.midi - static_cast<double>(namePitchClass), 12.0);
            if (std::abs(offset) <= 0.75)
            {
                root.cents = 100.0 * offset;
                root.midi = static_cast<int>(std::lround(sound.midi - offset));
                return root;
            }
            if (soundClear) // a mislabelled sample: trust what it sounds like
            {
                root.pitchClass = sound.pitchClass();
                root.cents = sound.cents();
                root.midi = sound.nearestNote();
            }
        }
        return root;
    }
    if (soundClear)
    {
        root.pitchClass = sound.pitchClass();
        root.cents = sound.cents();
        root.midi = sound.nearestNote();
    }
    return root;
}

int LaneSampleBank::getSelectedRootPitchClass(TrackType track) const
{
    // Cached on selection: this is read on the audio thread, so no string work here.
    return states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))].selectedRootPitchClass;
}

double LaneSampleBank::getSelectedRootCents(TrackType track) const
{
    return states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))].selectedRootCents;
}

int LaneSampleBank::getSelectedRootMidi(TrackType track) const
{
    return states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))].selectedRootMidi;
}

double LaneSampleBank::getSelectedSampleRate(TrackType track) const
{
    const auto& state = states[static_cast<size_t>(SampleLibraryManager::trackIndex(track))];
    if (state.sampleRates.empty())
        return 44100.0;
    const int index = juce::jlimit(0, static_cast<int>(state.sampleRates.size()) - 1, state.selectedIndex);
    return state.sampleRates[static_cast<size_t>(index)];
}

bool LaneSampleBank::loadWavToBuffer(const juce::File& file,
                                     juce::AudioFormatManager& formatManager,
                                     std::shared_ptr<juce::AudioBuffer<float>>& outBuffer,
                                     double& outSampleRate)
{
    outBuffer.reset();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    buffer->clear();
    if (!reader->read(buffer.get(), 0, static_cast<int>(reader->lengthInSamples), 0, true, true))
        return false;

    outSampleRate = reader->sampleRate;
    outBuffer = std::move(buffer);
    return true;
}
} // namespace bbg
