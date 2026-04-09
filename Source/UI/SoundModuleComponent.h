#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/EqDisplayAnalyzerState.h"
#include "../Core/SoundLayerState.h"
#include "../Core/SoundTargetDescriptor.h"

namespace bbg
{
struct SoundModuleTargetOption
{
    juce::String displayName;
    SoundTargetDescriptor descriptor;
};

struct SoundModuleViewState
{
    std::vector<SoundModuleTargetOption> targetOptions;
    SoundTargetDescriptor selectedTarget;
    SoundLayerState soundState;
};

class SoundModuleComponent : public juce::Component,
                             private juce::ScrollBar::Listener
{
public:
    SoundModuleComponent();
    ~SoundModuleComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    void setState(const SoundModuleViewState& state);
    void setEqDisplayAnalyzerState(const EqDisplayAnalyzerState& state);

    std::function<void(const SoundTargetDescriptor&)> onSoundTargetChanged;
    std::function<void(const SoundTargetDescriptor&, const SoundLayerState&)> onSoundLayerChanged;
    std::function<void()> onSoundLayerGestureStarted;
    std::function<void()> onSoundLayerGestureEnded;

private:
    class RotaryDial : public juce::Slider
    {
    public:
        RotaryDial();

        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
        bool isPointOverActiveZone(juce::Point<float> position) const;

    private:
        juce::Rectangle<float> getInteractiveKnobBounds() const;
        bool isPointOverKnob(juce::Point<float> position) const;
    };

    enum class LayoutMode
    {
        Overview,
        FocusComp,
        FocusReverb,
        FocusMonsta,
        FocusTransient,
        FocusEq,
        FocusStereo
    };

    enum class OverviewCard
    {
        None,
        Eq,
        Comp,
        Stereo,
        Reverb,
        MonstaFx,
        Transient
    };

    struct ModuleCardLayout
    {
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> headerBounds;
        juce::Rectangle<int> contentBounds;
    };

    struct CompressorUiState
    {
        double ratio = 3.2;
        double threshold = -20.0;
        double mix = 58.0;
        double attack = 14.0;
        double release = 145.0;
        double saturation = 18.0;
    };

    struct ReverbUiState
    {
        double size = 32.0;
        double mix = 0.0;
        double predelay = 14.0;
        double tail = 40.0;
    };

    struct MonstaUiState
    {
        double dry = 100.0;
        double wet = 0.0;
    };

    struct TransientUiState
    {
        double attack = 0.0;
        double sustain = 25.0;
        double gain = 0.0;
    };

    juce::Label titleLabel;
    juce::Label brandLabel;
    juce::Label targetLabel;
    juce::Label targetSummaryLabel;
    juce::Label targetModeLabel;
    juce::Label targetStatusLabel;
    juce::Label chainSummaryLabel;
    juce::ComboBox targetCombo;
    juce::TextButton bypassButton { "BYPASS" };
    juce::TextButton resetButton { "RESET" };

    juce::Label eqSectionLabel;
    juce::Label eqDescriptorLabel;
    juce::Label eqBandMeaningLabel;
    juce::Label eqBandRangeLabel;
    juce::Label eqOrderLabel;
    juce::Label eqBandLabel;
    juce::Label eqShapeLabel;
    juce::Label eqFreqLabel;
    juce::Label eqFreqValueLabel;
    juce::Label eqGainLabel;
    juce::Label eqGainValueLabel;
    juce::Label eqQLabel;
    juce::Label eqQValueLabel;
    juce::ComboBox eqOrderCombo;
    juce::ComboBox eqBandCombo;
    juce::ComboBox eqShapeCombo;
    juce::TextButton eqEnableButton { "ACTIVE" };
    RotaryDial eqFreqSlider;
    RotaryDial eqGainSlider;
    RotaryDial eqQSlider;

    juce::Label stereoSectionLabel;
    juce::Label stereoDescriptorLabel;
    juce::Label stereoInputLabel;
    juce::Label stereoOutputLabel;
    juce::Label panLabel;
    juce::Label panValueLabel;
    juce::Label widthLabel;
    juce::Label widthValueLabel;
    juce::Label stereoFocusLabel;
    juce::Label stereoFocusValueLabel;
    juce::Label stereoEdgeLabel;
    juce::Label stereoEdgeValueLabel;
    juce::Label stereoLowCenterProtectLabel;
    juce::Label stereoLowCenterProtectValueLabel;
    juce::Label stereoAirSpreadLabel;
    juce::Label stereoAirSpreadValueLabel;
    RotaryDial panSlider;
    RotaryDial widthSlider;
    RotaryDial stereoFocusSlider;
    RotaryDial stereoEdgeSlider;
    RotaryDial stereoLowCenterProtectSlider;
    RotaryDial stereoAirSpreadSlider;
    juce::TextButton stereoMonoSafeButton { "MONO SAFE" };

    juce::Label compSectionLabel;
    juce::Label compDescriptorLabel;
    juce::Label compOrderLabel;
    juce::Label compRatioLabel;
    juce::Label compRatioValueLabel;
    juce::Label compThresholdLabel;
    juce::Label compThresholdValueLabel;
    juce::Label compMixLabel;
    juce::Label compMixValueLabel;
    juce::Label compAttackLabel;
    juce::Label compAttackValueLabel;
    juce::Label compReleaseLabel;
    juce::Label compReleaseValueLabel;
    juce::Label compSaturationLabel;
    juce::Label compSaturationValueLabel;
    juce::ComboBox compOrderCombo;
    juce::TextButton compPowerButton { "POWER" };
    RotaryDial compRatioSlider;
    RotaryDial compThresholdSlider;
    RotaryDial compMixSlider;
    RotaryDial compAttackSlider;
    RotaryDial compReleaseSlider;
    RotaryDial compSaturationSlider;

    juce::Label reverbSectionLabel;
    juce::Label reverbDescriptorLabel;
    juce::Label reverbSizeLabel;
    juce::Label reverbSizeValueLabel;
    juce::Label reverbMixLabel;
    juce::Label reverbMixValueLabel;
    juce::Label reverbPredelayLabel;
    juce::Label reverbPredelayValueLabel;
    juce::Label reverbTailLabel;
    juce::Label reverbTailValueLabel;
    RotaryDial reverbSizeSlider;
    RotaryDial reverbMixSlider;
    RotaryDial reverbPredelaySlider;
    RotaryDial reverbTailSlider;

    juce::Label monstaSectionLabel;
    juce::Label monstaDescriptorLabel;
    juce::Label monstaDryLabel;
    juce::Label monstaDryValueLabel;
    juce::Label monstaWetLabel;
    juce::Label monstaWetValueLabel;
    RotaryDial monstaDrySlider;
    RotaryDial monstaWetSlider;
    juce::TextButton monstaChaosButton { "CHAOS" };

    juce::Label transientSectionLabel;
    juce::Label transientDescriptorLabel;
    juce::Label transientAttackLabel;
    juce::Label transientAttackValueLabel;
    juce::Label transientSustainLabel;
    juce::Label transientSustainValueLabel;
    juce::Label transientGainLabel;
    juce::Label transientGainValueLabel;
    RotaryDial transientAttackSlider;
    RotaryDial transientSustainSlider;
    RotaryDial transientGainSlider;
    juce::TextButton smoothButton { "SMOOTH" };
    juce::TextButton limitButton { "LIMIT" };

    juce::TextButton compStripButton { "COMP" };
    juce::TextButton reverbStripButton { "REVERB" };
    juce::TextButton monstaStripButton { "MONSTA" };
    juce::TextButton transientStripButton { "TRANSIENT" };
    juce::TextButton eqStripButton { "EQ" };
    juce::TextButton stereoStripButton { "STEREO" };
    juce::ScrollBar verticalScrollBar { true };
    juce::Component contentViewport;
    juce::Component contentCanvas;

    std::vector<SoundTargetDescriptor> targetDescriptors;
    SoundTargetDescriptor currentTarget;
    SoundLayerState currentSoundState;
    bool targetAvailable = true;
    LayoutMode layoutMode = LayoutMode::Overview;
    OverviewCard expandedOverviewCard = OverviewCard::None;
    CompressorUiState compressorUiState;
    ReverbUiState reverbUiState;
    MonstaUiState monstaUiState;
    TransientUiState transientUiState;
    bool isSyncingUi = false;

    juce::Rectangle<int> topBarBounds;
    juce::Rectangle<int> eqBounds;
    juce::Rectangle<int> stereoBounds;
    juce::Rectangle<int> compBounds;
    juce::Rectangle<int> reverbBounds;
    juce::Rectangle<int> monstaBounds;
    juce::Rectangle<int> transientBounds;
    ModuleCardLayout eqCardLayout;
    ModuleCardLayout stereoCardLayout;
    ModuleCardLayout compCardLayout;
    ModuleCardLayout reverbCardLayout;
    ModuleCardLayout monstaCardLayout;
    ModuleCardLayout transientCardLayout;
    juce::Rectangle<int> moduleStripBounds;
    juce::Rectangle<int> eqDisplayBounds;
    juce::Rectangle<int> stereoDisplayBounds;
    juce::Rectangle<int> transientDisplayBounds;
    juce::Rectangle<int> contentViewportBounds;
    int contentHeight = 0;
    int scrollOffsetY = 0;
    EqDisplayAnalyzerState eqDisplayAnalyzerState;
    int hoveredEqBandIndex = -1;
    int draggedEqBandIndex = -1;
    bool eqBandDragGestureActive = false;

    void setupDial(juce::Slider& slider,
                   double min,
                   double max,
                   double step,
                   juce::Colour accent,
                   double skewMidPoint = 0.0);
    void setupCombo(juce::ComboBox& combo);
    void setupActionButton(juce::TextButton& button, bool prominent);
    void setupStripButton(juce::TextButton& button, bool selected);
    void toggleLayoutMode(LayoutMode requestedMode);
    bool isOverviewCardExpanded(OverviewCard card) const;
    void toggleOverviewCard(OverviewCard card);
    bool isOverviewInteractiveChildHit(const juce::MouseEvent& event) const;
    OverviewCard findOverviewCardAt(juce::Point<int> position) const;
    OverviewCard findOverviewHeaderAt(juce::Point<int> position) const;
    juce::Point<float> toContentCanvasSpace(juce::Point<float> position) const;
    juce::Rectangle<int> toDisplaySpace(const juce::Rectangle<int>& canvasBounds) const;
    void updateStripSelection();
    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;
    void setScrollOffset(int newOffset);
    void setControlsEnabled(bool shouldEnable);
    void updateTargetPresentation(const SoundTargetDescriptor& selectedTarget,
                                  bool targetMatchedInCombo,
                                  const juce::String& matchedTargetName);
    void updateChainSummary();
    void updateValueLabels();
    int currentEqBandIndex() const;
    int findEqDisplayBandAt(juce::Point<float> position) const;
    void updateDraggedEqBandFromPosition(juce::Point<float> position);
    void setHoveredEqBandIndex(int newHoveredBandIndex);
    void updateEqBandUiFromSelection();
    void commitEqBandControlsToState();
    void syncVisualStateFromCurrentSoundState();
    void syncCompressorVisuals();
    void updateCompressorDescriptor();
    void ensureCompressorDefaultsForActivation();
    void applyCompressorUiToState();
    void syncReverbVisuals();
    void syncMonstaFxVisuals();
    void syncTransientVisuals();
    void handleCompressionAmountChange(float normalizedValue);
    void handleGateAmountChange(float normalizedValue);
    void handleDriveAmountChange(float normalizedValue);
    void handleReverbAmountChange(float normalizedValue);
    void handleTransientAmountChange(float normalizedValue);
    void resetCurrentState();
    void emitSoundLayerChange();
};
} // namespace bbg
