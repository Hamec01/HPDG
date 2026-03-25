#pragma once

#include <array>
#include <mutex>
#include <optional>

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "../Core/PatternProject.h"
#include "../Core/TransportSnapshot.h"
#include "../Engine/BoomBapEngine.h"
#include "../Engine/DrillEngine.h"
#include "../Engine/RapEngine.h"
#include "../Engine/TrapEngine.h"
#include "../Engine/LaneSampleBank.h"
#include "../Engine/PreviewEngine.h"
#include "../Engine/SampleLibraryManager.h"
#include "../Analysis/AudioFeatureMap.h"
#include "../Analysis/SampleAnalysisRequest.h"
#include "../Analysis/SampleAnalysisResult.h"
#include "../Analysis/SampleAnalyzer.h"
#include "../Analysis/SampleAwareGenerationContext.h"

namespace bbg
{
namespace ParamIds
{
static constexpr auto bpm = "bpm";
static constexpr auto bpmLock = "bpm_lock";
static constexpr auto syncDawTempo = "sync_daw_tempo";
static constexpr auto swingPercent = "swing_percent";
static constexpr auto velocityAmount = "velocity_amount";
static constexpr auto timingAmount = "timing_amount";
static constexpr auto humanizeAmount = "humanize_amount";
static constexpr auto densityAmount = "density_amount";
static constexpr auto tempoInterpretation = "tempo_interpretation";
static constexpr auto bars = "bars";
static constexpr auto genre = "genre";
static constexpr auto keyRoot = "key_root";
static constexpr auto scaleMode = "scale_mode";
static constexpr auto boombapSubstyle = "boombap_substyle";
static constexpr auto rapSubstyle = "rap_substyle";
static constexpr auto trapSubstyle = "trap_substyle";
static constexpr auto drillSubstyle = "drill_substyle";
static constexpr auto seed = "seed";
static constexpr auto seedLock = "seed_lock";
static constexpr auto masterVolume = "master_volume";
static constexpr auto masterCompressor = "master_compressor";
static constexpr auto masterLofi = "master_lofi";
} // namespace ParamIds

class BoomBapGeneratorAudioProcessor final : public juce::AudioProcessor
{
public:
    BoomBapGeneratorAudioProcessor();
    ~BoomBapGeneratorAudioProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getApvts() { return apvts; }
    const juce::AudioProcessorValueTreeState& getApvts() const { return apvts; }

    void generatePattern();
    void generateTrackNew(TrackType track);
    void regenerateTrack(TrackType track); // Variation regenerate for RG.
    void regenerateTrack(const RuntimeLaneId& laneId);
    void mutatePattern();
    void mutateTrack(TrackType track);
    void startPreview();
    void stopPreview();
    bool isPreviewPlaying() const;
    float getPreviewPlayheadStep() const;
    void syncBarsFromState();
    void setPreviewStartStep(int step);
    void setPreviewPlaybackMode(PreviewPlaybackMode mode);
    PreviewPlaybackMode getPreviewPlaybackMode() const;
    void restoreEditorProjectSnapshot(const PatternProject& snapshot);
    void setPreviewLoopRegion(const std::optional<juce::Range<int>>& tickRange);
    std::optional<juce::Range<int>> getPreviewLoopRegion() const;
    void setStartPlayWithDawEnabled(bool enabled);
    bool isStartPlayWithDawEnabled() const;

    void rescanLaneSamples();
    bool selectNextLaneSample(TrackType track);
    bool selectNextLaneSample(const RuntimeLaneId& laneId);
    bool selectPreviousLaneSample(TrackType track);
    bool selectPreviousLaneSample(const RuntimeLaneId& laneId);
    juce::String getSelectedLaneSampleName(TrackType track) const;
    int getBassKeyRootChoice() const;
    int getBassScaleModeChoice() const;
    void setBassKeyRootChoice(int choice);
    void setBassScaleModeChoice(int choice);

