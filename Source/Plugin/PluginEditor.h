#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"
#include "../UI/GridEditorComponent.h"
#include "../UI/MainHeaderComponent.h"
#include "../UI/TrackListComponent.h"

namespace bbg
{
class BoomBGeneratorAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    explicit BoomBGeneratorAudioProcessorEditor(BoomBapGeneratorAudioProcessor& processor);
    ~BoomBGeneratorAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshFromProcessor(bool refreshTrackRows = true);
    void setupAttachments();
    void refreshSubstyleBindingForGenre();
    void bindTrackCallbacks();
    void exportFullPattern();
    void dragFullPatternTempMidi();
    void dragFullPatternExternal();
    void exportTrack(TrackType type);
    void dragTrackTempMidi(TrackType type);
    void dragTrackExternal(TrackType type);
    void updateGridGeometry();
    bool isStandaloneWindowMaximized() const;
    void toggleStandaloneWindowMaximize();

    BoomBapGeneratorAudioProcessor& audioProcessor;

    MainHeaderComponent header;
    TrackListComponent trackList;
    juce::Viewport gridViewport;
    GridEditorComponent grid;
    float horizontalZoom = 1.0f;
    int laneHeight = 30;
    int lastGenerationCounter = -1;
    juce::Rectangle<int> standaloneRestoreBounds;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> bpmAttachment;
    std::unique_ptr<ButtonAttachment> syncAttachment;
    std::unique_ptr<SliderAttachment> swingAttachment;
    std::unique_ptr<SliderAttachment> velocityAttachment;
    std::unique_ptr<SliderAttachment> timingAttachment;
    std::unique_ptr<SliderAttachment> humanizeAttachment;
    std::unique_ptr<SliderAttachment> densityAttachment;
    std::unique_ptr<ComboAttachment> tempoInterpretationAttachment;
    std::unique_ptr<ComboAttachment> barsAttachment;
    std::unique_ptr<ComboAttachment> genreAttachment;
    std::unique_ptr<ComboAttachment> substyleAttachment;
    int lastGenreChoice = -1;
    std::unique_ptr<SliderAttachment> seedAttachment;
    std::unique_ptr<ButtonAttachment> seedLockAttachment;
    std::unique_ptr<SliderAttachment> masterVolumeAttachment;
    std::unique_ptr<SliderAttachment> masterCompressorAttachment;
    std::unique_ptr<SliderAttachment> masterLofiAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoomBGeneratorAudioProcessorEditor)
};
} // namespace bbg
