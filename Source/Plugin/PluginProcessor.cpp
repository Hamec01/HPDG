#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include "PluginEditor.h"
#include "../Core/PatternProjectSerialization.h"
#include "../Core/TrackRegistry.h"
#include "../Engine/MidiExportEngine.h"
#include "../Engine/StyleDefaults.h"
#include "../Services/TemporaryMidiExportService.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
constexpr auto kStateType = "BoomBapState";
constexpr auto kRootSchemaVersion = 3;

inline int floorDiv(int a, int b)
{
    const int q = a / b;
    const int r = a % b;
    return (r != 0 && ((r > 0) != (b > 0))) ? (q - 1) : q;
}

int barsFromChoiceIndex(int choice)
{
    switch (choice)
    {
        case 0: return 1;
        case 1: return 2;
        case 2: return 4;
        case 3: return 8;
        case 4: return 16;
        default: return 2;
    }
}

GenreType genreFromChoice(int choice)
{
    switch (choice)
    {
        case 1: return GenreType::Rap;
        case 2: return GenreType::Trap;
        case 3: return GenreType::Drill;
        default: return GenreType::BoomBap;
    }
}

float defaultBpmForGenre(GenreType genre)
{
    switch (genre)
    {
        case GenreType::Trap: return 145.0f;
        case GenreType::Drill: return 142.0f;
        case GenreType::Rap: return 96.0f;
        case GenreType::BoomBap:
        default: return 90.0f;
    }
}

bool isSyncEnabled(const juce::AudioProcessorValueTreeState& apvts)
{
    const auto* syncValue = apvts.getRawParameterValue(ParamIds::syncDawTempo);
    return syncValue != nullptr && syncValue->load() > 0.5f;
}

bool shouldIncludeTrackForPlayback(const PatternProject& project, const TrackState& track)
{
    if (!track.enabled || track.muted)
        return false;

    const bool hasSolo = std::any_of(project.tracks.begin(), project.tracks.end(), [](const TrackState& t)
    {
        return t.solo;
    });

    if (hasSolo && !track.solo)
        return false;

    return true;
}

