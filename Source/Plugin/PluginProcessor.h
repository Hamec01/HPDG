#pragma once

#include <array>
#include <mutex>
#include <optional>

#include <juce_dsp/juce_dsp.h>
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

namespace bbg
{
namespace ParamIds
{
static constexpr auto bpm = "bpm";
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
    void mutatePattern();
    void mutateTrack(TrackType track);
    void startPreview();
    void stopPreview();
    bool isPreviewPlaying() const;
    float getPreviewPlayheadStep() const;
    void setPreviewStartStep(int step);

    void rescanLaneSamples();
    bool selectNextLaneSample(TrackType track);
    bool selectPreviousLaneSample(TrackType track);
    juce::String getSelectedLaneSampleName(TrackType track) const;
    int getBassKeyRootChoice() const;
    int getBassScaleModeChoice() const;
    void setBassKeyRootChoice(int choice);
    void setBassScaleModeChoice(int choice);

    void setTrackSolo(TrackType track, bool value);
    void setTrackMuted(TrackType track, bool value);
    void setTrackLocked(TrackType track, bool value);
    void setTrackEnabled(TrackType track, bool value);
    void setTrackLaneVolume(TrackType track, float volume);
    void setTrackNotes(TrackType track, const std::vector<NoteEvent>& notes);
    void clearTrack(TrackType track);

    bool exportFullPatternToFile(const juce::File& targetFile) const;
    bool exportTrackToFile(TrackType track, const juce::File& targetFile) const;
    juce::File createTemporaryFullPatternMidiFile() const;
    juce::File createTemporaryTrackMidiFile(TrackType track) const;

    PatternProject getProjectSnapshot() const;
    TransportSnapshot getLastTransportSnapshot() const;
    void applySelectedStylePreset(bool force);

private:
    struct PreviewEvent
    {
        int sample = 0;
        TrackType track = TrackType::Kick;
        float gain = 0.8f;
    };

    GeneratorParams buildParamsFromState(const TransportSnapshot& snapshot) const;
    void advanceSeedForGeneration(std::optional<TrackType> trackForRg);
    void setSeedParameterValue(int newSeed);
    void setFloatParameterValue(const juce::String& paramId, float value);
    void rebuildMidiCache();
    void rescanLaneSamplesLocked();
    void applyMasterFx(juce::AudioBuffer<float>& buffer);
    int getPatternLengthSamples() const;
    TrackState* findTrackState(TrackType track);
    const TrackState* findTrackState(TrackType track) const;
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
    std::vector<PreviewEvent> previewEvents;
    bool previewPlaying = false;
    int previewSamplePosition = 0;

    double currentSampleRate = 44100.0;
    int transportSamplePosition = 0;

    juce::dsp::Compressor<float> previewCompressor;
    juce::dsp::StateVariableTPTFilter<float> previewLofiFilter;
    int lofiDownsampleCounter = 0;
    std::array<float, 2> lofiHeldSample { 0.0f, 0.0f };

    int lastAppliedGenreChoice = -1;
    int lastAppliedBoomBapSubstyleChoice = -1;
    int lastAppliedRapSubstyleChoice = -1;
    int lastAppliedTrapSubstyleChoice = -1;
    int lastAppliedDrillSubstyleChoice = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoomBapGeneratorAudioProcessor)
};
} // namespace bbg
