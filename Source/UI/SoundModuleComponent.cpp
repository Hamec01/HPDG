#include "SoundModuleComponent.h"

#include <algorithm>
#include <vector>

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

juce::Colour cardFillForIndex(int index)
{
    switch (index)
    {
        case 0: return juce::Colour::fromRGB(19, 29, 28);
        case 1: return juce::Colour::fromRGB(31, 25, 20);
        case 2: return juce::Colour::fromRGB(25, 23, 34);
        case 3: return juce::Colour::fromRGB(19, 23, 31);
        default: return juce::Colour::fromRGB(22, 25, 32);
    }
}

juce::Colour cardOutlineForIndex(int index)
{
    switch (index)
    {
        case 0: return juce::Colour::fromRGBA(136, 204, 174, 86);
        case 1: return juce::Colour::fromRGBA(248, 198, 154, 86);
        case 2: return juce::Colour::fromRGBA(206, 194, 250, 90);
        case 3: return juce::Colour::fromRGBA(152, 200, 255, 80);
        default: return juce::Colour::fromRGBA(255, 255, 255, 38);
    }
}

juce::String eqShapeToString(EqBandShape shape)
{
    switch (shape)
    {
        case EqBandShape::LowCut: return "LowCut";
        case EqBandShape::HighCut: return "HighCut";
        default: return "Bell";
    }
}

EqBandShape eqShapeFromComboId(int id)
{
    switch (id)
    {
        case 1: return EqBandShape::LowCut;
        case 3: return EqBandShape::HighCut;
        default: return EqBandShape::Bell;
    }
}
}