    void setTrackSolo(TrackType track, bool value);
    void setTrackSolo(const RuntimeLaneId& laneId, bool value);
    void setTrackMuted(TrackType track, bool value);
    void setTrackMuted(const RuntimeLaneId& laneId, bool value);
    void setTrackLocked(TrackType track, bool value);
    void setTrackLocked(const RuntimeLaneId& laneId, bool value);
    void setTrackEnabled(TrackType track, bool value);
    void setTrackEnabled(const RuntimeLaneId& laneId, bool value);
    void setTrackLaneVolume(TrackType track, float volume);
    void setTrackLaneVolume(const RuntimeLaneId& laneId, float volume);
    void setTrackNotes(TrackType track, const std::vector<NoteEvent>& notes);
    void setTrackNotes(const RuntimeLaneId& laneId, const std::vector<NoteEvent>& notes);
    void setSelectedTrack(TrackType track);
    void setSelectedTrack(const RuntimeLaneId& laneId);
    void setSoundModuleTrack(const std::optional<TrackType>& track);
    std::optional<TrackType> getSoundModuleTrack() const;
    void setTrackSoundLayer(TrackType track, const SoundLayerState& state);
    void setTrackSoundLayer(const RuntimeLaneId& laneId, const SoundLayerState& state);
    void setGlobalSoundLayer(const SoundLayerState& state);
    void setHatFxDragDensity(float density, bool lockDragDensity);
    float getHatFxDragDensity() const;
    bool isHatFxDragDensityLocked() const;
    void clearTrack(TrackType track);
    void clearTrack(const RuntimeLaneId& laneId);

    juce::File getLaneSampleDirectory(TrackType track) const;
    bool importLaneSample(TrackType track, const juce::File& sourceFile, juce::String* errorMessage = nullptr);
    bool deleteSelectedLaneSample(TrackType track, juce::String* errorMessage = nullptr);

    bool exportFullPatternToFile(const juce::File& targetFile) const;
    bool exportTrackToFile(TrackType track, const juce::File& targetFile) const;
    bool exportLoopWavToFile(const juce::File& targetFile) const;
    juce::File createTemporaryFullPatternMidiFile() const;
    juce::File createTemporaryTrackMidiFile(TrackType track) const;

    PatternProject getProjectSnapshot() const;
    TransportSnapshot getLastTransportSnapshot() const;
    void applySelectedStylePreset(bool force);

    void setSampleAnalysisRequest(const SampleAnalysisRequest& request);
    SampleAnalysisRequest getSampleAnalysisRequest() const;

    bool analyzeCurrentSampleSource(juce::String* errorMessage = nullptr);
    bool analyzeAudioFile(const juce::File& file, juce::String* errorMessage = nullptr);
    void clearSampleAnalysis();

    void setAnalysisMode(AnalysisMode mode);
    AnalysisMode getAnalysisMode() const;

    void setSampleAwareModeEnabled(bool enabled);
    bool isSampleAwareModeEnabled() const;
    bool isSampleAnalysisReady() const;
    void setSampleReactivity(float value);
    void setSupportVsContrast(float value);

    SampleAnalysisResult getSampleAnalysisResult() const;
    AudioFeatureMap getAudioFeatureMap() const;
    SampleAwareGenerationContext getSampleAwareGenerationContext() const;
    juce::String getGenerationDebugSummary() const;

private:
    struct PreviewEvent
    {
        int sample = 0;
        int endSample = 0;
        TrackType track = TrackType::Kick;
        float gain = 0.8f;
        int pitch = 36;
        bool legato = false;
        bool glide = false;
        juce::String sampleName;
    };

    struct SoundFxRuntimeState
    {
        juce::dsp::StateVariableTPTFilter<float> lowPass;
        juce::dsp::StateVariableTPTFilter<float> highPass;
        juce::dsp::Compressor<float> compressor;
        juce::Reverb reverb;
        float gateEnvelope = 0.0f;
        float transientFast = 0.0f;
        float transientSlow = 0.0f;
        int driveDownsampleCounter = 0;
        std::array<float, 2> driveHeldSample { 0.0f, 0.0f };
    };

    struct MasterFxRuntimeState
    {
        juce::dsp::Compressor<float> compressor;
        juce::dsp::StateVariableTPTFilter<float> lofiFilter;
        int lofiDownsampleCounter = 0;
        std::array<float, 2> lofiHeldSample { 0.0f, 0.0f };
    };

