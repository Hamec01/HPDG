#include "SoundModuleComponent.h"

namespace bbg
{
namespace
{
juce::String displayNameForTrackType(TrackType type)
{
    if (const auto* info = TrackRegistry::find(type); info != nullptr)
        return info->displayName;

    return "Track";
}

juce::String displayNameForDescriptor(const SoundTargetDescriptor& descriptor)
{
    if (descriptor.kind == SoundTargetDescriptorKind::BackedRuntimeLane && descriptor.runtimeTrackType.has_value())
        return displayNameForTrackType(*descriptor.runtimeTrackType);

    if (descriptor.kind == SoundTargetDescriptorKind::LegacyTrackTypeAlias && descriptor.legacyTrackTypeAlias.has_value())
        return displayNameForTrackType(*descriptor.legacyTrackTypeAlias);

    return "Global";
}

bool comboContainsDescriptor(const std::vector<SoundTargetDescriptor>& descriptors, const SoundTargetDescriptor& target)
{
    return std::find(descriptors.begin(), descriptors.end(), target) != descriptors.end();
}

juce::String formatPercent(float value)
{
    return juce::String(juce::roundToInt(std::abs(value) * 100.0f)) + "%";
}

juce::String formatToneValue(float value)
{
    if (std::abs(value) < 0.05f)
        return "Neutral";

    return value < 0.0f ? "Dark " + formatPercent(value)
                        : "Bright " + formatPercent(value);
}

juce::String formatGlueValue(float value)
{
    if (value < 0.05f)
        return "Open";
    if (value < 0.33f)
        return "Light " + formatPercent(value);
    if (value < 0.66f)
        return "Glue " + formatPercent(value);
    return "Tight " + formatPercent(value);
}

juce::String formatSpaceValue(float value)
{
    if (value < 0.05f)
        return "Dry";
    if (value < 0.4f)
        return "Room " + formatPercent(value);
    if (value < 0.75f)
        return "Space " + formatPercent(value);
    return "Wash " + formatPercent(value);
}

juce::String formatGateValue(float value)
{
    if (value < 0.05f)
        return "Open";
    if (value < 0.4f)
        return "Trim " + formatPercent(value);
    return "Tight " + formatPercent(value);
}

juce::String formatPunchValue(float value)
{
    if (value < 0.05f)
        return "Smooth";
    if (value < 0.4f)
        return "Lift " + formatPercent(value);
    return "Punch " + formatPercent(value);
}

juce::String formatDriveValue(float value)
{
    if (value < 0.05f)
        return "Clean";
    if (value < 0.4f)
        return "Warm " + formatPercent(value);
    return "Grit " + formatPercent(value);
}

juce::Colour groupFillForIndex(int index)
{
    switch (index)
    {
        case 0: return juce::Colour::fromRGB(19, 29, 28);
        case 1: return juce::Colour::fromRGB(31, 25, 20);
        default: return juce::Colour::fromRGB(25, 23, 34);
    }
}

juce::Colour groupOutlineForIndex(int index)
{
    switch (index)
    {
        case 0: return juce::Colour::fromRGBA(136, 204, 174, 76);
        case 1: return juce::Colour::fromRGBA(248, 198, 154, 78);
        default: return juce::Colour::fromRGBA(206, 194, 250, 82);
    }
}
}

SoundModuleComponent::SoundModuleComponent()
{
    titleLabel.setText("SOUND MODULE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(12.5f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(222, 228, 236));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    targetLabel.setText("Routing", juce::dontSendNotification);
    targetLabel.setJustificationType(juce::Justification::centredLeft);
    targetLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(166, 176, 192));
    targetLabel.setFont(juce::Font(11.0f));
    addAndMakeVisible(targetLabel);

    targetSummaryLabel.setText("Editing: Global", juce::dontSendNotification);
    targetSummaryLabel.setJustificationType(juce::Justification::centredLeft);
    targetSummaryLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(222, 228, 236));
    targetSummaryLabel.setFont(juce::Font(11.5f, juce::Font::bold));
    addAndMakeVisible(targetSummaryLabel);

