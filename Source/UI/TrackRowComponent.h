#pragma once

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/TrackInfo.h"
#include "../Core/RuntimeLaneProfile.h"
#include "../Core/TrackState.h"
#include "DragGestureButton.h"
#include "HardwareKnob.h"

namespace bbg
{
class ClickableLabel : public juce::Label
{
public:
    std::function<void(const juce::MouseEvent&)> onMouseDownEvent;

    void mouseDown(const juce::MouseEvent& event) override
    {
        juce::Label::mouseDown(event);
        if (onMouseDownEvent)
            onMouseDownEvent(event);
    }
};

class ContextActionButton : public juce::TextButton
{
public:
    explicit ContextActionButton(const juce::String& text = {})
        : juce::TextButton(text)
    {
    }

    std::function<void(const juce::MouseEvent&)> onContextMouseDown;

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isRightButtonDown())
        {
            if (onContextMouseDown)
                onContextMouseDown(event);
            return;
        }

        juce::TextButton::mouseDown(event);
    }
};

enum class LaneRackDisplayMode
{
    Full,
    Compact,
    Minimal
};

enum class RackVisualStyle
{
    Default,
    HpdgSoundVst3
};

struct RuntimeLaneRowState
{
    RuntimeLaneId laneId;
    juce::String laneName;
    juce::String groupName;
    juce::String dependencyName;
    int generationPriority = 50;
    bool isCore = true;
    bool isVisibleInEditor = true;
    bool enabled = true;
    bool muted = false;
    bool solo = false;
    bool locked = false;
    bool supportsDragExport = true;
    bool isGhostTrack = false;
    std::optional<TrackType> runtimeTrackType;
    float laneVolume = 0.85f;
    SoundLayerState sound;
    juce::String selectedSampleName;
    Sub808LaneSettings sub808Settings;
};

class TrackRowComponent : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    explicit TrackRowComponent(const RuntimeLaneRowState& initialState);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    void syncFromState(const RuntimeLaneRowState& state);
    void setDisplayMode(LaneRackDisplayMode mode);
    void setVisualStyle(RackVisualStyle style);

    std::function<void(const RuntimeLaneId&)> onRegenerate;
    std::function<void(const RuntimeLaneId&)> onMutate;
    std::function<void(const RuntimeLaneId&)> onImportMidiToLane;
    std::function<void(const RuntimeLaneId&, bool)> onSoloChanged;
    std::function<void(const RuntimeLaneId&, bool)> onMuteChanged;
    std::function<void(const RuntimeLaneId&)> onClear;
    std::function<void(const RuntimeLaneId&, bool)> onLockChanged;
    std::function<void(const RuntimeLaneId&, bool)> onEnableChanged;
    std::function<void(const RuntimeLaneId&)> onDrag;
    std::function<void(const RuntimeLaneId&)> onDragGesture;
    std::function<void(const RuntimeLaneId&, float)> onDragDensityChanged;
    std::function<void(const RuntimeLaneId&, bool)> onDragDensityLockChanged;
    std::function<void(const RuntimeLaneId&)> onExport;
    std::function<void(const RuntimeLaneId&)> onPrevSample;
    std::function<void(const RuntimeLaneId&)> onNextSample;
    std::function<void(const RuntimeLaneId&)> onSampleMenuRequested;
    std::function<void(const RuntimeLaneId&)> onTrackNameClicked;
    std::function<void(const RuntimeLaneId&)> onRenameLaneRequested;
    std::function<void(const RuntimeLaneId&)> onDeleteLaneRequested;
    std::function<void(const RuntimeLaneId&, float)> onLaneVolumeChanged;
    std::function<void(const RuntimeLaneId&, const SoundLayerState&)> onLaneSoundChanged;
    std::function<void(const RuntimeLaneId&)> onMoveLaneUp;
    std::function<void(const RuntimeLaneId&)> onMoveLaneDown;
    std::function<void(int)> onBassKeyChanged;
    std::function<void(int)> onBassScaleChanged;
    std::function<void(const RuntimeLaneId&, const Sub808LaneSettings&)> onSub808SettingsChanged;

    void setBassControls(int keyRootChoice, int scaleModeChoice);
    void setSub808Settings(const Sub808LaneSettings& settings);
    void setHatFxDragState(float density, bool locked);
    const RuntimeLaneId& getLaneId() const { return laneId; }
    std::optional<TrackType> getRuntimeTrackType() const { return runtimeTrackType; }
    bool isBackedBy(TrackType type) const { return runtimeTrackType.has_value() && *runtimeTrackType == type; }

private:
    static juce::String roleLabelForLane(const RuntimeLaneRowState& state);
    static juce::String helperBadgeTextForLane(const RuntimeLaneRowState& state);
    static juce::String tooltipTextForLane(const RuntimeLaneRowState& state);
    void applyIdentityVisualStyle();
    void updateParameterValueLabels();
    void showOverflowMenu();
    void applyDisplayModeVisibility();
    bool hasRuntimeTrack() const { return runtimeTrackType.has_value(); }
    bool isSub808Lane() const { return runtimeTrackType.has_value() && *runtimeTrackType == TrackType::Sub808; }
    bool isHatFxLane() const { return runtimeTrackType.has_value() && *runtimeTrackType == TrackType::HatFX; }

    RuntimeLaneId laneId;
    std::optional<TrackType> runtimeTrackType;
    juce::String groupName;
    juce::String dependencyName;
    int generationPriority = 50;
    bool isCore = true;
    bool supportsDragExport = true;
    bool isGhostTrack = false;
    bool helperLaneUi = false;
    bool explicitDependencyUi = false;
    juce::String helperBadgeText;
    LaneRackDisplayMode displayMode = LaneRackDisplayMode::Full;
    RackVisualStyle visualStyle = RackVisualStyle::Default;

    ClickableLabel nameLabel;
    juce::Label roleLabel;
    ContextActionButton rgButton { "RG" };
    juce::ToggleButton soloButton { "S" };
    juce::ToggleButton muteButton { "M" };
    juce::TextButton clearButton { "C" };
    juce::ToggleButton lockButton { "L" };
    juce::ToggleButton enableButton { "E" };
    juce::Label volumeLabel;
    juce::Label volumeValueLabel;
    RotaryKnobSlider volumeSlider;
    juce::Label panLabel;
    juce::Label panValueLabel;
    RotaryKnobSlider panSlider;
    juce::Label widthLabel;
    juce::Label widthValueLabel;
    juce::Slider widthSlider;
    juce::TextButton prevSampleButton { "<" };
    ClickableLabel sampleNameLabel;
    juce::TextButton nextSampleButton { ">" };
    juce::Label bassKeyLabel;
    juce::ComboBox bassKeyCombo;
    juce::Label bassScaleLabel;
    juce::ComboBox bassScaleCombo;
    DragGestureButton dragButton { "Drag" };
    juce::Slider dragDensitySlider;
    juce::Label dragDensityLabel;
    juce::Label dragDensityValueLabel;
    juce::ToggleButton dragDensityLockButton { "Lk" };
    juce::TextButton exportButton { "Export" };
    juce::TextButton overflowMenuButton { "..." };
    float dragDensityValue = 1.0f;
    bool dragDensityLocked = false;
    int currentBassKeyChoice = 0;
    int currentBassScaleChoice = 0;
    Sub808LaneSettings currentSub808Settings;
    SoundLayerState currentSoundState;
};
} // namespace bbg
