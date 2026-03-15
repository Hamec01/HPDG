#include "TrackRowComponent.h"

namespace bbg
{
TrackRowComponent::TrackRowComponent(const TrackInfo& info)
    : trackInfo(info)
    , trackType(info.type)
{
    nameLabel.setText(trackInfo.displayName, juce::dontSendNotification);
    nameLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    nameLabel.setJustificationType(juce::Justification::centredLeft);
    nameLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 236, 242));
    addAndMakeVisible(nameLabel);

    roleLabel.setText(roleLabelForTrack(trackType), juce::dontSendNotification);
    roleLabel.setFont(juce::Font(11.0f));
    roleLabel.setJustificationType(juce::Justification::centredLeft);
    roleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 160, 172));
    addAndMakeVisible(roleLabel);

    addAndMakeVisible(rgButton);
    addAndMakeVisible(soloButton);
    addAndMakeVisible(muteButton);
    addAndMakeVisible(clearButton);
    addAndMakeVisible(lockButton);
    addAndMakeVisible(enableButton);
    addAndMakeVisible(volumeLabel);
    addAndMakeVisible(volumeSlider);
    addAndMakeVisible(prevSampleButton);
    addAndMakeVisible(sampleNameLabel);
    addAndMakeVisible(nextSampleButton);
    addAndMakeVisible(bassKeyLabel);
    addAndMakeVisible(bassKeyCombo);
    addAndMakeVisible(bassScaleLabel);
    addAndMakeVisible(bassScaleCombo);
    addAndMakeVisible(dragButton);
    addAndMakeVisible(exportButton);

    rgButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(74, 82, 98));
    clearButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(74, 82, 98));
    dragButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(64, 70, 82));
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(64, 70, 82));
    prevSampleButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(58, 64, 76));
    nextSampleButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(58, 64, 76));

    sampleNameLabel.setJustificationType(juce::Justification::centred);
    sampleNameLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 221, 232));
    sampleNameLabel.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    sampleNameLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGBA(255, 255, 255, 8));
    sampleNameLabel.setText("(none)", juce::dontSendNotification);

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

    const bool showBassControls = trackType == TrackType::Sub808;
    bassKeyLabel.setVisible(showBassControls);
    bassKeyCombo.setVisible(showBassControls);
    bassScaleLabel.setVisible(showBassControls);
    bassScaleCombo.setVisible(showBassControls);

    volumeLabel.setText("Vol", juce::dontSendNotification);
    volumeLabel.setJustificationType(juce::Justification::centredLeft);
    volumeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 168, 182));
    volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange(0.0, 1.5, 0.01);
    volumeSlider.setValue(0.85, juce::dontSendNotification);
    volumeSlider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(220, 150, 76));
    volumeSlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(238, 170, 95));

    enableButton.setToggleState(true, juce::dontSendNotification);

    rgButton.onClick = [this]
    {
        if (onRegenerate)
            onRegenerate(trackType);
    };

    soloButton.onClick = [this]
    {
        if (onSoloChanged)
            onSoloChanged(trackType, soloButton.getToggleState());
    };

    muteButton.onClick = [this]
    {
        if (onMuteChanged)
            onMuteChanged(trackType, muteButton.getToggleState());
    };

    clearButton.onClick = [this]
    {
        if (onClear)
            onClear(trackType);
    };

    lockButton.onClick = [this]
    {
        if (onLockChanged)
            onLockChanged(trackType, lockButton.getToggleState());
    };

    enableButton.onClick = [this]
    {
        if (onEnableChanged)
            onEnableChanged(trackType, enableButton.getToggleState());
    };

    prevSampleButton.onClick = [this]
    {
        if (onPrevSample)
            onPrevSample(trackType);
    };

    nextSampleButton.onClick = [this]
    {
        if (onNextSample)
            onNextSample(trackType);
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
        if (onLaneVolumeChanged)
            onLaneVolumeChanged(trackType, static_cast<float>(volumeSlider.getValue()));
    };

    dragButton.onClickAction = [this]
    {
        if (onDrag)
            onDrag(trackType);
    };

    dragButton.onDragAction = [this]
    {
        if (onDragGesture)
            onDragGesture(trackType);
    };

    exportButton.onClick = [this]
    {
        if (onExport)
            onExport(trackType);
    };
}