    targetModeLabel.setText("GLOBAL", juce::dontSendNotification);
    targetModeLabel.setJustificationType(juce::Justification::centred);
    targetModeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(244, 248, 255));
    targetModeLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(71, 96, 132));
    targetModeLabel.setColour(juce::Label::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 0));
    targetModeLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(targetModeLabel);

    targetStatusLabel.setText("Routing: shared sound layer for the full kit.", juce::dontSendNotification);
    targetStatusLabel.setJustificationType(juce::Justification::centredLeft);
    targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 200, 255));
    targetStatusLabel.setFont(juce::Font(10.5f));
    addAndMakeVisible(targetStatusLabel);

    targetCombo.setTooltip("Choose whether the Sound Module edits the global layer or a specific lane target.");
    addAndMakeVisible(targetCombo);

    toneSectionLabel.setText("TONE & GLUE", juce::dontSendNotification);
    spaceSectionLabel.setText("SPACE & CONTROL", juce::dontSendNotification);
    textureSectionLabel.setText("PUNCH & TEXTURE", juce::dontSendNotification);

    auto styleSectionLabel = [](juce::Label& label)
    {
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(190, 198, 212));
        label.setFont(juce::Font(9.5f, juce::Font::bold));
    };

    styleSectionLabel(toneSectionLabel);
    styleSectionLabel(spaceSectionLabel);
    styleSectionLabel(textureSectionLabel);

    addAndMakeVisible(toneSectionLabel);
    addAndMakeVisible(spaceSectionLabel);
    addAndMakeVisible(textureSectionLabel);

    eqLabel.setText("Tone", juce::dontSendNotification);
    compLabel.setText("Glue", juce::dontSendNotification);
    reverbLabel.setText("Space", juce::dontSendNotification);
    gateLabel.setText("Tightness", juce::dontSendNotification);
    transientLabel.setText("Punch", juce::dontSendNotification);
    driveLabel.setText("Grit", juce::dontSendNotification);

    auto styleLabel = [](juce::Label& label)
    {
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(166, 176, 192));
        label.setFont(juce::Font(10.5f));
    };

    auto styleValueLabel = [](juce::Label& label)
    {
        label.setJustificationType(juce::Justification::centredRight);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 222, 234));
        label.setFont(juce::Font(10.0f, juce::Font::bold));
    };

    styleLabel(eqLabel);
    styleLabel(compLabel);
    styleLabel(reverbLabel);
    styleLabel(gateLabel);
    styleLabel(transientLabel);
    styleLabel(driveLabel);

    styleValueLabel(eqValueLabel);
    styleValueLabel(compValueLabel);
    styleValueLabel(reverbValueLabel);
    styleValueLabel(gateValueLabel);
    styleValueLabel(transientValueLabel);
    styleValueLabel(driveValueLabel);

    eqSlider.setTooltip("Tilt the current target darker or brighter.");
    compSlider.setTooltip("Add more or less glue to the current target.");
    reverbSlider.setTooltip("Set how much space surrounds the current target.");
    gateSlider.setTooltip("Tighten the tail of the current target.");
    transientSlider.setTooltip("Bring out the front edge and punch.");
    driveSlider.setTooltip("Add warmth and grit to the current target.");

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
    addAndMakeVisible(eqValueLabel);
    addAndMakeVisible(compValueLabel);
    addAndMakeVisible(reverbValueLabel);
    addAndMakeVisible(gateValueLabel);
    addAndMakeVisible(transientValueLabel);
    addAndMakeVisible(driveValueLabel);

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

    for (size_t i = 0; i < moduleGroupBounds.size(); ++i)
    {
        const auto bounds = moduleGroupBounds[i].toFloat();
        if (bounds.isEmpty())
            continue;

        g.setColour(groupFillForIndex(static_cast<int>(i)));
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(groupOutlineForIndex(static_cast<int>(i)));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 20));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 6.0f, 1.0f);
}

void SoundModuleComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto titleRow = area.removeFromTop(22);
    targetModeLabel.setBounds(titleRow.removeFromRight(84));
    titleLabel.setBounds(titleRow);

    area.removeFromTop(2);
    targetSummaryLabel.setBounds(area.removeFromTop(18));

    area.removeFromTop(2);
    auto targetLine = area.removeFromTop(24);
    targetLabel.setBounds(targetLine.removeFromLeft(68));
    targetCombo.setBounds(targetLine);

    area.removeFromTop(4);
    targetStatusLabel.setBounds(area.removeFromTop(18));

    area.removeFromTop(6);
    auto rowA = area.removeFromTop(46);
    auto rowB = area.removeFromTop(46);
    auto rowC = area.removeFromTop(46);

    moduleGroupBounds[0] = rowA;
    moduleGroupBounds[1] = rowB;
    moduleGroupBounds[2] = rowC;

    const auto placeSound = [](juce::Rectangle<int>& row,
                               juce::Label& label,
                               juce::Label& valueLabel,
                               juce::Slider& slider)
    {
        auto slot = row.removeFromLeft(row.getWidth() / 2).reduced(6, 0);
        auto topLine = slot.removeFromTop(14);
        valueLabel.setBounds(topLine.removeFromRight(72));
        label.setBounds(topLine);
        slot.removeFromTop(2);
        slider.setBounds(slot.removeFromTop(18));
    };

    auto rowAContent = rowA.reduced(4, 4);
    toneSectionLabel.setBounds(rowAContent.removeFromTop(12));
    rowAContent.removeFromTop(2);
    placeSound(rowAContent, eqLabel, eqValueLabel, eqSlider);
    placeSound(rowAContent, compLabel, compValueLabel, compSlider);

    auto rowBContent = rowB.reduced(4, 4);
    spaceSectionLabel.setBounds(rowBContent.removeFromTop(12));
    rowBContent.removeFromTop(2);
    placeSound(rowBContent, reverbLabel, reverbValueLabel, reverbSlider);
    placeSound(rowBContent, gateLabel, gateValueLabel, gateSlider);

    auto rowCContent = rowC.reduced(4, 4);
    textureSectionLabel.setBounds(rowCContent.removeFromTop(12));
    rowCContent.removeFromTop(2);
    placeSound(rowCContent, transientLabel, transientValueLabel, transientSlider);
    placeSound(rowCContent, driveLabel, driveValueLabel, driveSlider);
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
    bool targetMatchedInCombo = selectedTarget.isGlobal();
    juce::String matchedTargetName = "Global";
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
        {
            selectedId = itemId;
            targetMatchedInCombo = true;
            matchedTargetName = info->displayName;
        }
        ++itemId;
    }

    currentTarget = selectedTarget;
    currentSoundState = soundState;
    targetAvailable = selectedTarget.isGlobal() || comboContainsDescriptor(targetDescriptors, selectedTarget);
    if (targetAvailable)
    {
        targetCombo.setSelectedId(selectedId, juce::dontSendNotification);
    }
    else
    {
        targetCombo.setSelectedId(0, juce::dontSendNotification);
        targetCombo.setText("Unavailable: " + displayNameForDescriptor(selectedTarget), juce::dontSendNotification);
    }

    eqSlider.setValue(soundState.eqTone, juce::dontSendNotification);
    compSlider.setValue(soundState.compression, juce::dontSendNotification);
    reverbSlider.setValue(soundState.reverb, juce::dontSendNotification);
    gateSlider.setValue(soundState.gate, juce::dontSendNotification);
    transientSlider.setValue(soundState.transient, juce::dontSendNotification);
    driveSlider.setValue(soundState.drive, juce::dontSendNotification);

    updateTargetPresentation(tracks, selectedTarget, targetMatchedInCombo, matchedTargetName);
    setControlsEnabled(targetAvailable);
    updateValueLabels();
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

