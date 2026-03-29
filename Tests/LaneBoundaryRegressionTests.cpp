#include <functional>
#include <iostream>
#include <stdexcept>

#include <juce_events/juce_events.h>

#include "../Source/Core/RuntimeLaneLifecycle.h"
#include "../Source/Plugin/PluginProcessor.h"

namespace bbg
{
namespace
{
[[noreturn]] void fail(const juce::String& message)
{
    throw std::runtime_error(message.toStdString());
}

void expect(bool condition, const juce::String& message)
{
    if (!condition)
        fail(message);
}

RuntimeLaneId requireBackedLaneId(const PatternProject& project, TrackType type)
{
    const auto* lane = findRuntimeLaneForTrack(project.runtimeLaneProfile, type);
    expect(lane != nullptr, "Expected backed runtime lane for requested track.");
    return lane->laneId;
}

juce::File makeTestOutputFile(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("HPDG_LaneBoundaryTests");
    dir.createDirectory();
    auto file = dir.getChildFile(name);
    if (file.existsAsFile())
        file.deleteFile();
    return file;
}

void cleanupFile(const juce::File& file)
{
    if (file.existsAsFile())
        file.deleteFile();
}

void testLaneAwareExportTrackPath()
{
    BoomBapGeneratorAudioProcessor processor;
    const auto project = processor.getProjectSnapshot();
    const auto kickLaneId = requireBackedLaneId(project, TrackType::Kick);

    const auto trackFile = makeTestOutputFile("kick-tracktype.mid");
    const auto laneFile = makeTestOutputFile("kick-laneid.mid");
    const auto invalidFile = makeTestOutputFile("invalid-lane.mid");

    const bool trackResult = processor.exportTrackToFile(TrackType::Kick, trackFile);
    const bool laneResult = processor.exportTrackToFile(kickLaneId, laneFile);
    const bool invalidLaneResult = processor.exportTrackToFile(RuntimeLaneId("missing:lane"), invalidFile);

    expect(trackResult == laneResult, "Lane-aware export path must mirror TrackType export result.");
    expect(!invalidLaneResult, "Missing lane export must fail safely.");

    if (laneResult)
    {
        expect(trackFile.existsAsFile(), "TrackType export must create a MIDI file when successful.");
        expect(laneFile.existsAsFile(), "Lane-aware export must create a MIDI file when successful.");
        expect(trackFile.getSize() > 0, "TrackType export file must not be empty.");
        expect(laneFile.getSize() > 0, "Lane-aware export file must not be empty.");
    }

    cleanupFile(trackFile);
    cleanupFile(laneFile);
    cleanupFile(invalidFile);
}

void testLaneAwareTemporaryMidiPath()
{
    BoomBapGeneratorAudioProcessor processor;
    const auto project = processor.getProjectSnapshot();
    const auto kickLaneId = requireBackedLaneId(project, TrackType::Kick);

    const auto trackFile = processor.createTemporaryTrackMidiFile(TrackType::Kick);
    const auto laneFile = processor.createTemporaryTrackMidiFile(kickLaneId);
    const auto invalidLaneFile = processor.createTemporaryTrackMidiFile(RuntimeLaneId("missing:lane"));

    expect(trackFile.existsAsFile(), "TrackType temp MIDI path must create a file for a backed lane.");
    expect(laneFile.existsAsFile(), "Lane-aware temp MIDI path must create a file for a backed lane.");
    expect(invalidLaneFile == juce::File(), "Missing lane temp MIDI path must return a safe empty file.");

    cleanupFile(trackFile);
    cleanupFile(laneFile);
}

void testLaneAwareSampleCommandPath()
{
    BoomBapGeneratorAudioProcessor processorFromTrack;
    const auto project = processorFromTrack.getProjectSnapshot();
    const auto kickLaneId = requireBackedLaneId(project, TrackType::Kick);

    const auto trackDirectory = processorFromTrack.getLaneSampleDirectory(TrackType::Kick);
    const bool nextTrackResult = processorFromTrack.selectNextLaneSample(TrackType::Kick);
    const bool previousTrackResult = processorFromTrack.selectPreviousLaneSample(TrackType::Kick);

    BoomBapGeneratorAudioProcessor processorFromLane;
    const auto laneDirectory = processorFromLane.getLaneSampleDirectory(kickLaneId);
    const bool nextLaneResult = processorFromLane.selectNextLaneSample(kickLaneId);
    const bool previousLaneResult = processorFromLane.selectPreviousLaneSample(kickLaneId);
    const bool invalidLaneResult = processorFromLane.selectNextLaneSample(RuntimeLaneId("missing:lane"));

    expect(trackDirectory.getFullPathName() == laneDirectory.getFullPathName(),
           "Lane-aware sample directory lookup must match TrackType lookup.");
    expect(nextTrackResult == nextLaneResult,
           "Lane-aware next-sample command must match TrackType behavior.");
    expect(previousTrackResult == previousLaneResult,
           "Lane-aware previous-sample command must match TrackType behavior.");
    expect(!invalidLaneResult, "Missing lane sample command must fail safely.");
}

int runTest(const char* name, const std::function<void()>& test)
{
    try
    {
        test();
        std::cout << "[PASS] " << name << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << name << ": " << error.what() << std::endl;
        return 1;
    }
}
} // namespace
} // namespace bbg

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    using namespace bbg;

    int failures = 0;
    failures += runTest("Lane-aware export path", testLaneAwareExportTrackPath);
    failures += runTest("Lane-aware temporary MIDI path", testLaneAwareTemporaryMidiPath);
    failures += runTest("Lane-aware sample command path", testLaneAwareSampleCommandPath);

    if (failures == 0)
    {
        std::cout << "All lane boundary tests passed." << std::endl;
        return 0;
    }

    std::cerr << failures << " lane boundary test(s) failed." << std::endl;
    return 1;
}