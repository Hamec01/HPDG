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
};

class SampleLibraryManager
{
public:
    void setRootDirectory(const juce::File& root);
    void setGenre(GenreType genre);
    void scan();

    const std::vector<LaneSampleInfo>& getSamples(TrackType track) const;
    juce::File getResolvedRootDirectory() const { return resolvedRootDirectory; }

    static juce::String folderNameForTrack(TrackType track);
    static juce::String folderNameForGenre(GenreType genre);
    static int trackIndex(TrackType track);

private:
    juce::File chooseExistingRoot() const;

    juce::File configuredRootDirectory;
    juce::File resolvedRootDirectory;
    GenreType currentGenre = GenreType::BoomBap;
    std::array<std::vector<LaneSampleInfo>, 11> laneSamples;
};
} // namespace bbg