void SoundModuleComponent::setControlsEnabled(bool shouldEnable)
{
    eqSlider.setEnabled(shouldEnable);
    compSlider.setEnabled(shouldEnable);
    reverbSlider.setEnabled(shouldEnable);
    gateSlider.setEnabled(shouldEnable);
    transientSlider.setEnabled(shouldEnable);
    driveSlider.setEnabled(shouldEnable);
}

void SoundModuleComponent::updateTargetPresentation(const std::vector<TrackState>& tracks,
                                                    const SoundTargetDescriptor& selectedTarget,
                                                    bool targetMatchedInCombo,
                                                    const juce::String& matchedTargetName)
{
    juce::ignoreUnused(tracks);

    if (!targetAvailable)
    {
        targetSummaryLabel.setText("Editing: Target unavailable", juce::dontSendNotification);
        targetModeLabel.setText("INVALID", juce::dontSendNotification);
        targetModeLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(120, 68, 68));
        targetStatusLabel.setText("Target is no longer valid. Choose Global or a visible lane before editing.",
                                  juce::dontSendNotification);
        targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(255, 170, 170));
        return;
    }

    if (selectedTarget.isGlobal())
    {
        targetSummaryLabel.setText("Editing: Global sound layer", juce::dontSendNotification);
        targetModeLabel.setText("GLOBAL", juce::dontSendNotification);
        targetModeLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(71, 96, 132));
        targetStatusLabel.setText("Routing: shared sound shaping for the full kit.", juce::dontSendNotification);
        targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 200, 255));
        return;
    }

    const auto targetName = targetMatchedInCombo ? matchedTargetName : displayNameForDescriptor(selectedTarget);
    if (selectedTarget.kind == SoundTargetDescriptorKind::BackedRuntimeLane)
    {
        targetSummaryLabel.setText("Editing: " + targetName + " lane", juce::dontSendNotification);
        targetModeLabel.setText("LANE", juce::dontSendNotification);
        targetModeLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(78, 112, 84));
        targetStatusLabel.setText("Routing: edits the current backed runtime lane only.", juce::dontSendNotification);
        targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(174, 222, 184));
        return;
    }

    targetSummaryLabel.setText("Editing: " + targetName + " track", juce::dontSendNotification);
    targetModeLabel.setText("TRACK", juce::dontSendNotification);
    targetModeLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(118, 92, 64));
    targetStatusLabel.setText("Routing: edits the selected track target.", juce::dontSendNotification);
    targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(240, 204, 166));
}

void SoundModuleComponent::updateValueLabels()
{
    eqValueLabel.setText(formatToneValue(static_cast<float>(eqSlider.getValue())), juce::dontSendNotification);
    compValueLabel.setText(formatGlueValue(static_cast<float>(compSlider.getValue())), juce::dontSendNotification);
    reverbValueLabel.setText(formatSpaceValue(static_cast<float>(reverbSlider.getValue())), juce::dontSendNotification);
    gateValueLabel.setText(formatGateValue(static_cast<float>(gateSlider.getValue())), juce::dontSendNotification);
    transientValueLabel.setText(formatPunchValue(static_cast<float>(transientSlider.getValue())), juce::dontSendNotification);
    driveValueLabel.setText(formatDriveValue(static_cast<float>(driveSlider.getValue())), juce::dontSendNotification);
}

void SoundModuleComponent::emitSoundLayerChange()
{
    if (!onSoundLayerChanged || !targetAvailable)
        return;

    SoundLayerState state = currentSoundState;
    state.eqTone = static_cast<float>(eqSlider.getValue());
    state.compression = static_cast<float>(compSlider.getValue());
    state.reverb = static_cast<float>(reverbSlider.getValue());
    state.gate = static_cast<float>(gateSlider.getValue());
    state.transient = static_cast<float>(transientSlider.getValue());
    state.drive = static_cast<float>(driveSlider.getValue());
    currentSoundState = state;
    updateValueLabels();
    onSoundLayerChanged(currentTarget, state);
}
} // namespace bbg