SoundModuleComponent::SoundModuleComponent()
{
    titleLabel.setText("SOUND MODULE", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(226, 231, 238));
    titleLabel.setFont(juce::Font(12.5f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    targetLabel.setText("Target", juce::dontSendNotification);
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

    targetStatusLabel.setText("Routing: shared sound shaping for the full kit.", juce::dontSendNotification);
    targetStatusLabel.setJustificationType(juce::Justification::centredLeft);
    targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 200, 255));
    targetStatusLabel.setFont(juce::Font(10.5f));
    addAndMakeVisible(targetStatusLabel);

    chainSummaryLabel.setText("1 EQ -> 2 COMP -> 3 REVERB -> 4 TRANSIENT", juce::dontSendNotification);
    chainSummaryLabel.setJustificationType(juce::Justification::centredRight);
    chainSummaryLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(188, 196, 210));
    chainSummaryLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(chainSummaryLabel);

    targetCombo.setTooltip("Choose whether the Sound Module edits the global layer or a specific lane/track target.");
    addAndMakeVisible(targetCombo);

    bypassAllButton.setButtonText("Bypass All");
    bypassAllButton.setTooltip("Disable EQ, compressor, reverb, and transient processing for the current target.");
    addAndMakeVisible(bypassAllButton);

    resetTargetButton.setButtonText("Reset Target FX");
    resetTargetButton.setTooltip("Reset the current target to the default Sound Module state.");
    addAndMakeVisible(resetTargetButton);

    setupSectionTitle(compressorTitleLabel, "COMPRESSOR");
    setupToggle(compressorEnableButton, "Enable");
    populateOrderCombo(compressorOrderCombo);
    addAndMakeVisible(compressorOrderCombo);

    const std::array<juce::String, 6> compressorNames { "Ratio", "Threshold", "Mix", "Attack", "Release", "Saturation" };
    for (size_t index = 0; index < compressorLabels.size(); ++index)
    {
        setupControlLabel(compressorLabels[index], compressorNames[index]);
        addAndMakeVisible(compressorLabels[index]);
        addAndMakeVisible(compressorSliders[index]);
    }
    setupLinearSlider(compressorSliders[0], 1.0, 20.0, 0.1);
    setupLinearSlider(compressorSliders[1], -60.0, 6.0, 0.1, " dB");
    setupLinearSlider(compressorSliders[2], 0.0, 1.0, 0.01);
    setupLinearSlider(compressorSliders[3], 0.1, 250.0, 0.1, " ms");
    setupLinearSlider(compressorSliders[4], 5.0, 2000.0, 1.0, " ms");
    setupLinearSlider(compressorSliders[5], 0.0, 1.0, 0.01);

    setupSectionTitle(reverbTitleLabel, "REVERB");
    setupToggle(reverbEnableButton, "Enable");
    populateOrderCombo(reverbOrderCombo);
    addAndMakeVisible(reverbOrderCombo);

    const std::array<juce::String, 4> reverbNames { "Mix", "Predelay", "Size", "ER/Tail" };
    for (size_t index = 0; index < reverbLabels.size(); ++index)
    {
        setupControlLabel(reverbLabels[index], reverbNames[index]);
        addAndMakeVisible(reverbLabels[index]);
        addAndMakeVisible(reverbSliders[index]);
    }
    setupLinearSlider(reverbSliders[0], 0.0, 1.0, 0.01);
    setupLinearSlider(reverbSliders[1], 0.0, 250.0, 0.1, " ms");
    setupLinearSlider(reverbSliders[2], 0.0, 1.0, 0.01);
    setupLinearSlider(reverbSliders[3], 0.0, 1.0, 0.01);

    setupSectionTitle(transientTitleLabel, "TRANSIENT MASTER");
    setupToggle(transientEnableButton, "Enable");
    populateOrderCombo(transientOrderCombo);
    addAndMakeVisible(transientOrderCombo);

    const std::array<juce::String, 3> transientNames { "Attack", "Sustain", "Gain" };
    for (size_t index = 0; index < transientSliderLabels.size(); ++index)
    {
        setupControlLabel(transientSliderLabels[index], transientNames[index]);
        addAndMakeVisible(transientSliderLabels[index]);
        addAndMakeVisible(transientSliders[index]);
    }
    setupLinearSlider(transientSliders[0], -1.0, 1.0, 0.01);
    setupLinearSlider(transientSliders[1], -1.0, 1.0, 0.01);
    setupLinearSlider(transientSliders[2], -24.0, 24.0, 0.1, " dB");
    setupToggle(transientSmoothButton, "Smooth");
    setupToggle(transientLimitButton, "Limit");

    setupSectionTitle(eqTitleLabel, "EQ");
    setupToggle(eqEnableButton, "Enable");
    populateOrderCombo(eqOrderCombo);
    addAndMakeVisible(eqOrderCombo);

    eqPlaceholderLabel.setText("EQ Display (waveform/curve in later phase)", juce::dontSendNotification);
    eqPlaceholderLabel.setJustificationType(juce::Justification::centred);
    eqPlaceholderLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(173, 188, 212));
    eqPlaceholderLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(eqPlaceholderLabel);

    setupControlLabel(eqBandLabel, "Band");
    addAndMakeVisible(eqBandLabel);
    addAndMakeVisible(eqBandSelector);
    setupToggle(eqBandEnableButton, "Band Enabled");

    setupControlLabel(eqFreqLabel, "Freq");
    setupLinearSlider(eqFreqSlider, 20.0, 22000.0, 1.0, " Hz");
    eqFreqSlider.setSkewFactorFromMidPoint(1000.0);
    addAndMakeVisible(eqFreqLabel);
    addAndMakeVisible(eqFreqSlider);

    setupControlLabel(eqGainLabel, "Gain");
    setupLinearSlider(eqGainSlider, -24.0, 24.0, 0.1, " dB");
    addAndMakeVisible(eqGainLabel);
    addAndMakeVisible(eqGainSlider);

    setupControlLabel(eqQLabel, "Q");
    setupLinearSlider(eqQSlider, 0.1, 12.0, 0.01);
    addAndMakeVisible(eqQLabel);
    addAndMakeVisible(eqQSlider);

    setupControlLabel(eqShapeLabel, "Shape");
    eqShapeCombo.addItem("LowCut", 1);
    eqShapeCombo.addItem("Bell", 2);
    eqShapeCombo.addItem("HighCut", 3);
    addAndMakeVisible(eqShapeLabel);
    addAndMakeVisible(eqShapeCombo);

    for (int bandIndex = 0; bandIndex < 7; ++bandIndex)
        eqBandSelector.addItem("Band " + juce::String(bandIndex + 1), bandIndex + 1);

    setupSectionTitle(stereoTitleLabel, "STEREO / PLACEMENT");
    setupControlLabel(panLabel, "Pan");
    setupControlLabel(widthLabel, "Width");
    setupLinearSlider(panSlider, -1.0, 1.0, 0.01);
    setupLinearSlider(widthSlider, 0.0, 2.0, 0.01);
    addAndMakeVisible(panLabel);
    addAndMakeVisible(panSlider);
    addAndMakeVisible(widthLabel);
    addAndMakeVisible(widthSlider);

    targetCombo.onChange = [this]
    {
        if (suppressCallbacks)
            return;

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

    auto onAnyControlChanged = [this]
    {
        if (!suppressCallbacks)
            emitSoundLayerChange();
    };

    compressorEnableButton.onClick = onAnyControlChanged;
    compressorOrderCombo.onChange = onAnyControlChanged;
    for (auto& slider : compressorSliders)
        slider.onValueChange = onAnyControlChanged;

    reverbEnableButton.onClick = onAnyControlChanged;
    reverbOrderCombo.onChange = onAnyControlChanged;
    for (auto& slider : reverbSliders)
        slider.onValueChange = onAnyControlChanged;

    transientEnableButton.onClick = onAnyControlChanged;
    transientOrderCombo.onChange = onAnyControlChanged;
    for (auto& slider : transientSliders)
        slider.onValueChange = onAnyControlChanged;
    transientSmoothButton.onClick = onAnyControlChanged;
    transientLimitButton.onClick = onAnyControlChanged;

    eqEnableButton.onClick = onAnyControlChanged;
    eqOrderCombo.onChange = onAnyControlChanged;
    eqBandEnableButton.onClick = onAnyControlChanged;
    eqFreqSlider.onValueChange = onAnyControlChanged;
    eqGainSlider.onValueChange = onAnyControlChanged;
    eqQSlider.onValueChange = onAnyControlChanged;
    eqShapeCombo.onChange = onAnyControlChanged;
    panSlider.onValueChange = onAnyControlChanged;
    widthSlider.onValueChange = onAnyControlChanged;

    eqBandSelector.onChange = [this]
    {
        if (suppressCallbacks)
            return;

        currentSoundState.eq.selectedBand = juce::jlimit(0, static_cast<int>(currentSoundState.eq.bands.size()) - 1,
                                                         eqBandSelector.getSelectedId() - 1);
        refreshControlsFromState();
        emitSoundLayerChange();
    };

    bypassAllButton.onClick = [this]
    {
        if (!targetAvailable)
            return;

        currentSoundState.eq.enabled = false;
        currentSoundState.compressor.enabled = false;
        currentSoundState.reverbState.enabled = false;
        currentSoundState.transientState.enabled = false;
        currentSoundState.sanitizeExpanded();
        currentSoundState.syncLegacyFromExpanded();
        refreshControlsFromState();
        emitSoundLayerChange();
    };

    resetTargetButton.onClick = [this]
    {
        if (!targetAvailable)
            return;

        currentSoundState = SoundLayerState {};
        currentSoundState.sanitizeExpanded();
        currentSoundState.syncLegacyFromExpanded();
        refreshControlsFromState();
        emitSoundLayerChange();
    };

    refreshControlsFromState();
    updateChainSummary();
}

void SoundModuleComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(13, 16, 22));

    for (size_t index = 0; index < moduleGroupBounds.size(); ++index)
    {
        const auto bounds = moduleGroupBounds[index].toFloat();
        if (bounds.isEmpty())
            continue;

        g.setColour(cardFillForIndex(static_cast<int>(index)));
        g.fillRoundedRectangle(bounds, 7.0f);
        g.setColour(cardOutlineForIndex(static_cast<int>(index)));
        g.drawRoundedRectangle(bounds, 7.0f, 1.0f);
    }

    const std::array<juce::Rectangle<int>, 3> rightCards { eqDisplayBounds, eqEditorBounds, stereoBounds };
    for (size_t index = 0; index < rightCards.size(); ++index)
    {
        const auto bounds = rightCards[index].toFloat();
        if (bounds.isEmpty())
            continue;

        g.setColour(cardFillForIndex(static_cast<int>(index) + 3));
        g.fillRoundedRectangle(bounds, 7.0f);
        g.setColour(cardOutlineForIndex(static_cast<int>(index) + 3));
        g.drawRoundedRectangle(bounds, 7.0f, 1.0f);
    }

    if (!eqDisplayBounds.isEmpty())
    {
        auto placeholderBounds = eqDisplayBounds.reduced(12, 34).toFloat();
        g.setColour(juce::Colour::fromRGBA(152, 200, 255, 22));
        g.fillRoundedRectangle(placeholderBounds, 8.0f);
        g.setColour(juce::Colour::fromRGBA(152, 200, 255, 76));
        g.drawRoundedRectangle(placeholderBounds, 8.0f, 1.0f);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
        const auto centerY = placeholderBounds.getCentreY();
        g.drawLine(placeholderBounds.getX() + 18.0f, centerY,
                   placeholderBounds.getRight() - 18.0f, centerY, 1.0f);
        g.drawLine(placeholderBounds.getCentreX(), placeholderBounds.getY() + 16.0f,
                   placeholderBounds.getCentreX(), placeholderBounds.getBottom() - 16.0f, 1.0f);
    }

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 8.0f, 1.0f);
}

