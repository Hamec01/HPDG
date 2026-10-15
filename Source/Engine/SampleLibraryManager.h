#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

#include "../Core/GeneratorParams.h"
#include "../Core/TrackType.h"

namespace bbg
{
struct LaneSampleInfo
{
    juce::File file;
    juce::String name;
    // Original filename metadata survives compact display/file renaming.
    juce::String sourceName;
};

class SampleLibraryManager
{
public:
    void setRootDirectory(const juce::File& root);
    void setGenre(GenreType genre);
    void scan();

    const std::vector<LaneSampleInfo>& getSamples(TrackType track) const;
    juce::File getResolvedRootDirectory() const { return resolvedRootDirectory; }
    // HPDG's synthesized sub basses (Samples/Synth/Sub808, tools/make_synth_sub.py) follow every
    // genre's own 808s in the Sub808 list (appended: saved sample indices stay valid). Index of the
    // first one, -1 when they are missing.
    int firstSynthSubIndex() const { return synthSubStart; }

    static juce::String folderNameForTrack(TrackType track);
    static juce::String folderNameForGenre(GenreType genre);
    static int trackIndex(TrackType track);

private:
    juce::File chooseExistingRoot() const;

    juce::File configuredRootDirectory;
    juce::File resolvedRootDirectory;
    GenreType currentGenre = GenreType::BoomBap;
    std::array<std::vector<LaneSampleInfo>, 11> laneSamples;
    int synthSubStart = -1;
};
} // namespace bbg
