#include "SampleLibraryManager.h"

#include <algorithm>

namespace bbg
{
void SampleLibraryManager::setRootDirectory(const juce::File& root)
{
    configuredRootDirectory = root;
}

void SampleLibraryManager::setGenre(GenreType genre)
{
    currentGenre = genre;
}

void SampleLibraryManager::scan()
{
    for (auto& lane : laneSamples)
        lane.clear();

    resolvedRootDirectory = chooseExistingRoot();

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
        const auto genreRoot = resolvedRootDirectory.getChildFile(folderNameForGenre(currentGenre));
        auto folder = genreRoot.exists() && genreRoot.isDirectory()
            ? genreRoot.getChildFile(folderNameForTrack(track))
            : resolvedRootDirectory.getChildFile(folderNameForTrack(track));

        if ((!folder.exists() || !folder.isDirectory()) && (genreRoot.exists() && genreRoot.isDirectory()))
            folder = resolvedRootDirectory.getChildFile(folderNameForTrack(track));

        if (!folder.exists() || !folder.isDirectory())
            continue;

        auto files = folder.findChildFiles(juce::File::findFiles, false, "*.wav");
        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension()) < 0;
        });

        auto& out = laneSamples[static_cast<size_t>(trackIndex(track))];
        out.reserve(files.size());

        for (const auto& file : files)
            out.push_back({ file, file.getFileNameWithoutExtension() });
    }
}

const std::vector<LaneSampleInfo>& SampleLibraryManager::getSamples(TrackType track) const
{
    return laneSamples[static_cast<size_t>(trackIndex(track))];
}

juce::String SampleLibraryManager::folderNameForTrack(TrackType track)
{
    switch (track)
    {
        case TrackType::Kick: return "Kick";
        case TrackType::HiHat: return "HiHat";
        case TrackType::HatFX: return "HatFX";
        case TrackType::OpenHat: return "OpenHat";
        case TrackType::Snare: return "Snare";
        case TrackType::ClapGhostSnare: return "ClapGhost";
        case TrackType::GhostKick: return "GhostKick";
        case TrackType::Sub808: return "Sub808";
        case TrackType::Ride: return "Ride";
        case TrackType::Cymbal: return "Cymbal";
        case TrackType::Perc: return "Perc";
        default: return "Misc";
    }
}

juce::String SampleLibraryManager::folderNameForGenre(GenreType genre)
{
    switch (genre)
    {
        case GenreType::BoomBap: return "BoomBap";
        case GenreType::Rap: return "Rap";
        case GenreType::Trap: return "Trap";
        case GenreType::Drill: return "Drill";
        default: return "BoomBap";
    }
}

int SampleLibraryManager::trackIndex(TrackType track)
{
    switch (track)
    {
        case TrackType::HiHat: return 0;
        case TrackType::HatFX: return 1;
        case TrackType::OpenHat: return 2;
        case TrackType::Snare: return 3;
        case TrackType::ClapGhostSnare: return 4;
        case TrackType::Kick: return 5;
        case TrackType::GhostKick: return 6;
        case TrackType::Sub808: return 7;
        case TrackType::Ride: return 8;
        case TrackType::Cymbal: return 9;
        case TrackType::Perc: return 10;
        default: return 0;
    }
}

juce::File SampleLibraryManager::chooseExistingRoot() const
{
    if (configuredRootDirectory.exists() && configuredRootDirectory.isDirectory())
        return configuredRootDirectory;

    const auto cwdRoot = juce::File::getCurrentWorkingDirectory().getChildFile("Samples");
    if (cwdRoot.exists() && cwdRoot.isDirectory())
        return cwdRoot;

    const auto exeRoot = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                             .getParentDirectory()
                             .getChildFile("Samples");
    if (exeRoot.exists() && exeRoot.isDirectory())
        return exeRoot;

    const auto docsRoot = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                              .getChildFile("DRUMENGINE")
                              .getChildFile("Samples");
    if (docsRoot.exists() && docsRoot.isDirectory())
        return docsRoot;

    return cwdRoot;
}
} // namespace bbg