std::optional<juce::Range<int>> activePreviewLoopTicks(const PatternProject& project)
{
    if (project.previewPlaybackMode != PreviewPlaybackMode::LoopRange || !project.previewLoopTicks.has_value())
        return std::nullopt;

    if (project.previewLoopTicks->getLength() <= 0)
        return std::nullopt;

    return project.previewLoopTicks;
}

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
    const auto line = juce::Time::getCurrentTime().toString(true, true) + " | PROCESSOR | " + message + "\n";
    file.appendText(line, false, false, "\n");
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout BoomBapGeneratorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::bpm, "BPM", juce::NormalisableRange<float>(60.0f, 180.0f, 0.1f), 90.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(ParamIds::bpmLock, "BPM Lock", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(ParamIds::syncDawTempo, "Sync DAW Tempo", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::swingPercent, "Swing %", juce::NormalisableRange<float>(50.0f, 75.0f, 0.1f), 56.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::velocityAmount, "Velocity Amount", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::timingAmount, "Timing Amount", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.4f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::humanizeAmount, "Humanize Amount", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::densityAmount, "Density Amount", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::tempoInterpretation,
                                                                   "Tempo Interpretation",
                                                                   juce::StringArray { "Auto", "Original", "Half-time Aware" },
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::bars,
                                                                   "Bars",
                                                                   juce::StringArray { "1", "2", "4", "8", "16" },
                                                                   1));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::genre,
                                                                   "Genre",
                                                                   juce::StringArray { "Boom Bap", "Rap", "Trap", "Drill" },
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::keyRoot,
                                                                   "Key Root",
                                                                   juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" },
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::scaleMode,
                                                                   "Scale",
                                                                   juce::StringArray { "Minor", "Major", "Harmonic Minor" },
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::boombapSubstyle, "BoomBap Substyle", getBoomBapSubstyleNames(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::rapSubstyle,
                                                                   "Rap Substyle",
                                                                   getRapSubstyleNames(),
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::trapSubstyle,
                                                                   "Trap Substyle",
                                                                   getTrapSubstyleNames(),
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::drillSubstyle,
                                                                   "Drill Substyle",
                                                                   getDrillSubstyleNames(),
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(ParamIds::seed, "Seed", 1, 999999, 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>(ParamIds::seedLock, "Seed Lock", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::masterVolume, "Master Volume", juce::NormalisableRange<float>(0.0f, 1.5f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::masterCompressor, "Master Compressor", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::masterLofi, "Master LoFi", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    return { params.begin(), params.end() };
}

BoomBapGeneratorAudioProcessor::BoomBapGeneratorAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , project(createDefaultProject())
    , apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    sampleLibraryManager.setGenre(project.params.genre);
    sampleLibraryManager.scan();
    laneSampleBank.applyLibrary(sampleLibraryManager);

    {
        std::scoped_lock lock(projectMutex);
        for (auto& track : project.tracks)
        {
            track.selectedSampleName = laneSampleBank.getSelectedName(track.type);
            track.selectedSampleIndex = laneSampleBank.getSelectedIndex(track.type);
        }
    }

    applySelectedStylePreset(true);

    generatePattern();
}

BoomBapGeneratorAudioProcessor::~BoomBapGeneratorAudioProcessor() = default;

void BoomBapGeneratorAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    transportSamplePosition = 0;
    previewSamplePosition = 0;
    previewEngine.prepare(sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(getBlockSize() > 0 ? getBlockSize() : 1024);
    spec.numChannels = 2;

    previewCompressor.reset();
    previewCompressor.prepare(spec);
    previewCompressor.setAttack(7.0f);
    previewCompressor.setRelease(120.0f);

    previewLofiFilter.reset();
    previewLofiFilter.prepare(spec);
    previewLofiFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    previewLofiFilter.setCutoffFrequency(18000.0f);

    lofiDownsampleCounter = 0;
    lofiHeldSample = { 0.0f, 0.0f };
}

void BoomBapGeneratorAudioProcessor::releaseResources() {}

bool BoomBapGeneratorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void BoomBapGeneratorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    midiMessages.clear();

    const auto snapshot = TransportSnapshot::fromPlayHead(getPlayHead());
    const auto currentParams = buildParamsFromState(snapshot);
    {
        std::scoped_lock lock(projectMutex);
        lastTransport = snapshot;
        project.params = currentParams;
    }

    int startSample = transportSamplePosition;
    if (snapshot.hasPpq)
        startSample = stepToSamples(static_cast<int>(snapshot.ppqPosition * 4.0), currentSampleRate, currentParams.bpm);

    const int numSamples = buffer.getNumSamples();
    const int patternLength = getPatternLengthSamples();
    if (patternLength <= 0)
        return;

    std::scoped_lock lock(projectMutex);

    for (int i = 0; i < midiCache.getNumEvents(); ++i)
    {
        const auto* holder = midiCache.getEventPointer(i);
        if (holder == nullptr)
            continue;

        const auto eventSample = static_cast<int>(holder->message.getTimeStamp());
        const int blockStartInPattern = ((startSample % patternLength) + patternLength) % patternLength;
        int rel = eventSample - blockStartInPattern;
        if (rel < 0)
            rel += patternLength;

        if (rel >= 0 && rel < numSamples)
            midiMessages.addEvent(holder->message, rel);
    }

    if (previewPlaying)
    {
        const auto schedulePreviewSegment = [this](int segmentStart,
                                                   int segmentLength,
                                                   int bufferOffset,
                                                   const std::optional<juce::Range<int>>& allowedSampleRange)
        {
            for (const auto& event : previewEvents)
            {
                const int eventSample = event.sample;
                if (allowedSampleRange.has_value() && !allowedSampleRange->contains(eventSample))
                    continue;

                const int rel = eventSample - segmentStart;
                if (rel >= 0 && rel < segmentLength)
                    previewEngine.noteOnAtSample(event.track, event.gain, bufferOffset + rel, laneSampleBank);
            }
        };

        if (const auto loopTicks = activePreviewLoopTicks(project); loopTicks.has_value())
        {
            const int loopStartSample = ticksToSamples(loopTicks->getStart(), currentSampleRate, project.params.bpm);
            const int loopEndSample = ticksToSamples(loopTicks->getEnd(), currentSampleRate, project.params.bpm);

            if (loopEndSample > loopStartSample)
            {
                int localPreviewPosition = previewSamplePosition;
                if (localPreviewPosition < loopStartSample || localPreviewPosition >= loopEndSample)
                    localPreviewPosition = loopStartSample;

                int remainingSamples = numSamples;
                int bufferOffset = 0;
                const auto loopSampleRange = juce::Range<int>(loopStartSample, loopEndSample);

                while (remainingSamples > 0)
                {
                    const int segmentStart = juce::jlimit(loopStartSample, loopEndSample - 1, localPreviewPosition);
                    const int segmentLength = juce::jmin(remainingSamples, loopEndSample - segmentStart);
                    schedulePreviewSegment(segmentStart, segmentLength, bufferOffset, loopSampleRange);

                    remainingSamples -= segmentLength;
                    bufferOffset += segmentLength;
                    localPreviewPosition = segmentStart + segmentLength;
                    if (localPreviewPosition >= loopEndSample)
                        localPreviewPosition = loopStartSample;
                }

                previewEngine.render(buffer, 0, numSamples);
                applyMasterFx(buffer);
                previewSamplePosition = localPreviewPosition;
            }
        }
        else
        {
            const int previewStart = previewSamplePosition;
            const int previewBlockStartInPattern = ((previewStart % patternLength) + patternLength) % patternLength;

            for (const auto& event : previewEvents)
            {
                const int eventSample = event.sample;
                int rel = eventSample - previewBlockStartInPattern;
                if (rel < 0)
                    rel += patternLength;

                if (rel >= 0 && rel < numSamples)
                    previewEngine.noteOnAtSample(event.track, event.gain, rel, laneSampleBank);
            }

            previewEngine.render(buffer, 0, numSamples);
            applyMasterFx(buffer);
            previewSamplePosition = previewStart + numSamples;
        }
    }

    transportSamplePosition = startSample + numSamples;
}

juce::AudioProcessorEditor* BoomBapGeneratorAudioProcessor::createEditor()
{
    return new BoomBGeneratorAudioProcessorEditor(*this);
}

bool BoomBapGeneratorAudioProcessor::hasEditor() const { return true; }
const juce::String BoomBapGeneratorAudioProcessor::getName() const { return JucePlugin_Name; }
bool BoomBapGeneratorAudioProcessor::acceptsMidi() const { return true; }
bool BoomBapGeneratorAudioProcessor::producesMidi() const { return true; }
bool BoomBapGeneratorAudioProcessor::isMidiEffect() const
{
#if JUCE_STANDALONE_APPLICATION
    return false;
#else
    return true;
#endif
}
double BoomBapGeneratorAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int BoomBapGeneratorAudioProcessor::getNumPrograms() { return 1; }
int BoomBapGeneratorAudioProcessor::getCurrentProgram() { return 0; }
void BoomBapGeneratorAudioProcessor::setCurrentProgram(int) {}
const juce::String BoomBapGeneratorAudioProcessor::getProgramName(int) { return {}; }
void BoomBapGeneratorAudioProcessor::changeProgramName(int, const juce::String&) {}

void BoomBapGeneratorAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state(kStateType);
    state.copyPropertiesAndChildrenFrom(apvts.copyState(), nullptr);
    state.setProperty("root_schema_version", kRootSchemaVersion, nullptr);

    {
        std::scoped_lock lock(projectMutex);
        serializePatternProjectToState(state);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void BoomBapGeneratorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto loaded = juce::ValueTree::fromXml(*xml);
    if (!loaded.isValid())
        return;

    apvts.replaceState(loaded);

    const auto* genreValue = apvts.getRawParameterValue(ParamIds::genre);
    const auto* boombapSubstyleValue = apvts.getRawParameterValue(ParamIds::boombapSubstyle);
    const auto* rapSubstyleValue = apvts.getRawParameterValue(ParamIds::rapSubstyle);
    const auto* trapSubstyleValue = apvts.getRawParameterValue(ParamIds::trapSubstyle);
    const auto* drillSubstyleValue = apvts.getRawParameterValue(ParamIds::drillSubstyle);
    lastAppliedGenreChoice = genreValue != nullptr ? static_cast<int>(genreValue->load()) : 0;
    lastAppliedBoomBapSubstyleChoice = boombapSubstyleValue != nullptr ? static_cast<int>(boombapSubstyleValue->load()) : 0;
    lastAppliedRapSubstyleChoice = rapSubstyleValue != nullptr ? static_cast<int>(rapSubstyleValue->load()) : 0;
    lastAppliedTrapSubstyleChoice = trapSubstyleValue != nullptr ? static_cast<int>(trapSubstyleValue->load()) : 0;
    lastAppliedDrillSubstyleChoice = drillSubstyleValue != nullptr ? static_cast<int>(drillSubstyleValue->load()) : 0;

    {
        std::scoped_lock lock(projectMutex);
        restorePatternProjectFromState(loaded);

        rescanLaneSamplesLocked();

        project.params = buildParamsFromState(lastTransport);
        rebuildMidiCache();
    }
}

void BoomBapGeneratorAudioProcessor::generatePattern()
{
    std::scoped_lock lock(projectMutex);
    advanceSeedForGeneration(std::nullopt);
    project.params = buildParamsFromState(lastTransport);
    switch (project.params.genre)
    {
        case GenreType::Rap: rapEngine.generate(project); break;
        case GenreType::Trap: trapEngine.generate(project); break;
        case GenreType::Drill: drillEngine.generate(project); break;
        case GenreType::BoomBap:
        default: boomBapEngine.generate(project); break;
    }
    ++project.generationCounter;
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::generateTrackNew(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    advanceSeedForGeneration(track);
    project.params = buildParamsFromState(lastTransport);
    switch (project.params.genre)
    {
        case GenreType::Rap: rapEngine.generateTrackNew(project, track); break;
        case GenreType::Trap: trapEngine.generateTrackNew(project, track); break;
        case GenreType::Drill: drillEngine.generateTrackNew(project, track); break;
        case GenreType::BoomBap:
        default: boomBapEngine.generateTrackNew(project, track); break;
    }
    ++project.generationCounter;
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::regenerateTrack(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    advanceSeedForGeneration(track);
    project.params = buildParamsFromState(lastTransport);
    switch (project.params.genre)
    {
        case GenreType::Rap: rapEngine.regenerateTrackVariation(project, track); break;
        case GenreType::Trap: trapEngine.regenerateTrackVariation(project, track); break;
        case GenreType::Drill: drillEngine.regenerateTrackVariation(project, track); break;
        case GenreType::BoomBap:
        default: boomBapEngine.regenerateTrackVariation(project, track); break;
    }
    ++project.generationCounter;
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::mutatePattern()
{
    std::scoped_lock lock(projectMutex);
    advanceSeedForGeneration(std::nullopt);
    project.params = buildParamsFromState(lastTransport);
    switch (project.params.genre)
    {
        case GenreType::Rap: rapEngine.mutatePattern(project); break;
        case GenreType::Trap: trapEngine.mutatePattern(project); break;
        case GenreType::Drill: drillEngine.mutatePattern(project); break;
        case GenreType::BoomBap:
        default: boomBapEngine.mutatePattern(project); break;
    }
    ++project.generationCounter;
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::mutateTrack(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    advanceSeedForGeneration(track);
    project.params = buildParamsFromState(lastTransport);
    switch (project.params.genre)
    {
        case GenreType::Rap: rapEngine.mutateTrack(project, track); break;
        case GenreType::Trap: trapEngine.mutateTrack(project, track); break;
        case GenreType::Drill: drillEngine.mutateTrack(project, track); break;
        case GenreType::BoomBap:
        default: boomBapEngine.mutateTrack(project, track); break;
    }
    ++project.generationCounter;
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::startPreview()
{
    std::scoped_lock lock(projectMutex);
    if (!laneSampleBank.hasSamples(TrackType::Kick)
        && !laneSampleBank.hasSamples(TrackType::Snare)
        && !laneSampleBank.hasSamples(TrackType::HiHat))
        rescanLaneSamplesLocked();
    rebuildMidiCache();
    previewPlaying = true;
    startPreviewFromCurrentStartStepLocked();
    previewEngine.reset();
}

void BoomBapGeneratorAudioProcessor::stopPreview()
{
    std::scoped_lock lock(projectMutex);
    previewPlaying = false;
    previewEngine.reset();
}

bool BoomBapGeneratorAudioProcessor::isPreviewPlaying() const
{
    std::scoped_lock lock(projectMutex);
    return previewPlaying;
}

float BoomBapGeneratorAudioProcessor::getPreviewPlayheadStep() const
{
    std::scoped_lock lock(projectMutex);
    if (!previewPlaying)
        return -1.0f;

    const int totalSteps = juce::jmax(1, project.params.bars * 16);
    const int patternLength = getPatternLengthSamples();
    if (patternLength <= 0)
        return -1.0f;

    const int sampleInPattern = ((previewSamplePosition % patternLength) + patternLength) % patternLength;
    return static_cast<float>(sampleInPattern) * static_cast<float>(totalSteps) / static_cast<float>(patternLength);
}

void BoomBapGeneratorAudioProcessor::setPreviewStartStep(int step)
{
    std::scoped_lock lock(projectMutex);
    const int maxStep = juce::jmax(0, project.params.bars * 16 - 1);
    project.previewStartStep = juce::jlimit(0, maxStep, step);
}

void BoomBapGeneratorAudioProcessor::setPreviewPlaybackMode(PreviewPlaybackMode mode)
{
    std::scoped_lock lock(projectMutex);
    project.previewPlaybackMode = mode;
    if (previewPlaying)
    {
        startPreviewFromCurrentStartStepLocked();
        previewEngine.reset();
    }
}

PreviewPlaybackMode BoomBapGeneratorAudioProcessor::getPreviewPlaybackMode() const
{
    std::scoped_lock lock(projectMutex);
    return project.previewPlaybackMode;
}

void BoomBapGeneratorAudioProcessor::restoreEditorProjectSnapshot(const PatternProject& snapshot)
{
    std::scoped_lock lock(projectMutex);
    project = snapshot;
    project.params = buildParamsFromState(lastTransport);
    PatternProjectSerialization::validate(project);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setPreviewLoopRegion(const std::optional<juce::Range<int>>& tickRange)
{
    std::scoped_lock lock(projectMutex);
    if (tickRange.has_value() && tickRange->getLength() > 0)
    {
        const int totalTicks = juce::jmax(1, project.params.bars * 16 * ticksPerStep());
        const int startTick = juce::jlimit(0, totalTicks - 1, tickRange->getStart());
        const int endTick = juce::jlimit(startTick + 1, totalTicks, tickRange->getEnd());
        if (endTick > startTick)
            project.previewLoopTicks = juce::Range<int>(startTick, endTick);
        else
            project.previewLoopTicks.reset();
    }
    else
    {
        project.previewLoopTicks.reset();
    }

    if (previewPlaying && project.previewPlaybackMode == PreviewPlaybackMode::LoopRange)
    {
        startPreviewFromCurrentStartStepLocked();
        previewEngine.reset();
    }
}

std::optional<juce::Range<int>> BoomBapGeneratorAudioProcessor::getPreviewLoopRegion() const
{
    std::scoped_lock lock(projectMutex);
    if (!project.previewLoopTicks.has_value() || project.previewLoopTicks->getLength() <= 0)
        return std::nullopt;

    return project.previewLoopTicks;
}

void BoomBapGeneratorAudioProcessor::startPreviewFromCurrentStartStepLocked()
{
    if (const auto loopTicks = activePreviewLoopTicks(project); loopTicks.has_value())
    {
        previewSamplePosition = ticksToSamples(loopTicks->getStart(), currentSampleRate, project.params.bpm);
        return;
    }

    const int maxStep = juce::jmax(0, project.params.bars * 16 - 1);
    const int startStep = juce::jlimit(0, maxStep, project.previewStartStep);
    previewSamplePosition = stepToSamples(startStep, currentSampleRate, project.params.bpm);
}

void BoomBapGeneratorAudioProcessor::setStartPlayWithDawEnabled(bool enabled)
{
    std::scoped_lock lock(projectMutex);
    startPlayWithDawEnabled = enabled;
}

bool BoomBapGeneratorAudioProcessor::isStartPlayWithDawEnabled() const
{
    std::scoped_lock lock(projectMutex);
    return startPlayWithDawEnabled;
}

void BoomBapGeneratorAudioProcessor::rescanLaneSamples()
{
    std::scoped_lock lock(projectMutex);
    rescanLaneSamplesLocked();
}

void BoomBapGeneratorAudioProcessor::rescanLaneSamplesLocked()
{
    sampleLibraryManager.setGenre(project.params.genre);
    sampleLibraryManager.scan();
    laneSampleBank.applyLibrary(sampleLibraryManager);

    for (auto& track : project.tracks)
    {
        const auto desired = track.selectedSampleIndex;
        laneSampleBank.selectIndex(track.type, desired);
        track.selectedSampleIndex = laneSampleBank.getSelectedIndex(track.type);
        track.selectedSampleName = laneSampleBank.getSelectedName(track.type);
    }
}

bool BoomBapGeneratorAudioProcessor::selectNextLaneSample(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    if (!laneSampleBank.hasSamples(track))
        rescanLaneSamplesLocked();
    const bool ok = laneSampleBank.selectNext(track);
    if (auto* state = findTrackState(track))
    {
        state->selectedSampleIndex = laneSampleBank.getSelectedIndex(track);
        state->selectedSampleName = laneSampleBank.getSelectedName(track);
    }

    return ok;
}

bool BoomBapGeneratorAudioProcessor::selectPreviousLaneSample(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    if (!laneSampleBank.hasSamples(track))
        rescanLaneSamplesLocked();
    const bool ok = laneSampleBank.selectPrevious(track);
    if (auto* state = findTrackState(track))
    {
        state->selectedSampleIndex = laneSampleBank.getSelectedIndex(track);
        state->selectedSampleName = laneSampleBank.getSelectedName(track);
    }

    return ok;
}

juce::String BoomBapGeneratorAudioProcessor::getSelectedLaneSampleName(TrackType track) const
{
    std::scoped_lock lock(projectMutex);
    return laneSampleBank.getSelectedName(track);
}

bool BoomBapGeneratorAudioProcessor::selectNextLaneSample(const RuntimeLaneId& laneId)
{
    const auto type = resolveTrackTypeForLaneId(laneId);
    return type.has_value() ? selectNextLaneSample(*type) : false;
}

bool BoomBapGeneratorAudioProcessor::selectPreviousLaneSample(const RuntimeLaneId& laneId)
{
    const auto type = resolveTrackTypeForLaneId(laneId);
    return type.has_value() ? selectPreviousLaneSample(*type) : false;
}

int BoomBapGeneratorAudioProcessor::getBassKeyRootChoice() const
{
    const auto* value = apvts.getRawParameterValue(ParamIds::keyRoot);
    return value != nullptr ? static_cast<int>(value->load()) : 0;
}

int BoomBapGeneratorAudioProcessor::getBassScaleModeChoice() const
{
    const auto* value = apvts.getRawParameterValue(ParamIds::scaleMode);
    return value != nullptr ? static_cast<int>(value->load()) : 0;
}

void BoomBapGeneratorAudioProcessor::setBassKeyRootChoice(int choice)
{
    if (auto* parameter = apvts.getParameter(ParamIds::keyRoot))
    {
        const int clamped = juce::jlimit(0, 11, choice);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(clamped)));
        parameter->endChangeGesture();
    }
}

void BoomBapGeneratorAudioProcessor::setBassScaleModeChoice(int choice)
{
    if (auto* parameter = apvts.getParameter(ParamIds::scaleMode))
    {
        const int clamped = juce::jlimit(0, 2, choice);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(clamped)));
        parameter->endChangeGesture();
    }
}

void BoomBapGeneratorAudioProcessor::setTrackSolo(TrackType track, bool value)
{
    std::scoped_lock lock(projectMutex);
    if (auto* state = findTrackState(track))
        state->solo = value;

    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setTrackSolo(const RuntimeLaneId& laneId, bool value)
{
    if (const auto type = resolveTrackTypeForLaneId(laneId); type.has_value())
        setTrackSolo(*type, value);
}

void BoomBapGeneratorAudioProcessor::setTrackMuted(TrackType track, bool value)
{
    std::scoped_lock lock(projectMutex);
    if (auto* state = findTrackState(track))
        state->muted = value;

    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setTrackMuted(const RuntimeLaneId& laneId, bool value)
{
    if (const auto type = resolveTrackTypeForLaneId(laneId); type.has_value())
        setTrackMuted(*type, value);
}

void BoomBapGeneratorAudioProcessor::setTrackLocked(TrackType track, bool value)
{
    std::scoped_lock lock(projectMutex);
    if (auto* state = findTrackState(track))
        state->locked = value;
}

void BoomBapGeneratorAudioProcessor::setTrackLocked(const RuntimeLaneId& laneId, bool value)
{
    if (const auto type = resolveTrackTypeForLaneId(laneId); type.has_value())
        setTrackLocked(*type, value);
}

void BoomBapGeneratorAudioProcessor::setTrackEnabled(TrackType track, bool value)
{
    std::scoped_lock lock(projectMutex);
    if (auto* state = findTrackState(track))
        state->enabled = value;

    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setTrackEnabled(const RuntimeLaneId& laneId, bool value)
{
    if (const auto type = resolveTrackTypeForLaneId(laneId); type.has_value())
        setTrackEnabled(*type, value);
}

void BoomBapGeneratorAudioProcessor::setTrackLaneVolume(TrackType track, float volume)
{
    std::scoped_lock lock(projectMutex);
    if (auto* state = findTrackState(track))
        state->laneVolume = juce::jlimit(0.0f, 1.5f, volume);

    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setTrackLaneVolume(const RuntimeLaneId& laneId, float volume)
{
    if (const auto type = resolveTrackTypeForLaneId(laneId); type.has_value())
        setTrackLaneVolume(*type, volume);
}

void BoomBapGeneratorAudioProcessor::setTrackNotes(TrackType track, const std::vector<NoteEvent>& notes)
{
    std::scoped_lock lock(projectMutex);
    auto* state = findTrackState(track);
    if (state == nullptr || state->locked || !state->enabled)
        return;

    state->notes = notes;
    const int bars = juce::jmax(1, project.params.bars);
    const int maxTicks = bars * 16 * ticksPerStep();

    for (auto& note : state->notes)
    {
        note.pitch = juce::jlimit(0, 127, note.pitch);
        note.velocity = juce::jlimit(1, 127, note.velocity);
        note.length = juce::jlimit(1, 64, note.length);

        int ticks = note.step * ticksPerStep() + note.microOffset;
        ticks = juce::jlimit(0, juce::jmax(0, maxTicks - 1), ticks);

        int step = floorDiv(ticks, ticksPerStep());
        int micro = ticks - step * ticksPerStep();
        if (micro > ticksPerStep() / 2)
        {
            micro -= ticksPerStep();
            ++step;
        }

        note.step = juce::jlimit(0, bars * 16 - 1, step);
        note.microOffset = juce::jlimit(-960, 960, micro);
    }

    std::sort(state->notes.begin(), state->notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.pitch < b.pitch;
    });

    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setTrackNotes(const RuntimeLaneId& laneId, const std::vector<NoteEvent>& notes)
{
    if (const auto type = resolveTrackTypeForLaneId(laneId); type.has_value())
        setTrackNotes(*type, notes);
}

void BoomBapGeneratorAudioProcessor::setSelectedTrack(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    for (size_t i = 0; i < project.tracks.size(); ++i)
    {
        if (project.tracks[i].type == track)
        {
            project.selectedTrackIndex = static_cast<int>(i);
            return;
        }
    }
}

void BoomBapGeneratorAudioProcessor::setSelectedTrack(const RuntimeLaneId& laneId)
{
    if (const auto type = resolveTrackTypeForLaneId(laneId); type.has_value())
        setSelectedTrack(*type);
}

void BoomBapGeneratorAudioProcessor::setSoundModuleTrack(const std::optional<TrackType>& track)
{
    std::scoped_lock lock(projectMutex);
    if (!track.has_value())
    {
        project.soundModuleTrackIndex = -1;
        return;
    }

    for (size_t i = 0; i < project.tracks.size(); ++i)
    {
        if (project.tracks[i].type == *track)
        {
            project.soundModuleTrackIndex = static_cast<int>(i);
            return;
        }
    }

    project.soundModuleTrackIndex = -1;
}

std::optional<TrackType> BoomBapGeneratorAudioProcessor::getSoundModuleTrack() const
{
    std::scoped_lock lock(projectMutex);
    if (project.soundModuleTrackIndex < 0 || project.soundModuleTrackIndex >= static_cast<int>(project.tracks.size()))
        return std::nullopt;

    return project.tracks[static_cast<size_t>(project.soundModuleTrackIndex)].type;
}

void BoomBapGeneratorAudioProcessor::setTrackSoundLayer(TrackType track, const SoundLayerState& state)
{
    std::scoped_lock lock(projectMutex);
    if (auto* trackState = findTrackState(track))
        trackState->sound = state;
}

void BoomBapGeneratorAudioProcessor::setTrackSoundLayer(const RuntimeLaneId& laneId, const SoundLayerState& state)
{
    if (const auto type = resolveTrackTypeForLaneId(laneId); type.has_value())
        setTrackSoundLayer(*type, state);
}

void BoomBapGeneratorAudioProcessor::setGlobalSoundLayer(const SoundLayerState& state)
{
    std::scoped_lock lock(projectMutex);
    project.globalSound = state;
}

void BoomBapGeneratorAudioProcessor::setHatFxDragDensity(float density, bool lockDragDensity)
{
    std::scoped_lock lock(projectMutex);
    hatFxDragDensity = juce::jlimit(0.0f, 2.0f, density);
    hatFxDragDensityLocked = lockDragDensity;
}

float BoomBapGeneratorAudioProcessor::getHatFxDragDensity() const
{
    std::scoped_lock lock(projectMutex);
    return hatFxDragDensity;
}

bool BoomBapGeneratorAudioProcessor::isHatFxDragDensityLocked() const
{
    std::scoped_lock lock(projectMutex);
    return hatFxDragDensityLocked;
}

void BoomBapGeneratorAudioProcessor::clearTrack(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    if (auto* state = findTrackState(track))
        state->notes.clear();

    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::clearTrack(const RuntimeLaneId& laneId)
{
    if (const auto type = resolveTrackTypeForLaneId(laneId); type.has_value())
        clearTrack(*type);
}

juce::File BoomBapGeneratorAudioProcessor::getLaneSampleDirectory(TrackType track) const
{
    std::scoped_lock lock(projectMutex);
    return sampleLibraryManager.getResolvedRootDirectory()
        .getChildFile(SampleLibraryManager::folderNameForGenre(project.params.genre))
        .getChildFile(SampleLibraryManager::folderNameForTrack(track));
}

bool BoomBapGeneratorAudioProcessor::importLaneSample(TrackType track, const juce::File& sourceFile, juce::String* errorMessage)
{
    if (!sourceFile.existsAsFile())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Sample file not found.";
        return false;
    }

    std::scoped_lock lock(projectMutex);
    auto targetDirectory = sampleLibraryManager.getResolvedRootDirectory()
        .getChildFile(SampleLibraryManager::folderNameForGenre(project.params.genre))
        .getChildFile(SampleLibraryManager::folderNameForTrack(track));
    if (!targetDirectory.createDirectory())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Failed to create lane sample directory.";
        return false;
    }

    const auto targetFile = targetDirectory.getNonexistentChildFile(sourceFile.getFileNameWithoutExtension(),
                                                                    sourceFile.getFileExtension(),
                                                                    false);
    if (!sourceFile.copyFileTo(targetFile))
    {
        if (errorMessage != nullptr)
            *errorMessage = "Failed to copy sample into lane directory.";
        return false;
    }

    rescanLaneSamplesLocked();
    if (auto* state = findTrackState(track))
    {
        state->selectedSampleIndex = laneSampleBank.getSelectedIndex(track);
        state->selectedSampleName = laneSampleBank.getSelectedName(track);
    }

    return true;
}

bool BoomBapGeneratorAudioProcessor::deleteSelectedLaneSample(TrackType track, juce::String* errorMessage)
{
    std::scoped_lock lock(projectMutex);
    const auto& samples = sampleLibraryManager.getSamples(track);
    const int selectedIndex = laneSampleBank.getSelectedIndex(track);
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(samples.size()))
    {
        if (errorMessage != nullptr)
            *errorMessage = "No selected sample to delete.";
        return false;
    }

    const auto targetFile = samples[static_cast<size_t>(selectedIndex)].file;
    if (!targetFile.existsAsFile() || !targetFile.deleteFile())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Failed to delete selected sample.";
        return false;
    }

    rescanLaneSamplesLocked();
    if (auto* state = findTrackState(track))
    {
        state->selectedSampleIndex = laneSampleBank.getSelectedIndex(track);
        state->selectedSampleName = laneSampleBank.getSelectedName(track);
    }

    return true;
}

bool BoomBapGeneratorAudioProcessor::exportFullPatternToFile(const juce::File& targetFile) const
{
    logDrag("exportFullPatternToFile begin file=" + targetFile.getFullPathName());
    PatternProject snapshot;
    {
        std::scoped_lock lock(projectMutex);
        snapshot = project;
    }

    const bool ok = MidiExportEngine::saveMultiTrackMidiFile(snapshot, targetFile, 960);
    logDrag("exportFullPatternToFile done ok=" + juce::String(ok ? "1" : "0"));
    return ok;
}

bool BoomBapGeneratorAudioProcessor::exportTrackToFile(TrackType track, const juce::File& targetFile) const
{
    logDrag("exportTrackToFile begin track=" + juce::String(static_cast<int>(track)) + " file=" + targetFile.getFullPathName());
    PatternProject snapshot;
    {
        std::scoped_lock lock(projectMutex);
        snapshot = project;
    }

    const bool ok = MidiExportEngine::saveMidiFile(snapshot, targetFile, track, 960, false, false);
    logDrag("exportTrackToFile done ok=" + juce::String(ok ? "1" : "0") + " track=" + juce::String(static_cast<int>(track)));
    return ok;
}

bool BoomBapGeneratorAudioProcessor::exportLoopWavToFile(const juce::File& targetFile) const
{
    juce::ignoreUnused(targetFile);
    return false;
}

juce::File BoomBapGeneratorAudioProcessor::createTemporaryFullPatternMidiFile() const
{
    try
    {
        logDrag("createTemporaryFullPatternMidiFile begin");
        PatternProject snapshot;
        {
            std::scoped_lock lock(projectMutex);
            snapshot = project;
        }

        logDrag("createTemporaryFullPatternMidiFile snapshot tracks=" + juce::String(static_cast<int>(snapshot.tracks.size())));
        auto file = TemporaryMidiExportService::createTempFileForPattern(snapshot);
        logDrag("createTemporaryFullPatternMidiFile done exists=" + juce::String(file.existsAsFile() ? "1" : "0")
                + " path=" + file.getFullPathName());
        return file;
    }
    catch (...)
    {
        logDrag("createTemporaryFullPatternMidiFile exception");
        return {};
    }
}

juce::File BoomBapGeneratorAudioProcessor::createTemporaryTrackMidiFile(TrackType track) const
{
    try
    {
        logDrag("createTemporaryTrackMidiFile begin track=" + juce::String(static_cast<int>(track)));
        PatternProject snapshot;
        {
            std::scoped_lock lock(projectMutex);
            snapshot = project;
        }

        logDrag("createTemporaryTrackMidiFile snapshot tracks=" + juce::String(static_cast<int>(snapshot.tracks.size()))
                + " track=" + juce::String(static_cast<int>(track)));
        auto file = TemporaryMidiExportService::createTempFileForTrack(snapshot, track);
        logDrag("createTemporaryTrackMidiFile done exists=" + juce::String(file.existsAsFile() ? "1" : "0")
                + " track=" + juce::String(static_cast<int>(track))
                + " path=" + file.getFullPathName());
        return file;
    }
    catch (...)
    {
        logDrag("createTemporaryTrackMidiFile exception track=" + juce::String(static_cast<int>(track)));
        return {};
    }
}

PatternProject BoomBapGeneratorAudioProcessor::getProjectSnapshot() const
{
    std::scoped_lock lock(projectMutex);
    return project;
}

TransportSnapshot BoomBapGeneratorAudioProcessor::getLastTransportSnapshot() const
{
    std::scoped_lock lock(projectMutex);
    return lastTransport;
}

void BoomBapGeneratorAudioProcessor::setSampleAnalysisRequest(const SampleAnalysisRequest& request)
{
    std::scoped_lock lock(projectMutex);
    currentAnalysisRequest = request;
}

SampleAnalysisRequest BoomBapGeneratorAudioProcessor::getSampleAnalysisRequest() const
{
    std::scoped_lock lock(projectMutex);
    return currentAnalysisRequest;
}

bool BoomBapGeneratorAudioProcessor::analyzeCurrentSampleSource(juce::String* errorMessage)
{
    SampleAnalysisRequest request;
    {
        std::scoped_lock lock(projectMutex);
        request = currentAnalysisRequest;
    }

    if (request.source == SampleAnalysisRequest::SourceType::AudioFile)
        return analyzeAudioFile(request.audioFile, errorMessage);

    if (errorMessage != nullptr)
        *errorMessage = "Live input analysis is not available yet.";
    return false;
}

bool BoomBapGeneratorAudioProcessor::analyzeAudioFile(const juce::File& file, juce::String* errorMessage)
{
    SampleAnalysisRequest request;
    double hostBpm = 0.0;
    {
        std::scoped_lock lock(projectMutex);
        request = currentAnalysisRequest;
        request.source = SampleAnalysisRequest::SourceType::AudioFile;
        request.audioFile = file;
        hostBpm = lastTransport.hasHostTempo && lastTransport.bpm > 0.0 ? lastTransport.bpm : static_cast<double>(project.params.bpm);
    }

    const auto result = sampleAnalyzer.analyzeAudioFile(file, request, hostBpm, errorMessage);

    std::scoped_lock lock(projectMutex);
    currentAnalysisRequest = request;
    currentAnalysisResult = result;
    currentFeatureMap = result.valid ? sampleAnalyzer.buildFeatureMap(result) : AudioFeatureMap{};
    analysisReady = result.valid;
    currentSampleContext.enabled = sampleAwareModeEnabled && analysisReady;
    currentSampleContext.featureMap = currentFeatureMap;
    return result.valid;
}

void BoomBapGeneratorAudioProcessor::clearSampleAnalysis()
{
    std::scoped_lock lock(projectMutex);
    currentAnalysisResult = {};
    currentFeatureMap = {};
    analysisReady = false;
    currentSampleContext.featureMap = {};
    currentSampleContext.enabled = false;
}

void BoomBapGeneratorAudioProcessor::setAnalysisMode(AnalysisMode mode)
{
    std::scoped_lock lock(projectMutex);
    analysisMode = mode;
}

AnalysisMode BoomBapGeneratorAudioProcessor::getAnalysisMode() const
{
    std::scoped_lock lock(projectMutex);
    return analysisMode;
}

void BoomBapGeneratorAudioProcessor::setSampleAwareModeEnabled(bool enabled)
{
    std::scoped_lock lock(projectMutex);
    sampleAwareModeEnabled = enabled;
    currentSampleContext.enabled = enabled && analysisReady;
}

bool BoomBapGeneratorAudioProcessor::isSampleAwareModeEnabled() const
{
    std::scoped_lock lock(projectMutex);
    return sampleAwareModeEnabled;
}

bool BoomBapGeneratorAudioProcessor::isSampleAnalysisReady() const
{
    std::scoped_lock lock(projectMutex);
    return analysisReady;
}

void BoomBapGeneratorAudioProcessor::setSampleReactivity(float value)
{
    std::scoped_lock lock(projectMutex);
    currentSampleContext.reactivity = juce::jlimit(0.0f, 1.0f, value);
}

void BoomBapGeneratorAudioProcessor::setSupportVsContrast(float value)
{
    std::scoped_lock lock(projectMutex);
    currentSampleContext.supportVsContrast = juce::jlimit(0.0f, 1.0f, value);
}

SampleAnalysisResult BoomBapGeneratorAudioProcessor::getSampleAnalysisResult() const
{
    std::scoped_lock lock(projectMutex);
    return currentAnalysisResult;
}

AudioFeatureMap BoomBapGeneratorAudioProcessor::getAudioFeatureMap() const
{
    std::scoped_lock lock(projectMutex);
    return currentFeatureMap;
}

SampleAwareGenerationContext BoomBapGeneratorAudioProcessor::getSampleAwareGenerationContext() const
{
    std::scoped_lock lock(projectMutex);
    return currentSampleContext;
}

juce::String BoomBapGeneratorAudioProcessor::getGenerationDebugSummary() const
{
    std::scoped_lock lock(projectMutex);
    juce::StringArray lines;
    lines.add("Analysis mode: " + juce::String(static_cast<int>(analysisMode)));
    lines.add("Sample-aware: " + juce::String(sampleAwareModeEnabled ? "on" : "off"));
    lines.add("Analysis ready: " + juce::String(analysisReady ? "yes" : "no"));
    lines.add("Detected BPM: " + juce::String(currentAnalysisResult.detectedBpm, 2));
    lines.add("Bars: " + juce::String(currentAnalysisResult.analyzedBars));
    lines.add("Support vs Contrast: " + juce::String(currentSampleContext.supportVsContrast, 2));
    lines.add("Reactivity: " + juce::String(currentSampleContext.reactivity, 2));
    return lines.joinIntoString("\n");
}

void BoomBapGeneratorAudioProcessor::applySelectedStylePreset(bool force)
{
    const auto* genreValue = apvts.getRawParameterValue(ParamIds::genre);
    const auto* boombapSubstyleValue = apvts.getRawParameterValue(ParamIds::boombapSubstyle);
    const auto* rapSubstyleValue = apvts.getRawParameterValue(ParamIds::rapSubstyle);
    const auto* trapSubstyleValue = apvts.getRawParameterValue(ParamIds::trapSubstyle);
    const auto* drillSubstyleValue = apvts.getRawParameterValue(ParamIds::drillSubstyle);

    const int genreChoice = genreValue != nullptr ? static_cast<int>(genreValue->load()) : 0;
    const int boombapChoice = boombapSubstyleValue != nullptr ? static_cast<int>(boombapSubstyleValue->load()) : 0;
    const int rapChoice = rapSubstyleValue != nullptr ? static_cast<int>(rapSubstyleValue->load()) : 0;
    const int trapChoice = trapSubstyleValue != nullptr ? static_cast<int>(trapSubstyleValue->load()) : 0;
    const int drillChoice = drillSubstyleValue != nullptr ? static_cast<int>(drillSubstyleValue->load()) : 0;

    const bool changed = force
        || genreChoice != lastAppliedGenreChoice
        || boombapChoice != lastAppliedBoomBapSubstyleChoice
        || rapChoice != lastAppliedRapSubstyleChoice
        || trapChoice != lastAppliedTrapSubstyleChoice
        || drillChoice != lastAppliedDrillSubstyleChoice;
    if (!changed)
        return;

    const bool genreChanged = genreChoice != lastAppliedGenreChoice;

    const auto genreType = genreFromChoice(genreChoice);
    int selectedSubstyle = boombapChoice;
    if (genreType == GenreType::Rap)
        selectedSubstyle = rapChoice;
    else if (genreType == GenreType::Trap)
        selectedSubstyle = trapChoice;
    else if (genreType == GenreType::Drill)
        selectedSubstyle = drillChoice;
    const auto& style = getGenreStyleDefaults(genreType, selectedSubstyle);

    setFloatParameterValue(ParamIds::swingPercent, style.swingDefault);
    setFloatParameterValue(ParamIds::velocityAmount, style.velocityDefault);
    setFloatParameterValue(ParamIds::timingAmount, style.timingDefault);
    setFloatParameterValue(ParamIds::humanizeAmount, style.humanizeDefault);
    setFloatParameterValue(ParamIds::densityAmount, style.densityDefault);
    if (genreChanged && !isSyncEnabled(apvts))
        setFloatParameterValue(ParamIds::bpm, defaultBpmForGenre(genreType));

    {
        std::scoped_lock lock(projectMutex);
        project.params = buildParamsFromState(lastTransport);
        if (genreChanged)
            rescanLaneSamplesLocked();

        for (auto& track : project.tracks)
        {
            const auto& lane = getLaneStyleDefaults(style, track.type);
            track.enabled = lane.enabledByDefault;
            track.laneVolume = juce::jlimit(0.0f, 1.5f, lane.volumeDefault);
        }
        rebuildMidiCache();
    }

    lastAppliedGenreChoice = genreChoice;
    lastAppliedBoomBapSubstyleChoice = boombapChoice;
    lastAppliedRapSubstyleChoice = rapChoice;
    lastAppliedTrapSubstyleChoice = trapChoice;
    lastAppliedDrillSubstyleChoice = drillChoice;
}

GeneratorParams BoomBapGeneratorAudioProcessor::buildParamsFromState(const TransportSnapshot& snapshot) const
{
    GeneratorParams p;

    const auto bpmValue = apvts.getRawParameterValue(ParamIds::bpm);
    const auto syncValue = apvts.getRawParameterValue(ParamIds::syncDawTempo);
    const auto swingValue = apvts.getRawParameterValue(ParamIds::swingPercent);
    const auto velocityValue = apvts.getRawParameterValue(ParamIds::velocityAmount);
    const auto timingValue = apvts.getRawParameterValue(ParamIds::timingAmount);
    const auto humanizeValue = apvts.getRawParameterValue(ParamIds::humanizeAmount);
    const auto densityValue = apvts.getRawParameterValue(ParamIds::densityAmount);
    const auto tempoInterpretationValue = apvts.getRawParameterValue(ParamIds::tempoInterpretation);
    const auto barsValue = apvts.getRawParameterValue(ParamIds::bars);
    const auto genreValue = apvts.getRawParameterValue(ParamIds::genre);
    const auto keyRootValue = apvts.getRawParameterValue(ParamIds::keyRoot);
    const auto scaleModeValue = apvts.getRawParameterValue(ParamIds::scaleMode);
    const auto substyleValue = apvts.getRawParameterValue(ParamIds::boombapSubstyle);
    const auto rapSubstyleValue = apvts.getRawParameterValue(ParamIds::rapSubstyle);
    const auto trapSubstyleValue = apvts.getRawParameterValue(ParamIds::trapSubstyle);
    const auto drillSubstyleValue = apvts.getRawParameterValue(ParamIds::drillSubstyle);
    const auto seedValue = apvts.getRawParameterValue(ParamIds::seed);
    const auto seedLockValue = apvts.getRawParameterValue(ParamIds::seedLock);

    p.syncDawTempo = syncValue != nullptr && syncValue->load() > 0.5f;
    p.bpm = bpmValue != nullptr ? bpmValue->load() : 90.0f;
    if (p.syncDawTempo && snapshot.hasHostTempo && snapshot.bpm > 0.0)
        p.bpm = static_cast<float>(snapshot.bpm);

    p.swingPercent = swingValue != nullptr ? swingValue->load() : 56.0f;
    p.velocityAmount = velocityValue != nullptr ? velocityValue->load() : 0.5f;
    p.timingAmount = timingValue != nullptr ? timingValue->load() : 0.4f;
    p.humanizeAmount = humanizeValue != nullptr ? humanizeValue->load() : 0.3f;
    p.densityAmount = densityValue != nullptr ? densityValue->load() : 0.5f;
    p.tempoInterpretationMode = tempoInterpretationValue != nullptr ? static_cast<int>(tempoInterpretationValue->load()) : 0;
    p.bars = barsFromChoiceIndex(barsValue != nullptr ? static_cast<int>(barsValue->load()) : 1);
    p.genre = genreFromChoice(genreValue != nullptr ? static_cast<int>(genreValue->load()) : 0);
    p.keyRoot = keyRootValue != nullptr ? static_cast<int>(keyRootValue->load()) : 0;
    p.scaleMode = scaleModeValue != nullptr ? static_cast<int>(scaleModeValue->load()) : 0;
    p.boombapSubstyle = substyleValue != nullptr ? static_cast<int>(substyleValue->load()) : 0;
    p.rapSubstyle = rapSubstyleValue != nullptr ? static_cast<int>(rapSubstyleValue->load()) : 0;
    p.trapSubstyle = trapSubstyleValue != nullptr ? static_cast<int>(trapSubstyleValue->load()) : 0;
    p.drillSubstyle = drillSubstyleValue != nullptr ? static_cast<int>(drillSubstyleValue->load()) : 0;
    p.seed = seedValue != nullptr ? static_cast<int>(seedValue->load()) : 1;
    p.seedLock = seedLockValue != nullptr && seedLockValue->load() > 0.5f;

    return p;
}

void BoomBapGeneratorAudioProcessor::advanceSeedForGeneration(std::optional<TrackType> trackForRg)
{
    const auto seedLockValue = apvts.getRawParameterValue(ParamIds::seedLock);
    const bool seedLocked = seedLockValue != nullptr && seedLockValue->load() > 0.5f;
    if (seedLocked)
        return;

    const auto seedValue = apvts.getRawParameterValue(ParamIds::seed);
    int baseSeed = seedValue != nullptr ? static_cast<int>(seedValue->load()) : 1;
    baseSeed = juce::jlimit(1, 999999, baseSeed);

    const int delta = trackForRg.has_value()
        ? 7 + static_cast<int>(*trackForRg) * 3
        : 1;
    const int nextSeed = 1 + ((baseSeed - 1 + delta) % 999999);
    setSeedParameterValue(nextSeed);
}

void BoomBapGeneratorAudioProcessor::setSeedParameterValue(int newSeed)
{
    auto* parameter = apvts.getParameter(ParamIds::seed);
    if (parameter == nullptr)
        return;

    const float normalized = parameter->convertTo0to1(static_cast<float>(juce::jlimit(1, 999999, newSeed)));
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalized);
    parameter->endChangeGesture();
}

void BoomBapGeneratorAudioProcessor::setFloatParameterValue(const juce::String& paramId, float value)
{
    auto* parameter = apvts.getParameter(paramId);
    if (parameter == nullptr)
        return;

    const float normalized = parameter->convertTo0to1(value);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalized);
    parameter->endChangeGesture();
}

void BoomBapGeneratorAudioProcessor::rebuildMidiCache()
{
    midiCache = MidiExportEngine::patternToSequence(project, std::nullopt);
    previewEvents.clear();

    juce::MidiMessageSequence normalized;
    for (int i = 0; i < midiCache.getNumEvents(); ++i)
    {
        const auto* e = midiCache.getEventPointer(i);
        if (e == nullptr)
            continue;

        auto message = e->message;
        const int eventTick = static_cast<int>(message.getTimeStamp());
        const int eventSample = ticksToSamples(eventTick, currentSampleRate, project.params.bpm);
        message.setTimeStamp(static_cast<double>(eventSample));
        normalized.addEvent(message);
    }

    normalized.sort();
    normalized.updateMatchedPairs();
    midiCache = normalized;

    for (const auto& track : project.tracks)
    {
        if (!shouldIncludeTrackForPlayback(project, track))
            continue;

        for (const auto& note : track.notes)
        {
            PreviewEvent event;
            const int noteTicks = note.step * ticksPerStep() + note.microOffset;
            event.sample = ticksToSamples(noteTicks, currentSampleRate, project.params.bpm);
            event.track = track.type;

            const float velNorm = juce::jlimit(0.0f, 1.0f, static_cast<float>(note.velocity) / 127.0f);
            const bool snareFamily = track.type == TrackType::Snare || track.type == TrackType::ClapGhostSnare;
            const bool hatFxLane = track.type == TrackType::HatFX;
            const float velGain = std::pow(velNorm, note.isGhost ? 1.0f : 0.95f);
            const float ghostFactor = note.isGhost ? (snareFamily ? 0.62f : (hatFxLane ? 0.85f : 0.45f)) : 1.0f;
            const float laneGain = juce::jlimit(0.0f, 1.5f, track.laneVolume);

            float gain = laneGain * velGain * ghostFactor * 0.95f;
            if (snareFamily)
            {
                const float floor = laneGain * (note.isGhost ? 0.08f : 0.16f);
                gain = std::max(gain, floor);
            }
            else if (hatFxLane)
            {
                const float floor = laneGain * (note.isGhost ? 0.12f : 0.18f);
                gain = std::max(gain, floor);
            }

            event.gain = juce::jlimit(0.0f, 1.0f, gain);
            previewEvents.push_back(event);
        }
    }

    std::sort(previewEvents.begin(), previewEvents.end(), [](const PreviewEvent& a, const PreviewEvent& b)
    {
        return a.sample < b.sample;
    });
}

void BoomBapGeneratorAudioProcessor::applyMasterFx(juce::AudioBuffer<float>& buffer)
{
    const auto* volParam = apvts.getRawParameterValue(ParamIds::masterVolume);
    const auto* compParam = apvts.getRawParameterValue(ParamIds::masterCompressor);
    const auto* lofiParam = apvts.getRawParameterValue(ParamIds::masterLofi);

    const float masterVol = volParam != nullptr ? volParam->load() : 1.0f;
    const float compAmount = compParam != nullptr ? compParam->load() : 0.0f;
    const float lofiAmount = lofiParam != nullptr ? lofiParam->load() : 0.0f;

    buffer.applyGain(juce::jlimit(0.0f, 1.5f, masterVol));

    if (compAmount > 0.001f)
    {
        juce::AudioBuffer<float> dry;
        dry.makeCopyOf(buffer, true);
        previewCompressor.setThreshold(juce::jmap(compAmount, -9.0f, -26.0f));
        previewCompressor.setRatio(juce::jmap(compAmount, 1.4f, 5.5f));

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        previewCompressor.process(context);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* dryData = dry.getReadPointer(ch);
            float* wetData = buffer.getWritePointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                wetData[i] = juce::jmap(compAmount, dryData[i], wetData[i]);
        }
    }

    if (lofiAmount > 0.001f)
    {
        const int holdSamples = 1 + static_cast<int>(lofiAmount * 14.0f);
        const int bitDepth = juce::jlimit(6, 16, 16 - static_cast<int>(lofiAmount * 9.0f));
        const float levels = static_cast<float>(1 << (bitDepth - 1));

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (lofiDownsampleCounter <= 0)
            {
                for (int ch = 0; ch < juce::jmin(2, buffer.getNumChannels()); ++ch)
                {
                    const float s = buffer.getSample(ch, i);
                    lofiHeldSample[static_cast<size_t>(ch)] = std::round(s * levels) / levels;
                }
                lofiDownsampleCounter = holdSamples;
            }

            --lofiDownsampleCounter;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const float dry = buffer.getSample(ch, i);
                const float wet = lofiHeldSample[static_cast<size_t>(juce::jmin(ch, 1))];
                buffer.setSample(ch, i, juce::jmap(lofiAmount, dry, wet));
            }
        }

        previewLofiFilter.setCutoffFrequency(juce::jmap(lofiAmount, 18000.0f, 3600.0f));
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        previewLofiFilter.process(context);
    }
}

