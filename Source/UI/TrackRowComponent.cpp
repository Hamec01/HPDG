#include "TrackRowComponent.h"

#include <array>
#include <cmath>

namespace bbg
{
namespace
{
double panUiFromState(float pan)
{
    return juce::jlimit(-100.0, 100.0, static_cast<double>(pan) * 100.0);
}

float panStateFromUi(double panUi)
{
    return juce::jlimit(-1.0f, 1.0f, static_cast<float>(panUi / 100.0));
}

double widthUiFromState(float width)
{
    return juce::jlimit(0.0, 100.0, static_cast<double>(width) * 100.0);
}

float widthStateFromUi(double widthUi)
{
    return juce::jlimit(0.0f, 1.0f, static_cast<float>(widthUi / 100.0));
}

juce::String formatVolumeValue(double value)
{
    return juce::String(value, 2) + "x";
}

juce::String formatPanValue(double value)
{
    return juce::String(static_cast<int>(std::round(value)));
}

juce::String formatWidthValue(double value)
{
    return juce::String(static_cast<int>(std::round(value))) + "%";
}

bool isGhostHelperLane(const RuntimeLaneRowState& state)
{
    if (state.runtimeTrackType.has_value())
    {
        switch (*state.runtimeTrackType)
        {
            case TrackType::GhostKick:
            case TrackType::ClapGhostSnare:
                return true;
            default: break;
        }
    }

    return state.isGhostTrack;
}

bool isHatFxHelperLane(const RuntimeLaneRowState& state)
{
    return state.runtimeTrackType.has_value() && *state.runtimeTrackType == TrackType::HatFX;
}

bool isHelperLaneVisual(const RuntimeLaneRowState& state)
{
    return isGhostHelperLane(state)
        || isHatFxHelperLane(state)
        || state.dependencyName.isNotEmpty()
        || !state.isCore;
}

bool hasExplicitDependencyRelation(const RuntimeLaneRowState& state)
{
    return state.dependencyName.isNotEmpty();
}

juce::String helperBadgeTextForState(const RuntimeLaneRowState& state)
{
    if (isGhostHelperLane(state))
        return "Ghost";

    if (isHatFxHelperLane(state))
        return "Hat FX";

    if (state.dependencyName.isNotEmpty())
        return juce::String::fromUTF8("\xE2\x86\xB3 ") + state.dependencyName;

    if (!state.isCore)
        return "Helper";

    return {};
}

juce::String laneRolePhraseForLane(const RuntimeLaneRowState& state)
{
    if (isGhostHelperLane(state))
    {
        if (state.dependencyName.isNotEmpty())
            return "Ghost support for " + state.dependencyName;
        return "Ghost support lane";
    }

    if (isHatFxHelperLane(state))
    {
        if (state.dependencyName.isNotEmpty())
            return "Hat FX support for " + state.dependencyName;
        return "Hat FX helper lane";
    }

    if (state.dependencyName.isNotEmpty())
        return "Derived from " + state.dependencyName;

    if (!state.isCore)
        return "Helper lane";

    return "Main lane";
}

juce::String laneTooltipPhraseForLane(const RuntimeLaneRowState& state)
{
    if (isGhostHelperLane(state))
    {
        if (state.dependencyName.isNotEmpty())
            return "Ghost support lane for " + state.dependencyName;
        return "Ghost helper lane";
    }

    if (isHatFxHelperLane(state))
    {
        if (state.dependencyName.isNotEmpty())
            return "Hat FX support layer for " + state.dependencyName;
        return "Hat FX helper lane";
    }

    if (state.dependencyName.isNotEmpty())
        return "Helper lane derived from " + state.dependencyName;

    if (!state.isCore)
        return "Helper / dependent lane";

    return "Main rhythmic lane";
}

float textWidthForFont(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox(0, glyphs.getNumGlyphs(), true).getWidth();
}
}

TrackRowComponent::TrackRowComponent(const RuntimeLaneRowState& initialState)
    : laneId(initialState.laneId)
    , runtimeTrackType(initialState.runtimeTrackType)
    , groupName(initialState.groupName)
    , dependencyName(initialState.dependencyName)
    , generationPriority(initialState.generationPriority)
    , isCore(initialState.isCore)
    , supportsDragExport(initialState.supportsDragExport)
    , isGhostTrack(initialState.isGhostTrack)
{
    helperLaneUi = isHelperLaneVisual(initialState);
    explicitDependencyUi = hasExplicitDependencyRelation(initialState);

    nameLabel.setText(initialState.laneName, juce::dontSendNotification);
    nameLabel.setJustificationType(juce::Justification::centredLeft);
    nameLabel.setInterceptsMouseClicks(true, false);
    nameLabel.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    nameLabel.onMouseDownEvent = [this](const juce::MouseEvent& event)
    {
        if (event.mods.isRightButtonDown())
            return;

        if (onTrackNameClicked)
            onTrackNameClicked(laneId);
    };
    addAndMakeVisible(nameLabel);

    roleLabel.setText(roleLabelForLane(initialState), juce::dontSendNotification);
    roleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(roleLabel);
    helperBadgeText = helperBadgeTextForLane(initialState);
    applyIdentityVisualStyle();

    addAndMakeVisible(rgButton);
    addAndMakeVisible(soloButton);
    addAndMakeVisible(muteButton);
    addAndMakeVisible(clearButton);
    addAndMakeVisible(lockButton);
    addAndMakeVisible(enableButton);
    addAndMakeVisible(volumeLabel);
    addAndMakeVisible(volumeValueLabel);
    addAndMakeVisible(volumeSlider);
    addAndMakeVisible(panLabel);
    addAndMakeVisible(panValueLabel);
    addAndMakeVisible(panSlider);
    addAndMakeVisible(widthLabel);
    addAndMakeVisible(widthValueLabel);
    addAndMakeVisible(widthSlider);
    addAndMakeVisible(prevSampleButton);
    addAndMakeVisible(sampleNameLabel);
    addAndMakeVisible(nextSampleButton);
    addAndMakeVisible(bassKeyLabel);
    addAndMakeVisible(bassKeyCombo);
    addAndMakeVisible(bassScaleLabel);
    addAndMakeVisible(bassScaleCombo);
    addAndMakeVisible(dragButton);
    addAndMakeVisible(dragDensityLabel);
    addAndMakeVisible(dragDensitySlider);
    addAndMakeVisible(dragDensityValueLabel);
    addAndMakeVisible(dragDensityLockButton);
    addAndMakeVisible(exportButton);
    addAndMakeVisible(overflowMenuButton);

    rgButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(96, 120, 154));
    clearButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(74, 82, 98));
    dragButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(84, 112, 168));
    dragDensityLabel.setText("HFX", juce::dontSendNotification);
    dragDensityLabel.setJustificationType(juce::Justification::centredLeft);
    dragDensityLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(188, 196, 210));
    dragDensityValueLabel.setJustificationType(juce::Justification::centredLeft);
    dragDensityValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(172, 210, 255));
    dragDensityLockButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(188, 196, 210));
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(86, 106, 146));
    prevSampleButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(58, 64, 76));
    nextSampleButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(58, 64, 76));

    sampleNameLabel.setJustificationType(juce::Justification::centred);
    sampleNameLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 221, 232));
    sampleNameLabel.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    sampleNameLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGBA(255, 255, 255, 8));
    sampleNameLabel.setText("(none)", juce::dontSendNotification);
    sampleNameLabel.setInterceptsMouseClicks(true, false);
    sampleNameLabel.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    sampleNameLabel.onMouseDownEvent = [this](const juce::MouseEvent& event)
    {
        if (!event.mods.isRightButtonDown())
            return;

        if (onSampleMenuRequested)
            onSampleMenuRequested(laneId);
    };

    bassKeyLabel.setText("Key", juce::dontSendNotification);
    bassKeyLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 168, 182));
    bassScaleLabel.setText("Scale", juce::dontSendNotification);
    bassScaleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 168, 182));

    bassKeyCombo.addItem("C", 1);
    bassKeyCombo.addItem("C#", 2);
    bassKeyCombo.addItem("D", 3);
    bassKeyCombo.addItem("D#", 4);
    bassKeyCombo.addItem("E", 5);
    bassKeyCombo.addItem("F", 6);
    bassKeyCombo.addItem("F#", 7);
    bassKeyCombo.addItem("G", 8);
    bassKeyCombo.addItem("G#", 9);
    bassKeyCombo.addItem("A", 10);
    bassKeyCombo.addItem("A#", 11);
    bassKeyCombo.addItem("B", 12);

    bassScaleCombo.addItem("Minor", 1);
    bassScaleCombo.addItem("Major", 2);
    bassScaleCombo.addItem("HarmMinor", 3);

    bassKeyLabel.setVisible(false);
    bassKeyCombo.setVisible(false);
    bassScaleLabel.setVisible(false);
    bassScaleCombo.setVisible(false);

    volumeLabel.setText("Vol", juce::dontSendNotification);
    volumeLabel.setJustificationType(juce::Justification::centredLeft);
    volumeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 168, 182));
    volumeValueLabel.setJustificationType(juce::Justification::centredRight);
    volumeValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(238, 214, 186));
    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange(0.0, 1.5, 0.01);
    volumeSlider.setValue(0.85, juce::dontSendNotification);
    volumeSlider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(220, 150, 76));
    volumeSlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(238, 170, 95));

    panLabel.setText("Pan", juce::dontSendNotification);
    panLabel.setJustificationType(juce::Justification::centredLeft);
    panLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 168, 182));
    panValueLabel.setJustificationType(juce::Justification::centredRight);
    panValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(190, 224, 255));
    panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider.setRange(-100.0, 100.0, 1.0);
    panSlider.setValue(0.0, juce::dontSendNotification);
    panSlider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(110, 168, 236));
    panSlider.setColour(juce::Slider::backgroundColourId, juce::Colour::fromRGBA(255, 255, 255, 20));
    panSlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(190, 224, 255));
    panSlider.setTooltip("Pan: -100 left, 0 center, +100 right");

    widthLabel.setText("Width", juce::dontSendNotification);
    widthLabel.setJustificationType(juce::Justification::centredLeft);
    widthLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 168, 182));
    widthValueLabel.setJustificationType(juce::Justification::centredRight);
    widthValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(190, 224, 255));
    widthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    widthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    widthSlider.setRange(0.0, 100.0, 1.0);
    widthSlider.setValue(100.0, juce::dontSendNotification);
    widthSlider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(110, 168, 236));
    widthSlider.setColour(juce::Slider::backgroundColourId, juce::Colour::fromRGBA(255, 255, 255, 20));
    widthSlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(190, 224, 255));
    widthSlider.setTooltip("Width: 0 mono, 100 stereo");

    overflowMenuButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(58, 64, 76));

    dragDensitySlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    dragDensitySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    dragDensitySlider.setRange(0.0, 2.0, 0.01);
    dragDensitySlider.setValue(1.0, juce::dontSendNotification);
    dragDensitySlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(110, 176, 248));
    dragDensitySlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(196, 222, 255));
    dragDensitySlider.setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(216, 226, 238));
    dragDensitySlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    dragDensitySlider.setSkewFactorFromMidPoint(1.0);
    dragDensitySlider.setTooltip("HatFX density: 0 = no notes, 1 = original, >1 = more notes");
    dragDensityLockButton.setTooltip("Lock HatFX density");
    dragDensityValueLabel.setText("1.00", juce::dontSendNotification);

    enableButton.setToggleState(true, juce::dontSendNotification);

    rgButton.onClick = [this]
    {
        if (onRegenerate)
            onRegenerate(laneId);
    };

    soloButton.onClick = [this]
    {
        if (onSoloChanged)
            onSoloChanged(laneId, soloButton.getToggleState());
    };

    muteButton.onClick = [this]
    {
        if (onMuteChanged)
            onMuteChanged(laneId, muteButton.getToggleState());
    };

    clearButton.onClick = [this]
    {
        if (onClear)
            onClear(laneId);
    };

    lockButton.onClick = [this]
    {
        if (onLockChanged)
            onLockChanged(laneId, lockButton.getToggleState());
    };

    enableButton.onClick = [this]
    {
        if (onEnableChanged)
            onEnableChanged(laneId, enableButton.getToggleState());
    };

    prevSampleButton.onClick = [this]
    {
        if (onPrevSample)
            onPrevSample(laneId);
    };

    nextSampleButton.onClick = [this]
    {
        if (onNextSample)
            onNextSample(laneId);
    };

    bassKeyCombo.onChange = [this]
    {
        if (onBassKeyChanged)
            onBassKeyChanged(juce::jmax(0, bassKeyCombo.getSelectedId() - 1));
    };

    bassScaleCombo.onChange = [this]
    {
        if (onBassScaleChanged)
            onBassScaleChanged(juce::jmax(0, bassScaleCombo.getSelectedId() - 1));
    };

    volumeSlider.onValueChange = [this]
    {
        updateParameterValueLabels();
        if (onLaneVolumeChanged)
            onLaneVolumeChanged(laneId, static_cast<float>(volumeSlider.getValue()));
    };

    auto emitLaneSoundChange = [this]
    {
        updateParameterValueLabels();
        if (!onLaneSoundChanged)
            return;

        SoundLayerState state;
        state.pan = panStateFromUi(panSlider.getValue());
        state.width = widthStateFromUi(widthSlider.getValue());
        onLaneSoundChanged(laneId, state);
    };

    panSlider.onValueChange = emitLaneSoundChange;
    widthSlider.onValueChange = emitLaneSoundChange;

    overflowMenuButton.onClick = [this]
    {
        showOverflowMenu();
    };

    dragButton.onClickAction = [this]
    {
        if (onDrag)
            onDrag(laneId);
    };

    dragButton.onDragAction = [this]
    {
        if (onDragGesture)
            onDragGesture(laneId);
    };

    dragDensitySlider.onValueChange = [this]
    {
        dragDensityValue = static_cast<float>(dragDensitySlider.getValue());
        dragDensityValueLabel.setText(juce::String(dragDensityValue, 2), juce::dontSendNotification);
        if (onDragDensityChanged)
            onDragDensityChanged(laneId, dragDensityValue);
    };

    dragDensityLockButton.onClick = [this]
    {
        dragDensityLocked = dragDensityLockButton.getToggleState();
        dragDensitySlider.setEnabled(!dragDensityLocked);
        if (onDragDensityLockChanged)
            onDragDensityLockChanged(laneId, dragDensityLocked);
    };

    updateParameterValueLabels();
    setTooltip(tooltipTextForLane(initialState));
    applyDisplayModeVisibility();
}