void TrackRowComponent::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f, 1.0f);
    g.setColour(juce::Colour::fromRGB(29, 33, 39));
    g.fillRoundedRectangle(r, 6.0f);

    const auto accent = trackInfo.isGhostTrack ? juce::Colour::fromRGB(90, 126, 170)
                                               : juce::Colour::fromRGB(230, 150, 67);
    g.setColour(accent.withAlpha(0.4f));
    g.fillRoundedRectangle(r.removeFromLeft(3.0f), 2.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f, 1.0f), 6.0f, 1.0f);
}

void TrackRowComponent::resized()
{
    auto area = getLocalBounds().reduced(6, 4);
    auto labelArea = area.removeFromLeft(132);
    nameLabel.setBounds(labelArea.removeFromTop(16));
    roleLabel.setBounds(labelArea.removeFromTop(14));

    const int buttonWidth = 28;
    rgButton.setBounds(area.removeFromLeft(buttonWidth));
    soloButton.setBounds(area.removeFromLeft(buttonWidth));
    muteButton.setBounds(area.removeFromLeft(buttonWidth));
    clearButton.setBounds(area.removeFromLeft(buttonWidth));
    lockButton.setBounds(area.removeFromLeft(buttonWidth));
    enableButton.setBounds(area.removeFromLeft(buttonWidth));

    area.removeFromLeft(4);
    auto volumeArea = area.removeFromLeft(86);
    volumeLabel.setBounds(volumeArea.removeFromTop(12));
    volumeSlider.setBounds(volumeArea.removeFromTop(16));

    area.removeFromLeft(4);
    prevSampleButton.setBounds(area.removeFromLeft(24));
    sampleNameLabel.setBounds(area.removeFromLeft(trackType == TrackType::Sub808 ? 96 : 86));
    nextSampleButton.setBounds(area.removeFromLeft(24));

    if (trackType == TrackType::Sub808)
    {
        area.removeFromLeft(4);
        auto keyArea = area.removeFromLeft(72);
        bassKeyLabel.setBounds(keyArea.removeFromTop(12));
        bassKeyCombo.setBounds(keyArea.removeFromTop(16));

        auto scaleArea = area.removeFromLeft(106);
        bassScaleLabel.setBounds(scaleArea.removeFromTop(12));
        bassScaleCombo.setBounds(scaleArea.removeFromTop(16));
    }

    area.removeFromLeft(4);
    dragButton.setBounds(area.removeFromLeft(46));
    exportButton.setBounds(area.removeFromLeft(52));
}

void TrackRowComponent::syncFromState(const TrackState& state)
{
    soloButton.setToggleState(state.solo, juce::dontSendNotification);
    muteButton.setToggleState(state.muted, juce::dontSendNotification);
    lockButton.setToggleState(state.locked, juce::dontSendNotification);
    enableButton.setToggleState(state.enabled, juce::dontSendNotification);
    volumeSlider.setValue(state.laneVolume, juce::dontSendNotification);

    auto sampleText = state.selectedSampleName;
    if (sampleText.isEmpty())
        sampleText = "(none)";
    sampleNameLabel.setText(sampleText, juce::dontSendNotification);
}

void TrackRowComponent::setBassControls(int keyRootChoice, int scaleModeChoice)
{
    if (trackType != TrackType::Sub808)
        return;

    bassKeyCombo.setSelectedId(juce::jlimit(1, 12, keyRootChoice + 1), juce::dontSendNotification);
    bassScaleCombo.setSelectedId(juce::jlimit(1, 3, scaleModeChoice + 1), juce::dontSendNotification);
}

juce::String TrackRowComponent::roleLabelForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick:           return "Low Foundation";
        case TrackType::GhostKick:      return "Ghost Motion";
        case TrackType::Snare:          return "Backbeat";
        case TrackType::ClapGhostSnare: return "Snare Texture";
        case TrackType::HiHat:          return "Pulse Driver";
        case TrackType::HatFX:          return "Hat Ornament";
        case TrackType::OpenHat:        return "Air Accent";
        case TrackType::Ride:           return "Top Rhythm";
        case TrackType::Cymbal:         return "Crash Accent";
        case TrackType::Perc:           return "Perc Layer";
        case TrackType::Sub808:         return "Sub Foundation";
        default:                        return {};
    }
}
} // namespace bbg