int BoomBapGeneratorAudioProcessor::getPatternLengthSamples() const
{
    const int totalSteps = juce::jmax(1, project.params.bars * 16);
    return stepToSamples(totalSteps, currentSampleRate, project.params.bpm);
}

TrackState* BoomBapGeneratorAudioProcessor::findTrackState(TrackType track)
{
    for (auto& state : project.tracks)
    {
        if (state.type == track)
            return &state;
    }

    return nullptr;
}

const TrackState* BoomBapGeneratorAudioProcessor::findTrackState(TrackType track) const
{
    for (const auto& state : project.tracks)
    {
        if (state.type == track)
            return &state;
    }

    return nullptr;
}

TrackState* BoomBapGeneratorAudioProcessor::findTrackState(const RuntimeLaneId& laneId)
{
    for (auto& state : project.tracks)
    {
        if (state.laneId.isNotEmpty() && state.laneId == laneId)
            return &state;
    }

    if (const auto type = resolveTrackTypeForLaneId(laneId); type.has_value())
        return findTrackState(*type);

    return nullptr;
}

const TrackState* BoomBapGeneratorAudioProcessor::findTrackState(const RuntimeLaneId& laneId) const
{
    for (const auto& state : project.tracks)
    {
        if (state.laneId.isNotEmpty() && state.laneId == laneId)
            return &state;
    }

    if (const auto type = resolveTrackTypeForLaneId(laneId); type.has_value())
        return findTrackState(*type);

    return nullptr;
}