void TrackRowComponent::paint(juce::Graphics& g)
{
    const auto rowBounds = getLocalBounds().toFloat().reduced(1.0f, 1.0f);
    const auto rowBackground = helperLaneUi ? juce::Colour::fromRGB(27, 31, 37)
                                            : juce::Colour::fromRGB(29, 33, 39);
    g.setColour(rowBackground);
    g.fillRoundedRectangle(rowBounds, 6.0f);

    const auto accent = helperLaneUi
        ? (explicitDependencyUi ? juce::Colour::fromRGB(98, 132, 166) : juce::Colour::fromRGB(84, 104, 128))
        : (isGhostTrack ? juce::Colour::fromRGB(90, 126, 170)
                        : juce::Colour::fromRGB(230, 150, 67));
    const float accentWidth = helperLaneUi ? 2.0f : 3.0f;
    g.setColour(accent.withAlpha(helperLaneUi ? 0.34f : 0.4f));
    g.fillRoundedRectangle(juce::Rectangle<float>(rowBounds.getX(), rowBounds.getY(), accentWidth, rowBounds.getHeight()), 2.0f);

    if (helperLaneUi)
    {
        const float branchX = rowBounds.getX() + 10.0f;
        const float branchTop = rowBounds.getY() + 8.0f;
        const float branchMidY = rowBounds.getY() + (displayMode == LaneRackDisplayMode::Full ? 19.0f : rowBounds.getCentreY());
        g.setColour(juce::Colour::fromRGBA(184, 198, 216, explicitDependencyUi ? 86 : 58));
        g.drawLine(branchX, branchTop, branchX, branchMidY, 1.0f);
        g.drawLine(branchX, branchMidY, branchX + 8.0f, branchMidY, 1.0f);

        if (explicitDependencyUi)
        {
            g.setColour(juce::Colour::fromRGBA(212, 222, 236, 92));
            g.fillEllipse(branchX - 1.5f, branchTop - 1.0f, 3.0f, 3.0f);
        }
    }

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, helperLaneUi ? 12 : 18));
    g.drawRoundedRectangle(rowBounds, 6.0f, 1.0f);

    if (displayMode != LaneRackDisplayMode::Minimal && helperBadgeText.isNotEmpty())
    {
        g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
        const auto badgeWidth = juce::jlimit(50.0f, 112.0f, textWidthForFont(g.getCurrentFont(), helperBadgeText) + 18.0f);
        const auto badgeBounds = juce::Rectangle<float>(rowBounds.getRight() - badgeWidth - 6.0f, rowBounds.getY() + 5.0f, badgeWidth, 14.0f);
        g.setColour(juce::Colour::fromRGBA(94, 126, 162, helperLaneUi ? 44 : 32));
        g.fillRoundedRectangle(badgeBounds, 4.0f);
        g.setColour(juce::Colour::fromRGBA(202, 214, 228, helperLaneUi ? 182 : 160));
        g.drawText(helperBadgeText, badgeBounds.toNearestInt(), juce::Justification::centred, false);
    }

    if (displayMode == LaneRackDisplayMode::Full && generationPriority > 0)
    {
        const auto priorityBounds = juce::Rectangle<float>(rowBounds.getRight() - 48.0f, rowBounds.getBottom() - 17.0f, 42.0f, 12.0f);
        g.setColour(juce::Colour::fromRGBA(255, 210, 138, 44));
        g.fillRoundedRectangle(priorityBounds, 4.0f);
        g.setColour(juce::Colour::fromRGBA(255, 230, 190, 190));
        g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
        g.drawText("P" + juce::String(generationPriority), priorityBounds.toNearestInt(), juce::Justification::centred, false);
    }
}

