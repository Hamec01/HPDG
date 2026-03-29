#include "SoundModuleComponent.h"

namespace bbg
{
SoundModuleComponent::SoundModuleComponent()
{
    titleLabel.setText("SOUND MODULE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(12.5f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(222, 228, 236));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    targetLabel.setText("Target", juce::dontSendNotification);
    targetLabel.setJustificationType(juce::Justification::centredLeft);
    targetLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(166, 176, 192));
    targetLabel.setFont(juce::Font(11.0f));
    addAndMakeVisible(targetLabel);
    addAndMakeVisible(targetCombo);

    eqLabel.setText("EQ Tone", juce::dontSendNotification);
    compLabel.setText("Compression", juce::dontSendNotification);
    reverbLabel.setText("Reverb", juce::dontSendNotification);
    gateLabel.setText("Gate", juce::dontSendNotification);
    transientLabel.setText("Transient", juce::dontSendNotification);
    driveLabel.setText("LoFi / Drive", juce::dontSendNotification);

    auto styleLabel = [](juce::Label& label)
    {
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(166, 176, 192));
        label.setFont(juce::Font(10.5f));
    };

    styleLabel(eqLabel);
    styleLabel(compLabel);
    styleLabel(reverbLabel);
    styleLabel(gateLabel);
    styleLabel(transientLabel);
    styleLabel(driveLabel);

    setupSlider(eqSlider, -1.0, 1.0, 0.01, juce::Colour::fromRGB(112, 172, 142), juce::Colour::fromRGB(188, 230, 206));
    setupSlider(compSlider, 0.0, 1.0, 0.01, juce::Colour::fromRGB(112, 172, 142), juce::Colour::fromRGB(188, 230, 206));
    setupSlider(reverbSlider, 0.0, 1.0, 0.01, juce::Colour::fromRGB(214, 150, 96), juce::Colour::fromRGB(248, 206, 162));
    setupSlider(gateSlider, 0.0, 1.0, 0.01, juce::Colour::fromRGB(214, 150, 96), juce::Colour::fromRGB(248, 206, 162));
    setupSlider(transientSlider, 0.0, 1.0, 0.01, juce::Colour::fromRGB(154, 144, 214), juce::Colour::fromRGB(212, 206, 250));
    setupSlider(driveSlider, 0.0, 1.0, 0.01, juce::Colour::fromRGB(154, 144, 214), juce::Colour::fromRGB(212, 206, 250));

    addAndMakeVisible(eqLabel);
    addAndMakeVisible(compLabel);
    addAndMakeVisible(reverbLabel);
    addAndMakeVisible(gateLabel);
    addAndMakeVisible(transientLabel);
    addAndMakeVisible(driveLabel);

    targetCombo.onChange = [this]
    {
        const int id = targetCombo.getSelectedId();
        SoundTargetDescriptor target = SoundTargetDescriptor::makeGlobal();
        if (id >= 2)
        {
            const int index = id - 2;
            if (index >= 0 && index < static_cast<int>(targetDescriptors.size()))
                target = targetDescriptors[static_cast<size_t>(index)];
        }

        currentTarget = target;
        if (onSoundTargetChanged)
            onSoundTargetChanged(currentTarget);
    };

    eqSlider.onValueChange = [this] { emitSoundLayerChange(); };
    compSlider.onValueChange = [this] { emitSoundLayerChange(); };
    reverbSlider.onValueChange = [this] { emitSoundLayerChange(); };
    gateSlider.onValueChange = [this] { emitSoundLayerChange(); };
    transientSlider.onValueChange = [this] { emitSoundLayerChange(); };
    driveSlider.onValueChange = [this] { emitSoundLayerChange(); };
}

void SoundModuleComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(13, 16, 22));
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 20));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 6.0f, 1.0f);
}

void SoundModuleComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    titleLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(2);

    auto targetLine = area.removeFromTop(24);
    targetLabel.setBounds(targetLine.removeFromLeft(68));
    targetCombo.setBounds(targetLine.removeFromLeft(230));

    area.removeFromTop(6);
    auto rowA = area.removeFromTop(42);
    auto rowB = area.removeFromTop(42);
    auto rowC = area.removeFromTop(42);
    auto rowD = area.removeFromTop(42);

    const auto placeSound = [](juce::Rectangle<int>& row, juce::Label& label, juce::Slider& slider)
    {
        auto slot = row.removeFromLeft(row.getWidth() / 2).reduced(2, 0);
        label.setBounds(slot.removeFromTop(14));
        slider.setBounds(slot.removeFromTop(24));
    };

    placeSound(rowA, eqLabel, eqSlider);
    placeSound(rowA, compLabel, compSlider);
    placeSound(rowB, reverbLabel, reverbSlider);
    placeSound(rowB, gateLabel, gateSlider);
    placeSound(rowC, transientLabel, transientSlider);
    placeSound(rowC, driveLabel, driveSlider);

    rowD.removeFromTop(rowD.getHeight());
}

void SoundModuleComponent::setState(const std::vector<TrackState>& tracks,
                                    const SoundTargetDescriptor& selectedTarget,
                                    const SoundLayerState& soundState)
{
    targetDescriptors.clear();
    targetCombo.clear(juce::dontSendNotification);
    targetCombo.addItem("All Tracks (Global)", 1);

    int selectedId = 1;
    int itemId = 2;
    for (const auto& track : tracks)
    {
        const auto* info = TrackRegistry::find(track.type);
        if (info == nullptr || !info->visibleInUI)
            continue;

        const auto descriptor = track.runtimeTrackType.has_value() && track.laneId.isNotEmpty()
            ? SoundTargetDescriptor::makeBackedRuntimeLane(track.laneId, *track.runtimeTrackType)
            : SoundTargetDescriptor::makeLegacyTrackTypeAlias(track.type);

        targetDescriptors.push_back(descriptor);
        targetCombo.addItem(info->displayName, itemId);
        if (selectedTarget == descriptor)
            selectedId = itemId;
        ++itemId;
    }

    currentTarget = selectedTarget;
    currentSoundState = soundState;
    targetCombo.setSelectedId(selectedId, juce::dontSendNotification);

    eqSlider.setValue(soundState.eqTone, juce::dontSendNotification);
    compSlider.setValue(soundState.compression, juce::dontSendNotification);
    reverbSlider.setValue(soundState.reverb, juce::dontSendNotification);
    gateSlider.setValue(soundState.gate, juce::dontSendNotification);
    transientSlider.setValue(soundState.transient, juce::dontSendNotification);
    driveSlider.setValue(soundState.drive, juce::dontSendNotification);
}

void SoundModuleComponent::setupSlider(juce::Slider& slider,
                                       double min,
                                       double max,
                                       double step,
                                       juce::Colour trackColour,
                                       juce::Colour thumbColour)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange(min, max, step);
    slider.setColour(juce::Slider::trackColourId, trackColour);
    slider.setColour(juce::Slider::thumbColourId, thumbColour);
    addAndMakeVisible(slider);
}

void SoundModuleComponent::emitSoundLayerChange()
{
    if (!onSoundLayerChanged)
        return;

    SoundLayerState state = currentSoundState;
    state.eqTone = static_cast<float>(eqSlider.getValue());
    state.compression = static_cast<float>(compSlider.getValue());
    state.reverb = static_cast<float>(reverbSlider.getValue());
    state.gate = static_cast<float>(gateSlider.getValue());
    state.transient = static_cast<float>(transientSlider.getValue());
    state.drive = static_cast<float>(driveSlider.getValue());
    currentSoundState = state;
    onSoundLayerChanged(currentTarget, state);
}
} // namespace bbg
