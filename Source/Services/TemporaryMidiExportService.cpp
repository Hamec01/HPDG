#include "TemporaryMidiExportService.h"

#include <atomic>

#include "../Engine/MidiExportEngine.h"

namespace bbg
{
namespace
{
juce::File dragLogFile()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("DRUMENGINE")
        .getChildFile("Logs")
        .getChildFile("drag.log");
}

void logDrag(const juce::String& message)
{
    auto file = dragLogFile();
    file.getParentDirectory().createDirectory();
    const auto line = juce::Time::getCurrentTime().toString(true, true) + " | SERVICE | " + message + "\n";
    file.appendText(line, false, false, "\n");
}
}

juce::File TemporaryMidiExportService::createTempFileForPattern(const PatternProject& project)
{
    return exportTemp(project, std::nullopt, "BoomBapPattern");
}

juce::File TemporaryMidiExportService::createTempFileForTrack(const PatternProject& project, TrackType trackType)
{
    return exportTemp(project, trackType, "BoomBapTrack");
}

juce::File TemporaryMidiExportService::createTempFile(const juce::String& prefix)
{
    static std::atomic<unsigned int> counter { 0u };

    const auto baseDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("DRUMENGINE")
        .getChildFile("TempMidi");
    baseDir.createDirectory();

    const auto stamp = juce::Time::getCurrentTime().toMilliseconds();
    const auto serial = counter.fetch_add(1u, std::memory_order_relaxed);
    const auto name = prefix + "_" + juce::String(stamp) + "_" + juce::String(static_cast<int>(serial));
    return baseDir.getNonexistentChildFile(name, ".mid", false);
}

juce::File TemporaryMidiExportService::exportTemp(const PatternProject& project,
                                                  std::optional<TrackType> onlyTrack,
                                                  const juce::String& prefix)
{
    try
    {
        logDrag("exportTemp pre-createTempFile prefix=" + prefix);
        const auto file = createTempFile(prefix);
        logDrag("exportTemp post-createTempFile prefix=" + prefix + " file=" + file.getFullPathName());

        const bool ok = onlyTrack.has_value()
            ? MidiExportEngine::saveMidiFile(project, file, onlyTrack, 960, false, false)
            : MidiExportEngine::saveMultiTrackMidiFile(project, file, 960);

        logDrag("exportTemp done ok=" + juce::String(ok ? "1" : "0") + " fileExists=" + juce::String(file.existsAsFile() ? "1" : "0"));
        return ok ? file : juce::File();
    }
    catch (...)
    {
        logDrag("exportTemp exception prefix=" + prefix);
        return juce::File();
    }
}
} // namespace bbg