void TrackRowComponent::resized()
{
    auto area = getLocalBounds().reduced(6, 4);
    auto layoutParamColumn = [](juce::Rectangle<int> columnArea,
                                juce::Label& label,
                                juce::Label& valueLabel,
                                juce::Slider& slider)
    {
        auto topRow = columnArea.removeFromTop(10);
        label.setBounds(topRow.removeFromLeft(columnArea.getWidth() / 2 + 12));
        valueLabel.setBounds(topRow);
        columnArea.removeFromTop(2);
        slider.setBounds(columnArea.removeFromTop(12));
    };

    if (displayMode == LaneRackDisplayMode::Minimal)
    {
        auto actions = area.removeFromRight(98);
        dragButton.setBounds(actions.removeFromLeft(62).reduced(1));
        actions.removeFromLeft(2);
        overflowMenuButton.setBounds(actions.removeFromLeft(32).reduced(1));

        auto sampleArea = area.removeFromRight(154);
        prevSampleButton.setBounds(sampleArea.removeFromLeft(24).reduced(1));
        sampleArea.removeFromLeft(2);
        nextSampleButton.setBounds(sampleArea.removeFromRight(24).reduced(1));
        sampleArea.removeFromRight(2);
        sampleNameLabel.setBounds(sampleArea.reduced(1));

        muteButton.setBounds(area.removeFromRight(28).reduced(1));
        area.removeFromRight(2);
        soloButton.setBounds(area.removeFromRight(28).reduced(1));
        area.removeFromRight(2);
        rgButton.setBounds(area.removeFromRight(36).reduced(1));
        area.removeFromRight(6);

        auto nameBounds = area.removeFromTop(20);
        if (helperLaneUi)
            nameBounds.removeFromLeft(explicitDependencyUi ? 10 : 8);
        nameLabel.setBounds(nameBounds);
        roleLabel.setBounds({});
        return;
    }

    if (displayMode == LaneRackDisplayMode::Compact)
    {
        auto actions = area.removeFromRight(98);
        dragButton.setBounds(actions.removeFromLeft(62).reduced(1));
        actions.removeFromLeft(2);
        overflowMenuButton.setBounds(actions.removeFromLeft(32).reduced(1));

        area.removeFromRight(4);
        auto sampleArea = area.removeFromRight(154);
        prevSampleButton.setBounds(sampleArea.removeFromLeft(24).reduced(1));
        sampleArea.removeFromLeft(2);
        nextSampleButton.setBounds(sampleArea.removeFromRight(24).reduced(1));
        sampleArea.removeFromRight(2);
        sampleNameLabel.setBounds(sampleArea.reduced(1));
        area.removeFromRight(8);

        const int rgWidth = 34;
        rgButton.setBounds(area.removeFromLeft(rgWidth).reduced(1));
        area.removeFromLeft(2);
        soloButton.setBounds(area.removeFromLeft(28).reduced(1));
        area.removeFromLeft(2);
        muteButton.setBounds(area.removeFromLeft(28).reduced(1));
        clearButton.setBounds({});
        lockButton.setBounds({});
        enableButton.setBounds({});

        area.removeFromLeft(6);
        auto nameBounds = area.removeFromTop(18);
        if (helperLaneUi)
            nameBounds.removeFromLeft(explicitDependencyUi ? 10 : 8);
        nameLabel.setBounds(nameBounds);
        roleLabel.setBounds({});
        return;
    }

    auto labelArea = area.removeFromLeft(132);
    if (helperLaneUi)
        labelArea.removeFromLeft(explicitDependencyUi ? 10 : 8);
    nameLabel.setBounds(labelArea.removeFromTop(16));
    roleLabel.setBounds(labelArea.removeFromTop(14));

    const int rgWidth = 36;
    const int buttonWidth = 28;
    rgButton.setBounds(area.removeFromLeft(rgWidth).reduced(1));
    area.removeFromLeft(2);
    soloButton.setBounds(area.removeFromLeft(buttonWidth).reduced(1));
    area.removeFromLeft(2);
    muteButton.setBounds(area.removeFromLeft(buttonWidth).reduced(1));
    area.removeFromLeft(2);
    clearButton.setBounds(area.removeFromLeft(buttonWidth).reduced(1));
    area.removeFromLeft(2);
    lockButton.setBounds(area.removeFromLeft(buttonWidth).reduced(1));
    area.removeFromLeft(2);
    enableButton.setBounds(area.removeFromLeft(buttonWidth).reduced(1));

    area.removeFromLeft(6);
    auto sampleArea = area.removeFromLeft(176);
    prevSampleButton.setBounds(sampleArea.removeFromLeft(24).reduced(1));
    sampleArea.removeFromLeft(2);
    nextSampleButton.setBounds(sampleArea.removeFromRight(24).reduced(1));
    sampleArea.removeFromRight(2);
    sampleNameLabel.setBounds(sampleArea.reduced(1));

    area.removeFromLeft(6);
    auto volumeArea = area.removeFromLeft(88);
    layoutParamColumn(volumeArea, volumeLabel, volumeValueLabel, volumeSlider);

    area.removeFromLeft(4);
    auto panArea = area.removeFromLeft(90);
    layoutParamColumn(panArea, panLabel, panValueLabel, panSlider);

    area.removeFromLeft(4);
    auto widthArea = area.removeFromLeft(96);
    layoutParamColumn(widthArea, widthLabel, widthValueLabel, widthSlider);

    bassKeyLabel.setBounds({});
    bassKeyCombo.setBounds({});
    bassScaleLabel.setBounds({});
    bassScaleCombo.setBounds({});
    dragDensityLabel.setBounds({});
    dragDensityValueLabel.setBounds({});
    dragDensityLockButton.setBounds({});
    dragDensitySlider.setBounds({});

    area.removeFromLeft(6);
    dragButton.setBounds(area.removeFromLeft(62).reduced(1));
    area.removeFromLeft(4);
    overflowMenuButton.setBounds(area.removeFromLeft(32).reduced(1));
}

void TrackRowComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.mods.isRightButtonDown()
        && sampleNameLabel.isVisible()
        && sampleNameLabel.getBounds().contains(event.getPosition()))
    {
        if (onSampleMenuRequested)
            onSampleMenuRequested(laneId);
        return;
    }

    juce::Component::mouseDown(event);
}

void TrackRowComponent::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    juce::Component::mouseUp(event);
}

void TrackRowComponent::setDisplayMode(LaneRackDisplayMode mode)
{
    if (displayMode == mode)
        return;

    displayMode = mode;
    applyDisplayModeVisibility();
    resized();
    repaint();
}

void TrackRowComponent::applyDisplayModeVisibility()
{
    const bool full = displayMode == LaneRackDisplayMode::Full;
    const bool compact = displayMode == LaneRackDisplayMode::Compact;
    const bool minimal = displayMode == LaneRackDisplayMode::Minimal;

    roleLabel.setVisible(full);
    soloButton.setVisible(true);
    muteButton.setVisible(true);
    clearButton.setVisible(full);
    lockButton.setVisible(full);
    enableButton.setVisible(full || compact);

    volumeLabel.setVisible(full);
    volumeValueLabel.setVisible(full);
    volumeSlider.setVisible(full);
    panLabel.setVisible(full);
    panValueLabel.setVisible(full);
    panSlider.setVisible(full);
    widthLabel.setVisible(full);
    widthValueLabel.setVisible(full);
    widthSlider.setVisible(full);

    sampleNameLabel.setVisible(true);

    bassKeyLabel.setVisible(false);
    bassKeyCombo.setVisible(false);
    bassScaleLabel.setVisible(false);
    bassScaleCombo.setVisible(false);

    // Mandatory controls that must survive narrow rack mode.
    nameLabel.setVisible(true);
    rgButton.setVisible(true);
    dragButton.setVisible(true);
    overflowMenuButton.setVisible(true);
    prevSampleButton.setVisible(true);
    nextSampleButton.setVisible(true);
    exportButton.setVisible(false);
    dragDensityLabel.setVisible(false);
    dragDensitySlider.setVisible(false);
    dragDensityValueLabel.setVisible(false);
    dragDensityLockButton.setVisible(false);

    volumeSlider.setEnabled(hasRuntimeTrack());
    panSlider.setEnabled(hasRuntimeTrack());
    widthSlider.setEnabled(hasRuntimeTrack());
    dragDensitySlider.setEnabled(false);
    dragDensityLockButton.setEnabled(false);

    juce::ignoreUnused(minimal);
    juce::ignoreUnused(compact);
}