std::optional<TrackType> BoomBapGeneratorAudioProcessor::resolveTrackTypeForLaneId(const RuntimeLaneId& laneId) const
{
    const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, laneId);
    if (lane == nullptr || !lane->runtimeTrackType.has_value())
        return std::nullopt;

    return lane->runtimeTrackType;
}

void BoomBapGeneratorAudioProcessor::serializePatternProjectToState(juce::ValueTree& state) const
{
    state.removeChild(state.getChildWithName("PATTERN_PROJECT"), nullptr);
    state.addChild(PatternProjectSerialization::serialize(project), -1, nullptr);
}

void BoomBapGeneratorAudioProcessor::restorePatternProjectFromState(const juce::ValueTree& state)
{
    const auto rootVersion = static_cast<int>(state.getProperty("root_schema_version", 1));
    juce::ignoreUnused(rootVersion);

    PatternProject restored;
    const bool hadPattern = PatternProjectSerialization::deserialize(state, restored);

    if (!hadPattern)
    {
        restored = createDefaultProject();

        // Migration path from older state snapshots that stored only track flags.
        const auto legacyTrackState = state.getChildWithName("TrackState");
        if (legacyTrackState.isValid())
        {
            for (int i = 0; i < legacyTrackState.getNumChildren(); ++i)
            {
                const auto node = legacyTrackState.getChild(i);
                const auto type = static_cast<TrackType>(static_cast<int>(node.getProperty("type", static_cast<int>(TrackType::Kick))));
                auto it = std::find_if(restored.tracks.begin(), restored.tracks.end(), [type](const TrackState& t) { return t.type == type; });
                if (it != restored.tracks.end())
                {
                    it->enabled = static_cast<bool>(node.getProperty("enabled", it->enabled));
                    it->muted = static_cast<bool>(node.getProperty("muted", false));
                    it->solo = static_cast<bool>(node.getProperty("solo", false));
                    it->locked = static_cast<bool>(node.getProperty("locked", false));
                }
            }
        }
    }

    // APVTS remains the parameter source of truth after restore.
    restored.params = buildParamsFromState(lastTransport);
    PatternProjectSerialization::validate(restored);
    project = std::move(restored);
}
} // namespace bbg

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new bbg::BoomBapGeneratorAudioProcessor();
}