void SoundModuleComponent::resized()
{
    auto area = getLocalBounds().reduced(8);

    auto topRow = area.removeFromTop(26);
    titleLabel.setBounds(topRow.removeFromLeft(132));
    targetLabel.setBounds(topRow.removeFromLeft(42));
    targetCombo.setBounds(topRow.removeFromLeft(190));
    topRow.removeFromLeft(6);
    resetTargetButton.setBounds(topRow.removeFromRight(112));
    topRow.removeFromRight(6);
    bypassAllButton.setBounds(topRow.removeFromRight(92));

    area.removeFromTop(4);
    auto summaryRow = area.removeFromTop(20);
    targetSummaryLabel.setBounds(summaryRow.removeFromLeft(210));
    targetModeLabel.setBounds(summaryRow.removeFromLeft(74));
    chainSummaryLabel.setBounds(summaryRow);

    area.removeFromTop(2);
    targetStatusLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);

    auto body = area;
    auto leftColumn = body.removeFromLeft(body.getWidth() * 44 / 100);
    body.removeFromLeft(8);
    auto rightColumn = body;

    const int leftGap = 8;
    const int leftHeight = (leftColumn.getHeight() - leftGap * 2) / 3;
    moduleGroupBounds[0] = leftColumn.removeFromTop(leftHeight);
    leftColumn.removeFromTop(leftGap);
    moduleGroupBounds[1] = leftColumn.removeFromTop(leftHeight);
    leftColumn.removeFromTop(leftGap);
    moduleGroupBounds[2] = leftColumn;

    const auto layoutModule = [](juce::Rectangle<int> bounds,
                                 juce::Label& title,
                                 juce::ToggleButton& enabled,
                                 juce::ComboBox& order,
                                 const std::vector<std::pair<juce::Label*, juce::Slider*>>& controls,
                                 const std::vector<juce::Component*>& footer)
    {
        auto inner = bounds.reduced(10, 8);
        auto header = inner.removeFromTop(22);
        title.setBounds(header.removeFromLeft(header.getWidth() / 2));
        auto orderArea = header.removeFromRight(64);
        order.setBounds(orderArea);
        header.removeFromRight(8);
        enabled.setBounds(header.removeFromRight(72));

        inner.removeFromTop(6);
        const int columns = 2;
        const int rows = static_cast<int>((controls.size() + columns - 1) / columns);
        const int footerHeight = footer.empty() ? 0 : 28;
        const int rowGap = 6;
        const int cellHeight = rows > 0
            ? (inner.getHeight() - footerHeight - rowGap * (rows - 1)) / rows
            : 0;

        size_t controlIndex = 0;
        for (int row = 0; row < rows; ++row)
        {
            auto rowArea = inner.removeFromTop(cellHeight);
            if (row < rows - 1)
                inner.removeFromTop(rowGap);

            const int columnGap = 8;
            const int cellWidth = (rowArea.getWidth() - columnGap) / columns;
            for (int column = 0; column < columns && controlIndex < controls.size(); ++column, ++controlIndex)
            {
                auto cell = rowArea.removeFromLeft(cellWidth);
                if (column == 0)
                    rowArea.removeFromLeft(columnGap);

                controls[controlIndex].first->setBounds(cell.removeFromTop(14));
                cell.removeFromTop(2);
                controls[controlIndex].second->setBounds(cell.removeFromTop(22));
            }
        }

        if (!footer.empty())
        {
            inner.removeFromTop(4);
            auto footerRow = inner.removeFromTop(24);
            const int gap = 10;
            const int width = (footerRow.getWidth() - gap * static_cast<int>(footer.size() - 1)) / static_cast<int>(footer.size());
            for (size_t index = 0; index < footer.size(); ++index)
            {
                footer[index]->setBounds(footerRow.removeFromLeft(width));
                if (index + 1 < footer.size())
                    footerRow.removeFromLeft(gap);
            }
        }
    };

    layoutModule(moduleGroupBounds[0],
                 compressorTitleLabel,
                 compressorEnableButton,
                 compressorOrderCombo,
                 {
                     { &compressorLabels[0], &compressorSliders[0] },
                     { &compressorLabels[1], &compressorSliders[1] },
                     { &compressorLabels[2], &compressorSliders[2] },
                     { &compressorLabels[3], &compressorSliders[3] },
                     { &compressorLabels[4], &compressorSliders[4] },
                     { &compressorLabels[5], &compressorSliders[5] }
                 },
                 {});

    layoutModule(moduleGroupBounds[1],
                 reverbTitleLabel,
                 reverbEnableButton,
                 reverbOrderCombo,
                 {
                     { &reverbLabels[0], &reverbSliders[0] },
                     { &reverbLabels[1], &reverbSliders[1] },
                     { &reverbLabels[2], &reverbSliders[2] },
                     { &reverbLabels[3], &reverbSliders[3] }
                 },
                 {});

    layoutModule(moduleGroupBounds[2],
                 transientTitleLabel,
                 transientEnableButton,
                 transientOrderCombo,
                 {
                     { &transientSliderLabels[0], &transientSliders[0] },
                     { &transientSliderLabels[1], &transientSliders[1] },
                     { &transientSliderLabels[2], &transientSliders[2] }
                 },
                 { &transientSmoothButton, &transientLimitButton });

    const int rightGap = 8;
    const int displayHeight = rightColumn.getHeight() * 46 / 100;
    const int stereoHeight = 78;
    eqDisplayBounds = rightColumn.removeFromTop(displayHeight);
    rightColumn.removeFromTop(rightGap);
    eqEditorBounds = rightColumn.removeFromTop(rightColumn.getHeight() - stereoHeight - rightGap);
    rightColumn.removeFromTop(rightGap);
    stereoBounds = rightColumn;

    auto eqDisplayInner = eqDisplayBounds.reduced(10, 8);
    auto eqHeader = eqDisplayInner.removeFromTop(22);
    eqTitleLabel.setBounds(eqHeader.removeFromLeft(eqHeader.getWidth() / 2));
    eqOrderCombo.setBounds(eqHeader.removeFromRight(64));
    eqHeader.removeFromRight(8);
    eqEnableButton.setBounds(eqHeader.removeFromRight(72));
    eqDisplayInner.removeFromTop(6);
    eqPlaceholderLabel.setBounds(eqDisplayInner);

    auto eqEditorInner = eqEditorBounds.reduced(10, 8);
    auto eqBandRow = eqEditorInner.removeFromTop(22);
    eqBandLabel.setBounds(eqBandRow.removeFromLeft(36));
    eqBandSelector.setBounds(eqBandRow.removeFromLeft(92));
    eqBandRow.removeFromLeft(10);
    eqBandEnableButton.setBounds(eqBandRow.removeFromLeft(112));

    eqEditorInner.removeFromTop(8);
    auto eqRowOne = eqEditorInner.removeFromTop(40);
    auto eqRowTwo = eqEditorInner.removeFromTop(40);
    const int eqGap = 8;
    const int eqCellWidth = (eqRowOne.getWidth() - eqGap * 2) / 3;

    auto placeSliderCell = [eqGap](juce::Rectangle<int>& row, juce::Label& label, juce::Slider& slider, int width)
    {
        auto cell = row.removeFromLeft(width);
        label.setBounds(cell.removeFromTop(14));
        cell.removeFromTop(2);
        slider.setBounds(cell.removeFromTop(22));
        row.removeFromLeft(eqGap);
    };

    placeSliderCell(eqRowOne, eqFreqLabel, eqFreqSlider, eqCellWidth);
    placeSliderCell(eqRowOne, eqGainLabel, eqGainSlider, eqCellWidth);
    placeSliderCell(eqRowOne, eqQLabel, eqQSlider, eqCellWidth);

    eqShapeLabel.setBounds(eqRowTwo.removeFromLeft(46));
    eqShapeCombo.setBounds(eqRowTwo.removeFromLeft(112));

    auto stereoInner = stereoBounds.reduced(10, 8);
    stereoTitleLabel.setBounds(stereoInner.removeFromTop(18));
    stereoInner.removeFromTop(6);
    auto stereoRow = stereoInner.removeFromTop(32);
    auto panCell = stereoRow.removeFromLeft((stereoRow.getWidth() - 8) / 2);
    stereoRow.removeFromLeft(8);
    auto widthCell = stereoRow;
    panLabel.setBounds(panCell.removeFromTop(14));
    panSlider.setBounds(panCell.removeFromTop(18));
    widthLabel.setBounds(widthCell.removeFromTop(14));
    widthSlider.setBounds(widthCell.removeFromTop(18));
}

