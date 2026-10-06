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
        auto genreRoot = resolvedRootDirectory.getChildFile(folderNameForGenre(currentGenre));
        auto laneFolderName = folderNameForTrack(track);
        // Drum & Bass uses Samples/DnB once it exists; until then (and per missing lane) the Boom Bap kit.
        if (currentGenre == GenreType::DnB)
        {
            const auto own = genreRoot.getChildFile(folderNameForTrack(track));
            if (!own.isDirectory() || own.findChildFiles(juce::File::findFiles, false, "*.wav").isEmpty())
                genreRoot = resolvedRootDirectory.getChildFile("BoomBap");
        }
        // Techno uses Samples/Techno once it exists; until then (and per missing lane) the
        // electronic DnB kit, then the Boom Bap kit.
        if (currentGenre == GenreType::Techno)
        {
            auto hasWavs = [&](const juce::File& genreFolder)
            {
                const auto own = genreFolder.getChildFile(laneFolderName);
                return own.isDirectory() && !own.findChildFiles(juce::File::findFiles, false, "*.wav").isEmpty();
            };
            // The Techno grammar writes its clap/backbeat to Snare. Use the own
            // clap kit before falling back to another genre's snare samples.
            if (!hasWavs(genreRoot) && track == TrackType::Snare)
            {
                laneFolderName = "ClapGhost";
                if (!hasWavs(genreRoot))
                    laneFolderName = folderNameForTrack(track);
            }
            // Ghost kicks are the same drum voiced at a lower velocity.
            if (!hasWavs(genreRoot) && track == TrackType::GhostKick)
            {
                laneFolderName = "Kick";
                if (!hasWavs(genreRoot))
                    laneFolderName = folderNameForTrack(track);
            }
            if (!hasWavs(genreRoot))
                genreRoot = hasWavs(resolvedRootDirectory.getChildFile("DnB")) ? resolvedRootDirectory.getChildFile("DnB")
                                                                               : resolvedRootDirectory.getChildFile("BoomBap");
        }
        auto folder = genreRoot.exists() && genreRoot.isDirectory()
            ? genreRoot.getChildFile(laneFolderName)
            : resolvedRootDirectory.getChildFile(folderNameForTrack(track));

        if ((!folder.exists() || !folder.isDirectory()) && (genreRoot.exists() && genreRoot.isDirectory()))
            folder = resolvedRootDirectory.getChildFile(folderNameForTrack(track));

        if (!folder.exists() || !folder.isDirectory())
            continue;

        auto files = folder.findChildFiles(juce::File::findFiles, false, "*.wav");
        const auto aliases = juce::JSON::parse(folder.getChildFile("sample-names.json").loadFileAsString());
        const auto sourceNameFor = [&aliases](const juce::File& file)
        {
            if (const auto* names = aliases.getDynamicObject())
            {
                const auto original = names->getProperty(juce::Identifier(file.getFileName())).toString();
                if (original.isNotEmpty())
                    return original;
            }
            return file.getFileNameWithoutExtension();
        };
        // Preserve sample indices saved in existing projects, even after renaming.
        std::sort(files.begin(), files.end(), [&sourceNameFor](const juce::File& a, const juce::File& b)
        {
            return sourceNameFor(a).compareIgnoreCase(sourceNameFor(b)) < 0;
        });

        auto& out = laneSamples[static_cast<size_t>(trackIndex(track))];
        out.reserve(files.size());

        for (const auto& file : files)
            out.push_back({ file, file.getFileNameWithoutExtension(), sourceNameFor(file) });
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
        // While Rap / Drill are parked, their kits are never loaded.
        case GenreType::Rap: return kShowRapAndDrillGenres ? "Rap" : "BoomBap";
        case GenreType::Trap: return "Trap";
        case GenreType::Drill: return kShowRapAndDrillGenres ? "Drill" : "Trap";
        case GenreType::DnB: return "DnB";
        case GenreType::Techno: return "Techno";
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