void TrackRowComponent::syncFromState(const RuntimeLaneRowState& state)
{
    laneId = state.laneId;
    runtimeTrackType = state.runtimeTrackType;
    groupName = state.groupName;
    dependencyName = state.dependencyName;
    generationPriority = state.generationPriority;
    isCore = state.isCore;
    supportsDragExport = state.supportsDragExport;
    isGhostTrack = state.isGhostTrack;
    helperLaneUi = isHelperLaneVisual(state);
    explicitDependencyUi = hasExplicitDependencyRelation(state);
    helperBadgeText = helperBadgeTextForLane(state);

    nameLabel.setText(state.laneName, juce::dontSendNotification);
    roleLabel.setText(roleLabelForLane(state), juce::dontSendNotification);
    applyIdentityVisualStyle();
    soloButton.setToggleState(state.solo, juce::dontSendNotification);
    muteButton.setToggleState(state.muted, juce::dontSendNotification);
    lockButton.setToggleState(state.locked, juce::dontSendNotification);
    enableButton.setToggleState(state.enabled, juce::dontSendNotification);
    volumeSlider.setValue(state.laneVolume, juce::dontSendNotification);
    panSlider.setValue(panUiFromState(state.sound.pan), juce::dontSendNotification);
    widthSlider.setValue(widthUiFromState(state.sound.width), juce::dontSendNotification);
    updateParameterValueLabels();

    auto sampleText = state.selectedSampleName;
    if (sampleText.isEmpty())
        sampleText = "(none)";
    sampleNameLabel.setText(sampleText, juce::dontSendNotification);
    currentSub808Settings = state.sub808Settings;

    rgButton.setEnabled(hasRuntimeTrack());
    soloButton.setEnabled(hasRuntimeTrack());
    muteButton.setEnabled(hasRuntimeTrack());
    clearButton.setEnabled(hasRuntimeTrack());
    lockButton.setEnabled(hasRuntimeTrack());
    enableButton.setEnabled(hasRuntimeTrack());
    prevSampleButton.setEnabled(hasRuntimeTrack());
    nextSampleButton.setEnabled(hasRuntimeTrack());
    dragButton.setEnabled(hasRuntimeTrack() && supportsDragExport);
    overflowMenuButton.setEnabled(true);
    volumeSlider.setEnabled(hasRuntimeTrack());
    panSlider.setEnabled(hasRuntimeTrack());
    widthSlider.setEnabled(hasRuntimeTrack());
    dragDensitySlider.setEnabled(false);
    dragDensityLockButton.setEnabled(false);

    setTooltip(tooltipTextForLane(state));
}