void SoundModuleComponent::setState(const std::vector<TrackState>& tracks,
                                    const SoundTargetDescriptor& selectedTarget,
                                    const SoundLayerState& soundState)
{
    targetDescriptors.clear();
    targetCombo.clear(juce::dontSendNotification);
    targetCombo.addItem("Global", 1);

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
    targetAvailable = selectedTarget.isGlobal() || comboContainsDescriptor(targetDescriptors, selectedTarget);
    if (targetAvailable)
    {
        currentSoundState = soundState;
        currentSoundState.sanitizeExpanded();
        currentSoundState.syncLegacyFromExpanded();
        suppressCallbacks = true;
        targetCombo.setSelectedId(selectedId, juce::dontSendNotification);
        suppressCallbacks = false;
    }
    else
    {
        currentSoundState = SoundLayerState {};
        currentSoundState.sanitizeExpanded();
        suppressCallbacks = true;
        targetCombo.setSelectedId(0, juce::dontSendNotification);
        targetCombo.setText("Unavailable: " + displayNameForDescriptor(selectedTarget), juce::dontSendNotification);
        suppressCallbacks = false;
    }

    refreshControlsFromState();
    updateTargetPresentation(tracks, selectedTarget, targetMatchedInCombo, matchedTargetName);
    setControlsEnabled(targetAvailable);
    updateChainSummary();
}