    GeneratorParams buildParamsFromState(const TransportSnapshot& snapshot) const;
    void advanceSeedForGeneration(std::optional<TrackType> trackForRg);
    void setSeedParameterValue(int newSeed);
    void setFloatParameterValue(const juce::String& paramId, float value);
    void rebuildMidiCache();
    void refreshHatFxDragSourceLocked();
    void applyHatFxDragDensityLocked();
    void startPreviewFromCurrentStartStepLocked();
    void rescanLaneSamplesLocked();
    void updateSampleAwareContextLocked();
    void applySampleAwarePostProcessLocked();
    void prepareSoundFxRuntimeState(SoundFxRuntimeState& state);
    void resetSoundFxRuntimeState(SoundFxRuntimeState& state);
    void prepareMasterFxRuntimeState(MasterFxRuntimeState& state);
    void resetMasterFxRuntimeState(MasterFxRuntimeState& state);
    void applySoundLayerFx(juce::AudioBuffer<float>& buffer, const SoundLayerState& state, SoundFxRuntimeState& runtime);
    void applyMasterFx(juce::AudioBuffer<float>& buffer);
    void applyMasterFx(juce::AudioBuffer<float>& buffer, MasterFxRuntimeState& runtime, float masterVol, float compAmount, float lofiAmount) const;
    std::vector<PreviewEvent> buildPreviewEventsForProject(const PatternProject& sourceProject, double sampleRate) const;
    int getPatternLengthSamples() const;
    int getPatternLengthSamples(const PatternProject& sourceProject, double sampleRate) const;
    SoundLayerState sanitizeSoundLayer(const SoundLayerState& state) const;
    float computePreviewEventGain(const TrackState& track, const NoteEvent& note) const;
    void syncSelectedSampleStateForTrackType(TrackType track);
    TrackState* findTrackState(TrackType track);
    const TrackState* findTrackState(TrackType track) const;
    TrackState* findTrackState(const RuntimeLaneId& laneId);
    const TrackState* findTrackState(const RuntimeLaneId& laneId) const;
    std::optional<TrackType> resolveTrackTypeForLaneId(const RuntimeLaneId& laneId) const;
    void serializePatternProjectToState(juce::ValueTree& state) const;
    void restorePatternProjectFromState(const juce::ValueTree& state);

    mutable std::mutex projectMutex;
    PatternProject project;
    BoomBapEngine boomBapEngine;
    RapEngine rapEngine;
    TrapEngine trapEngine;
    DrillEngine drillEngine;
    juce::AudioProcessorValueTreeState apvts;

    juce::MidiMessageSequence midiCache;
    TransportSnapshot lastTransport;
    PreviewEngine previewEngine;
    SampleLibraryManager sampleLibraryManager;
    LaneSampleBank laneSampleBank;
    juce::dsp::Compressor<float> previewCompressor;
    juce::dsp::StateVariableTPTFilter<float> previewLofiFilter;
    int lofiDownsampleCounter = 0;
    std::array<float, 2> lofiHeldSample { 0.0f, 0.0f };
    std::array<juce::AudioBuffer<float>, 11> previewLaneBuffers;
    std::array<SoundFxRuntimeState, 11> laneFxRuntimeStates;
    SoundFxRuntimeState globalFxRuntimeState;
    MasterFxRuntimeState masterFxRuntimeState;
    std::vector<PreviewEvent> previewEvents;
    bool previewPlaying = false;
    int previewSamplePosition = 0;
    bool startPlayWithDawEnabled = false;
    bool lastObservedHostPlaying = false;

    double currentSampleRate = 44100.0;
    int transportSamplePosition = 0;

    int lastAppliedGenreChoice = -1;
    int lastAppliedBoomBapSubstyleChoice = -1;
    int lastAppliedRapSubstyleChoice = -1;
    int lastAppliedTrapSubstyleChoice = -1;
    int lastAppliedDrillSubstyleChoice = -1;

    float hatFxDragDensity = 1.0f;
    bool hatFxDragDensityLocked = false;
    int hatFxDragSourceGenerationCounter = -1;
    std::vector<NoteEvent> hatFxDragSourceNotes;

    SampleAnalyzer sampleAnalyzer;
    SampleAnalysisRequest currentAnalysisRequest;
    SampleAnalysisResult currentAnalysisResult;
    AudioFeatureMap currentFeatureMap;
    SampleAwareGenerationContext currentSampleContext;
    AnalysisMode analysisMode = AnalysisMode::Off;

    bool sampleAwareModeEnabled = false;
    bool analysisReady = false;

    juce::AudioBuffer<float> liveCaptureBuffer;
    bool isCapturingInput = false;
    int capturedSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoomBapGeneratorAudioProcessor)
};
} // namespace bbg