void TrackRowComponent::applyIdentityVisualStyle()
{
    const auto nameFont = helperLaneUi
        ? juce::Font(juce::FontOptions(12.5f))
        : juce::Font(juce::FontOptions(13.0f, juce::Font::bold));
    nameLabel.setFont(nameFont);
    nameLabel.setColour(juce::Label::textColourId,
                        helperLaneUi ? juce::Colour::fromRGB(204, 212, 224)
                                     : juce::Colour::fromRGB(232, 236, 242));

    roleLabel.setFont(juce::Font(juce::FontOptions(helperLaneUi ? 9.5f : 10.0f)));
    roleLabel.setColour(juce::Label::textColourId,
                        helperLaneUi ? juce::Colour::fromRGB(126, 136, 148)
                                     : juce::Colour::fromRGB(144, 152, 164));
}

void TrackRowComponent::setBassControls(int keyRootChoice, int scaleModeChoice)
{
    if (!isSub808Lane())
        return;

    currentBassKeyChoice = keyRootChoice;
    currentBassScaleChoice = scaleModeChoice;
    bassKeyCombo.setSelectedId(juce::jlimit(1, 12, keyRootChoice + 1), juce::dontSendNotification);
    bassScaleCombo.setSelectedId(juce::jlimit(1, 3, scaleModeChoice + 1), juce::dontSendNotification);
}

void TrackRowComponent::setSub808Settings(const Sub808LaneSettings& settings)
{
    if (!isSub808Lane())
        return;

    currentSub808Settings = settings;
}