void SoundModuleComponent::setupSectionTitle(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(222, 228, 236));
    label.setFont(juce::Font(10.5f, juce::Font::bold));
    addAndMakeVisible(label);
}

void SoundModuleComponent::setupControlLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(166, 176, 192));
    label.setFont(juce::Font(10.0f));
}

void SoundModuleComponent::setupLinearSlider(juce::Slider& slider,
                                             double min,
                                             double max,
                                             double step,
                                             const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 18);
    slider.setRange(min, max, step);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(126, 158, 202));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(214, 222, 236));
    slider.setColour(juce::Slider::backgroundColourId, juce::Colour::fromRGBA(255, 255, 255, 18));
}

void SoundModuleComponent::setupToggle(juce::ToggleButton& button, const juce::String& text, const juce::String& tooltip)
{
    button.setButtonText(text);
    button.setTooltip(tooltip);
    button.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(214, 222, 234));
    addAndMakeVisible(button);
}

void SoundModuleComponent::populateOrderCombo(juce::ComboBox& combo)
{
    for (int order = 1; order <= 8; ++order)
        combo.addItem(juce::String(order), order);
}

void SoundModuleComponent::setControlsEnabled(bool shouldEnable)
{
    compressorEnableButton.setEnabled(shouldEnable);
    compressorOrderCombo.setEnabled(shouldEnable);
    for (auto& slider : compressorSliders)
        slider.setEnabled(shouldEnable);

    reverbEnableButton.setEnabled(shouldEnable);
    reverbOrderCombo.setEnabled(shouldEnable);
    for (auto& slider : reverbSliders)
        slider.setEnabled(shouldEnable);

    transientEnableButton.setEnabled(shouldEnable);
    transientOrderCombo.setEnabled(shouldEnable);
    for (auto& slider : transientSliders)
        slider.setEnabled(shouldEnable);
    transientSmoothButton.setEnabled(shouldEnable);
    transientLimitButton.setEnabled(shouldEnable);

    eqEnableButton.setEnabled(shouldEnable);
    eqOrderCombo.setEnabled(shouldEnable);
    eqBandSelector.setEnabled(shouldEnable);
    eqBandEnableButton.setEnabled(shouldEnable);
    eqFreqSlider.setEnabled(shouldEnable);
    eqGainSlider.setEnabled(shouldEnable);
    eqQSlider.setEnabled(shouldEnable);
    eqShapeCombo.setEnabled(shouldEnable);
    panSlider.setEnabled(shouldEnable);
    widthSlider.setEnabled(shouldEnable);
    bypassAllButton.setEnabled(shouldEnable);
    resetTargetButton.setEnabled(shouldEnable);
}

void SoundModuleComponent::refreshControlsFromState()
{
    suppressCallbacks = true;

    currentSoundState.sanitizeExpanded();

    panSlider.setValue(currentSoundState.pan, juce::dontSendNotification);
    widthSlider.setValue(currentSoundState.width, juce::dontSendNotification);

    compressorEnableButton.setToggleState(currentSoundState.compressor.enabled, juce::dontSendNotification);
    compressorOrderCombo.setSelectedId(juce::jlimit(1, 8, currentSoundState.compressor.order), juce::dontSendNotification);
    compressorSliders[0].setValue(currentSoundState.compressor.ratio, juce::dontSendNotification);
    compressorSliders[1].setValue(currentSoundState.compressor.thresholdDb, juce::dontSendNotification);
    compressorSliders[2].setValue(currentSoundState.compressor.mix, juce::dontSendNotification);
    compressorSliders[3].setValue(currentSoundState.compressor.attackMs, juce::dontSendNotification);
    compressorSliders[4].setValue(currentSoundState.compressor.releaseMs, juce::dontSendNotification);
    compressorSliders[5].setValue(currentSoundState.compressor.saturation, juce::dontSendNotification);

    reverbEnableButton.setToggleState(currentSoundState.reverbState.enabled, juce::dontSendNotification);
    reverbOrderCombo.setSelectedId(juce::jlimit(1, 8, currentSoundState.reverbState.order), juce::dontSendNotification);
    reverbSliders[0].setValue(currentSoundState.reverbState.mix, juce::dontSendNotification);
    reverbSliders[1].setValue(currentSoundState.reverbState.predelayMs, juce::dontSendNotification);
    reverbSliders[2].setValue(currentSoundState.reverbState.size, juce::dontSendNotification);
    reverbSliders[3].setValue(currentSoundState.reverbState.erTail, juce::dontSendNotification);

    transientEnableButton.setToggleState(currentSoundState.transientState.enabled, juce::dontSendNotification);
    transientOrderCombo.setSelectedId(juce::jlimit(1, 8, currentSoundState.transientState.order), juce::dontSendNotification);
    transientSliders[0].setValue(currentSoundState.transientState.attack, juce::dontSendNotification);
    transientSliders[1].setValue(currentSoundState.transientState.sustain, juce::dontSendNotification);
    transientSliders[2].setValue(currentSoundState.transientState.gainDb, juce::dontSendNotification);
    transientSmoothButton.setToggleState(currentSoundState.transientState.smooth, juce::dontSendNotification);
    transientLimitButton.setToggleState(currentSoundState.transientState.limit, juce::dontSendNotification);

    eqEnableButton.setToggleState(currentSoundState.eq.enabled, juce::dontSendNotification);
    eqOrderCombo.setSelectedId(juce::jlimit(1, 8, currentSoundState.eq.order), juce::dontSendNotification);
    eqBandSelector.setSelectedId(currentSoundState.eq.selectedBand + 1, juce::dontSendNotification);

    const auto& band = currentEqBand();
    eqBandEnableButton.setToggleState(band.enabled, juce::dontSendNotification);
    eqFreqSlider.setValue(band.freqHz, juce::dontSendNotification);
    eqGainSlider.setValue(band.gainDb, juce::dontSendNotification);
    eqQSlider.setValue(band.q, juce::dontSendNotification);
    eqShapeCombo.setSelectedId(band.shape == EqBandShape::LowCut ? 1
                                 : band.shape == EqBandShape::HighCut ? 3
                                                                     : 2,
                             juce::dontSendNotification);

    suppressCallbacks = false;
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
        targetStatusLabel.setText("Target is no longer valid. Choose Global or a visible lane/track before editing.",
                                  juce::dontSendNotification);
        targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(255, 170, 170));
        return;
    }

    if (selectedTarget.isGlobal())
    {
        targetSummaryLabel.setText("Editing: Global", juce::dontSendNotification);
        targetModeLabel.setText("GLOBAL", juce::dontSendNotification);
        targetModeLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(71, 96, 132));
        targetStatusLabel.setText("Routing: shared sound shaping for the full kit.", juce::dontSendNotification);
        targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 200, 255));
        return;
    }

    const auto targetName = targetMatchedInCombo ? matchedTargetName : displayNameForDescriptor(selectedTarget);
    if (selectedTarget.kind == SoundTargetDescriptorKind::BackedRuntimeLane)
    {
        targetSummaryLabel.setText("Editing: " + targetName, juce::dontSendNotification);
        targetModeLabel.setText("LANE", juce::dontSendNotification);
        targetModeLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(78, 112, 84));
        targetStatusLabel.setText("Routing: edits the current backed runtime lane only.", juce::dontSendNotification);
        targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(174, 222, 184));
        return;
    }

    targetSummaryLabel.setText("Editing: " + targetName, juce::dontSendNotification);
    targetModeLabel.setText("TRACK", juce::dontSendNotification);
    targetModeLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(118, 92, 64));
    targetStatusLabel.setText("Routing: edits the selected track target.", juce::dontSendNotification);
    targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(240, 204, 166));
}