void TrackRowComponent::setHatFxDragState(float density, bool locked)
{
    if (!isHatFxLane())
        return;

    dragDensityValue = juce::jlimit(0.0f, 2.0f, density);
    dragDensityLocked = locked;
    dragDensitySlider.setValue(dragDensityValue, juce::dontSendNotification);
    dragDensityValueLabel.setText(juce::String(dragDensityValue, 2), juce::dontSendNotification);
    dragDensityLockButton.setToggleState(dragDensityLocked, juce::dontSendNotification);
    dragDensitySlider.setEnabled(!dragDensityLocked);
    dragDensityLabel.setEnabled(true);
}

void TrackRowComponent::updateParameterValueLabels()
{
    volumeValueLabel.setText(formatVolumeValue(volumeSlider.getValue()), juce::dontSendNotification);
    panValueLabel.setText(formatPanValue(panSlider.getValue()), juce::dontSendNotification);
    widthValueLabel.setText(formatWidthValue(widthSlider.getValue()), juce::dontSendNotification);
}

void TrackRowComponent::showOverflowMenu()
{
    juce::PopupMenu menu;
    constexpr int kActionOpenSampleMenu = 1;
    constexpr int kActionPrevSample = 2;
    constexpr int kActionNextSample = 3;
    constexpr int kActionExport = 4;
    constexpr int kActionImportMidiToLane = 5;
    constexpr int kActionClearLane = 6;
    constexpr int kActionToggleLock = 7;
    constexpr int kActionToggleEnable = 8;
    constexpr int kActionMoveLaneUp = 9;
    constexpr int kActionMoveLaneDown = 10;
    constexpr int kActionRenameLane = 11;
    constexpr int kActionDeleteLane = 12;
    constexpr int kActionSub808Mono = 20;
    constexpr int kActionSub808CutItself = 21;
    constexpr int kActionSub808OverlapRetrigger = 22;
    constexpr int kActionSub808OverlapLegato = 23;
    constexpr int kActionSub808OverlapGlide = 24;
    constexpr int kActionSub808SnapOff = 25;
    constexpr int kActionSub808SnapHighlight = 26;
    constexpr int kActionSub808SnapForce = 27;
    constexpr int kActionSub808GlideTimeBase = 100;

    menu.addItem(kActionOpenSampleMenu, "Sample Menu", hasRuntimeTrack() && onSampleMenuRequested != nullptr);
    menu.addItem(kActionPrevSample, "Previous Sample", hasRuntimeTrack() && onPrevSample != nullptr);
    menu.addItem(kActionNextSample, "Next Sample", hasRuntimeTrack() && onNextSample != nullptr);
    menu.addItem(kActionImportMidiToLane, "Import MIDI to this lane", hasRuntimeTrack() && onImportMidiToLane != nullptr);
    menu.addItem(kActionExport, "Export Lane", hasRuntimeTrack() && supportsDragExport && onExport != nullptr);
    menu.addSeparator();
    menu.addItem(kActionClearLane, "Clear Lane", hasRuntimeTrack() && onClear != nullptr);
    menu.addItem(kActionToggleLock, lockButton.getToggleState() ? "Unlock Lane" : "Lock Lane", hasRuntimeTrack() && onLockChanged != nullptr);
    menu.addItem(kActionToggleEnable, enableButton.getToggleState() ? "Disable Lane" : "Enable Lane", hasRuntimeTrack() && onEnableChanged != nullptr);
    menu.addItem(kActionRenameLane, "Rename Lane", onRenameLaneRequested != nullptr);
    menu.addItem(kActionDeleteLane, "Delete Lane", onDeleteLaneRequested != nullptr);
    menu.addItem(kActionMoveLaneUp, "Move Lane Up", onMoveLaneUp != nullptr);
    menu.addItem(kActionMoveLaneDown, "Move Lane Down", onMoveLaneDown != nullptr);

    if (isSub808Lane() && onSub808SettingsChanged != nullptr)
    {
        juce::PopupMenu settingsMenu;
        settingsMenu.addItem(kActionSub808Mono, "Mono", true, currentSub808Settings.mono);
        settingsMenu.addItem(kActionSub808CutItself, "Cut Itself", true, currentSub808Settings.cutItself);

        juce::PopupMenu overlapMenu;
        overlapMenu.addItem(kActionSub808OverlapRetrigger, "Retrigger", true, currentSub808Settings.overlapMode == Sub808OverlapMode::Retrigger);
        overlapMenu.addItem(kActionSub808OverlapLegato, "Legato", true, currentSub808Settings.overlapMode == Sub808OverlapMode::Legato);
        overlapMenu.addItem(kActionSub808OverlapGlide, "Glide", true, currentSub808Settings.overlapMode == Sub808OverlapMode::Glide);
        settingsMenu.addSubMenu("Overlap Mode", overlapMenu, true);

        juce::PopupMenu snapMenu;
        snapMenu.addItem(kActionSub808SnapOff, "Snap Off", true, currentSub808Settings.scaleSnapPolicy == Sub808ScaleSnapPolicy::Off);
        snapMenu.addItem(kActionSub808SnapHighlight, "Highlight Only", true, currentSub808Settings.scaleSnapPolicy == Sub808ScaleSnapPolicy::HighlightOnly);
        snapMenu.addItem(kActionSub808SnapForce, "Force To Scale", true, currentSub808Settings.scaleSnapPolicy == Sub808ScaleSnapPolicy::ForceToScale);
        settingsMenu.addSubMenu("Scale Snap", snapMenu, true);

        juce::PopupMenu glideMenu;
        static constexpr std::array<int, 6> glideTimes { 0, 40, 80, 120, 200, 320 };
        for (const int glideTime : glideTimes)
            glideMenu.addItem(kActionSub808GlideTimeBase + glideTime,
                              juce::String(glideTime) + " ms",
                              true,
                              currentSub808Settings.glideTimeMs == glideTime);
        settingsMenu.addSubMenu("Glide Time", glideMenu, true);

        menu.addSeparator();
        menu.addSubMenu("Sub808 Settings", settingsMenu, true);
    }

    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&overflowMenuButton),
                       [safe = juce::Component::SafePointer<TrackRowComponent>(this)](int choice)
                       {
                           if (safe == nullptr || choice <= 0)
                               return;

                           if (choice == 1)
                           {
                               if (safe->onSampleMenuRequested)
                                   safe->onSampleMenuRequested(safe->laneId);
                               return;
                           }

                           if (choice == 2)
                           {
                               if (safe->onPrevSample)
                                   safe->onPrevSample(safe->laneId);
                               return;
                           }

                           if (choice == 3)
                           {
                               if (safe->onNextSample)
                                   safe->onNextSample(safe->laneId);
                               return;
                           }

                           if (choice == 4)
                           {
                               if (safe->onExport)
                                   safe->onExport(safe->laneId);
                               return;
                           }

                           if (choice == 5)
                           {
                               if (safe->onImportMidiToLane)
                                   safe->onImportMidiToLane(safe->laneId);
                               return;
                           }

                           if (choice == 6)
                           {
                               if (safe->onClear)
                                   safe->onClear(safe->laneId);
                               return;
                           }

                           if (choice == 7)
                           {
                               if (safe->onLockChanged)
                                   safe->onLockChanged(safe->laneId, !safe->lockButton.getToggleState());
                               return;
                           }

                           if (choice == 8)
                           {
                               if (safe->onEnableChanged)
                                   safe->onEnableChanged(safe->laneId, !safe->enableButton.getToggleState());
                               return;
                           }

                           if (choice >= 100)
                           {
                               if (safe->onSub808SettingsChanged)
                               {
                                   auto settings = safe->currentSub808Settings;
                                   settings.glideTimeMs = juce::jlimit(0, 4000, choice - 100);
                                   safe->currentSub808Settings = settings;
                                   safe->onSub808SettingsChanged(safe->laneId, settings);
                               }
                               return;
                           }

                           if (choice >= 20 && choice <= 27)
                           {
                               if (safe->onSub808SettingsChanged == nullptr)
                                   return;

                               auto settings = safe->currentSub808Settings;
                               if (choice == 20)
                                   settings.mono = !settings.mono;
                               else if (choice == 21)
                                   settings.cutItself = !settings.cutItself;
                               else if (choice == 22)
                                   settings.overlapMode = Sub808OverlapMode::Retrigger;
                               else if (choice == 23)
                                   settings.overlapMode = Sub808OverlapMode::Legato;
                               else if (choice == 24)
                                   settings.overlapMode = Sub808OverlapMode::Glide;
                               else if (choice == 25)
                                   settings.scaleSnapPolicy = Sub808ScaleSnapPolicy::Off;
                               else if (choice == 26)
                                   settings.scaleSnapPolicy = Sub808ScaleSnapPolicy::HighlightOnly;
                               else if (choice == 27)
                                   settings.scaleSnapPolicy = Sub808ScaleSnapPolicy::ForceToScale;

                               safe->currentSub808Settings = settings;
                               safe->onSub808SettingsChanged(safe->laneId, settings);
                               return;
                           }

                           if (choice == 11)
                           {
                               if (safe->onRenameLaneRequested)
                                   safe->onRenameLaneRequested(safe->laneId);
                               return;
                           }

                           if (choice == 12)
                           {
                               if (safe->onDeleteLaneRequested)
                                   safe->onDeleteLaneRequested(safe->laneId);
                               return;
                           }

                           if (choice == 9)
                           {
                               if (safe->onMoveLaneUp)
                                   safe->onMoveLaneUp(safe->laneId);
                               return;
                           }

                           if (choice == 10)
                           {
                               if (safe->onMoveLaneDown)
                                   safe->onMoveLaneDown(safe->laneId);
                               return;
                           }

                       });
}

juce::String TrackRowComponent::roleLabelForLane(const RuntimeLaneRowState& state)
{
    juce::StringArray parts;
    if (state.groupName.isNotEmpty())
        parts.add(state.groupName);
    parts.add(laneRolePhraseForLane(state));
    return parts.joinIntoString(" | ");
}

juce::String TrackRowComponent::helperBadgeTextForLane(const RuntimeLaneRowState& state)
{
    return helperBadgeTextForState(state);
}

juce::String TrackRowComponent::tooltipTextForLane(const RuntimeLaneRowState& state)
{
    juce::StringArray parts;
    if (state.groupName.isNotEmpty())
        parts.add(state.groupName);

    parts.add(laneTooltipPhraseForLane(state));

    if (state.generationPriority > 0)
        parts.add("Generation order " + juce::String(state.generationPriority));

    return parts.joinIntoString(" | ");
}
} // namespace bbg