void SoundModuleComponent::updateChainSummary()
{
    if (!targetAvailable)
    {
        chainSummaryLabel.setText("Chain unavailable for invalid target", juce::dontSendNotification);
        return;
    }

    struct ChainEntry
    {
        int order;
        juce::String name;
    };

    std::vector<ChainEntry> entries;
    if (currentSoundState.eq.enabled)
        entries.push_back({ juce::jlimit(1, 8, currentSoundState.eq.order), "EQ" });
    if (currentSoundState.compressor.enabled)
        entries.push_back({ juce::jlimit(1, 8, currentSoundState.compressor.order), "COMP" });
    if (currentSoundState.reverbState.enabled)
        entries.push_back({ juce::jlimit(1, 8, currentSoundState.reverbState.order), "REVERB" });
    if (currentSoundState.transientState.enabled)
        entries.push_back({ juce::jlimit(1, 8, currentSoundState.transientState.order), "TRANSIENT" });

    std::sort(entries.begin(), entries.end(), [](const ChainEntry& lhs, const ChainEntry& rhs)
    {
        if (lhs.order == rhs.order)
            return lhs.name < rhs.name;
        return lhs.order < rhs.order;
    });

    if (entries.empty())
    {
        chainSummaryLabel.setText("Chain: Bypassed", juce::dontSendNotification);
        return;
    }

    juce::String summary;
    for (size_t index = 0; index < entries.size(); ++index)
    {
        if (index > 0)
            summary << " -> ";
        summary << juce::String(entries[index].order) << " " << entries[index].name;
    }
    chainSummaryLabel.setText(summary, juce::dontSendNotification);
}

void SoundModuleComponent::emitSoundLayerChange()
{
    if (suppressCallbacks || !targetAvailable)
        return;

    SoundLayerState state = currentSoundState;
    state.pan = static_cast<float>(panSlider.getValue());
    state.width = static_cast<float>(widthSlider.getValue());

    state.compressor.enabled = compressorEnableButton.getToggleState();
    state.compressor.order = juce::jlimit(1, 8, compressorOrderCombo.getSelectedId());
    state.compressor.ratio = static_cast<float>(compressorSliders[0].getValue());
    state.compressor.thresholdDb = static_cast<float>(compressorSliders[1].getValue());
    state.compressor.mix = static_cast<float>(compressorSliders[2].getValue());
    state.compressor.attackMs = static_cast<float>(compressorSliders[3].getValue());
    state.compressor.releaseMs = static_cast<float>(compressorSliders[4].getValue());
    state.compressor.saturation = static_cast<float>(compressorSliders[5].getValue());

    state.reverbState.enabled = reverbEnableButton.getToggleState();
    state.reverbState.order = juce::jlimit(1, 8, reverbOrderCombo.getSelectedId());
    state.reverbState.mix = static_cast<float>(reverbSliders[0].getValue());
    state.reverbState.predelayMs = static_cast<float>(reverbSliders[1].getValue());
    state.reverbState.size = static_cast<float>(reverbSliders[2].getValue());
    state.reverbState.erTail = static_cast<float>(reverbSliders[3].getValue());

    state.transientState.enabled = transientEnableButton.getToggleState();
    state.transientState.order = juce::jlimit(1, 8, transientOrderCombo.getSelectedId());
    state.transientState.attack = static_cast<float>(transientSliders[0].getValue());
    state.transientState.sustain = static_cast<float>(transientSliders[1].getValue());
    state.transientState.gainDb = static_cast<float>(transientSliders[2].getValue());
    state.transientState.smooth = transientSmoothButton.getToggleState();
    state.transientState.limit = transientLimitButton.getToggleState();

    state.eq.enabled = eqEnableButton.getToggleState();
    state.eq.order = juce::jlimit(1, 8, eqOrderCombo.getSelectedId());
    state.eq.selectedBand = juce::jlimit(0, static_cast<int>(state.eq.bands.size()) - 1, eqBandSelector.getSelectedId() - 1);
    auto& band = state.eq.bands[static_cast<size_t>(state.eq.selectedBand)];
    band.enabled = eqBandEnableButton.getToggleState();
    band.freqHz = static_cast<float>(eqFreqSlider.getValue());
    band.gainDb = static_cast<float>(eqGainSlider.getValue());
    band.q = static_cast<float>(eqQSlider.getValue());
    band.shape = eqShapeFromComboId(eqShapeCombo.getSelectedId());

    state.sanitizeExpanded();
    state.syncLegacyFromExpanded();

    currentSoundState = state;
    updateChainSummary();

    if (onSoundLayerChanged)
        onSoundLayerChanged(currentTarget, state);
}

EqBandState& SoundModuleComponent::currentEqBand()
{
    currentSoundState.eq.selectedBand = juce::jlimit(0,
                                                     static_cast<int>(currentSoundState.eq.bands.size()) - 1,
                                                     currentSoundState.eq.selectedBand);
    return currentSoundState.eq.bands[static_cast<size_t>(currentSoundState.eq.selectedBand)];
}

const EqBandState& SoundModuleComponent::currentEqBand() const
{
    const auto index = juce::jlimit(0,
                                    static_cast<int>(currentSoundState.eq.bands.size()) - 1,
                                    currentSoundState.eq.selectedBand);
    return currentSoundState.eq.bands[static_cast<size_t>(index)];
}
} // namespace bbg
