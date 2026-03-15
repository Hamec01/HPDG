#include "MainHeaderComponent.h"

namespace bbg
{
MainHeaderComponent::MainHeaderComponent()
{
    titleLabel.setText("HPDG", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("HamloProdDrumGenerator", juce::dontSendNotification);
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(170, 176, 186));
    subtitleLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(subtitleLabel);

    bpmLabel.setText("BPM", juce::dontSendNotification);
    addAndMakeVisible(bpmLabel);
    setupSlider(bpmSlider, 60.0, 180.0, 0.1, " BPM");

    addAndMakeVisible(syncTempoToggle);

    swingLabel.setText("Swing", juce::dontSendNotification);
    addAndMakeVisible(swingLabel);
    setupSlider(swingSlider, 50.0, 75.0, 0.1, "%");

    velocityLabel.setText("Velocity", juce::dontSendNotification);
    addAndMakeVisible(velocityLabel);
    setupSlider(velocitySlider, 0.0, 1.0, 0.01);

    timingLabel.setText("Timing", juce::dontSendNotification);
    addAndMakeVisible(timingLabel);
    setupSlider(timingSlider, 0.0, 1.0, 0.01);

    humanizeLabel.setText("Humanize", juce::dontSendNotification);
    addAndMakeVisible(humanizeLabel);
    setupSlider(humanizeSlider, 0.0, 1.0, 0.01);

    densityLabel.setText("Density", juce::dontSendNotification);
    addAndMakeVisible(densityLabel);
    setupSlider(densitySlider, 0.0, 1.0, 0.01);

    tempoInterpretationLabel.setText("Tempo Mode", juce::dontSendNotification);
    addAndMakeVisible(tempoInterpretationLabel);
    tempoInterpretationCombo.addItem("Auto", 1);
    tempoInterpretationCombo.addItem("Original", 2);
    tempoInterpretationCombo.addItem("Half-time", 3);
    tempoInterpretationCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(tempoInterpretationCombo);

    barsLabel.setText("Bars", juce::dontSendNotification);
    addAndMakeVisible(barsLabel);
    barsCombo.addItem("1", 1);
    barsCombo.addItem("2", 2);
    barsCombo.addItem("4", 3);
    barsCombo.addItem("8", 4);
    barsCombo.addItem("16", 5);
    addAndMakeVisible(barsCombo);

    genreLabel.setText("Genre", juce::dontSendNotification);
    addAndMakeVisible(genreLabel);
    genreCombo.addItem("Boom Bap", 1);
    genreCombo.addItem("Rap", 2);
    genreCombo.addItem("Trap", 3);
    genreCombo.addItem("Drill", 4);
    addAndMakeVisible(genreCombo);

    substyleLabel.setText("Substyle", juce::dontSendNotification);
    addAndMakeVisible(substyleLabel);
    substyleCombo.addItem("Classic", 1);
    substyleCombo.addItem("Dusty", 2);
    substyleCombo.addItem("Jazzy", 3);
    substyleCombo.addItem("Aggressive", 4);
    substyleCombo.addItem("LaidBack", 5);
    substyleCombo.addItem("BoomBapGold", 6);
    substyleCombo.addItem("RussianUnderground", 7);
    addAndMakeVisible(substyleCombo);

    seedLabel.setText("Seed", juce::dontSendNotification);
    addAndMakeVisible(seedLabel);
    setupSlider(seedSlider, 1.0, 999999.0, 1.0);

    gridResolutionLabel.setText("Grid", juce::dontSendNotification);
    addAndMakeVisible(gridResolutionLabel);
    gridResolutionCombo.addItem("1/8", 1);
    gridResolutionCombo.addItem("1/16", 2);
    gridResolutionCombo.addItem("1/32", 3);
    gridResolutionCombo.addItem("1/64", 4);
    gridResolutionCombo.addItem("Triplet", 5);
    gridResolutionCombo.setSelectedId(2, juce::dontSendNotification);
    gridModeIndicatorLabel.setText("Grid: 1/16", juce::dontSendNotification);
    gridModeIndicatorLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(170, 176, 186));
    addAndMakeVisible(gridModeIndicatorLabel);
    gridResolutionCombo.onChange = [this]
    {
        const auto selected = gridResolutionCombo.getText();
        gridModeIndicatorLabel.setText("Grid: " + selected, juce::dontSendNotification);
        if (onGridResolutionChanged)
            onGridResolutionChanged(gridResolutionCombo.getSelectedId());
    };
    addAndMakeVisible(gridResolutionCombo);

    addAndMakeVisible(standaloneWindowButton);
    standaloneWindowButton.onClick = [this]
    {
        if (onToggleStandaloneWindow)
            onToggleStandaloneWindow();
    };
    standaloneWindowButton.setVisible(false);

    masterSectionLabel.setText("MASTER", juce::dontSendNotification);
    masterSectionLabel.setJustificationType(juce::Justification::centredLeft);
    masterSectionLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(200, 206, 214));
    addAndMakeVisible(masterSectionLabel);

    masterVolumeLabel.setText("Vol", juce::dontSendNotification);
    addAndMakeVisible(masterVolumeLabel);
    setupSlider(masterVolumeSlider, 0.0, 1.5, 0.01);

    masterCompressorLabel.setText("Comp", juce::dontSendNotification);
    addAndMakeVisible(masterCompressorLabel);
    setupSlider(masterCompressorSlider, 0.0, 1.0, 0.01);

    masterLofiLabel.setText("LoFi", juce::dontSendNotification);
    addAndMakeVisible(masterLofiLabel);
    setupSlider(masterLofiSlider, 0.0, 1.0, 0.01);

    zoomLabel.setText("Zoom", juce::dontSendNotification);
    addAndMakeVisible(zoomLabel);
    setupSlider(zoomSlider, 0.6, 2.6, 0.05);
    zoomSlider.setValue(1.0, juce::dontSendNotification);

    laneHeightLabel.setText("Lane H", juce::dontSendNotification);
    addAndMakeVisible(laneHeightLabel);
    setupSlider(laneHeightSlider, 22.0, 64.0, 1.0);
    laneHeightSlider.setValue(30.0, juce::dontSendNotification);

    addAndMakeVisible(seedLockToggle);
    addAndMakeVisible(playButton);
    addAndMakeVisible(mutateButton);
    addAndMakeVisible(exportFullButton);
    addAndMakeVisible(dragFullButton);
    addAndMakeVisible(generateButton);

    generateButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(232, 153, 66));
    generateButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(18, 19, 22));
    playButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(74, 122, 186));
    mutateButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(156, 124, 58));
    exportFullButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(54, 59, 70));
    dragFullButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(54, 59, 70));

    generateButton.onClick = [this]
    {
        if (onGeneratePressed)
            onGeneratePressed();
    };

    exportFullButton.onClick = [this]
    {
        if (onExportFullPressed)
            onExportFullPressed();
    };

    dragFullButton.onClickAction = [this]
    {
        if (onDragFullPressed)
            onDragFullPressed();
    };

    dragFullButton.onDragAction = [this]
    {
        if (onDragFullGesture)
            onDragFullGesture();
    };

    playButton.onClick = [this]
    {
        if (onPlayToggled)
            onPlayToggled(playButton.getButtonText() == "Play");
    };

    mutateButton.onClick = [this]
    {
        if (onMutatePressed)
            onMutatePressed();
    };

    const auto zoomChanged = [this]
    {
        if (onZoomChanged)
            onZoomChanged(static_cast<float>(zoomSlider.getValue()), static_cast<float>(laneHeightSlider.getValue()));
    };
    zoomSlider.onValueChange = zoomChanged;
    laneHeightSlider.onValueChange = zoomChanged;
}

void MainHeaderComponent::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGB(30, 33, 38), area.getTopLeft(),
                                           juce::Colour::fromRGB(20, 22, 26), area.getBottomLeft(), false));
    g.fillRoundedRectangle(area.reduced(0.5f), 10.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 20));
    g.drawRoundedRectangle(area.reduced(0.5f), 10.0f, 1.0f);

    g.setColour(juce::Colour::fromRGBA(232, 153, 66, 40));
    g.fillRect(0, 0, getWidth(), 2);
}

void MainHeaderComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto masterArea = area.removeFromRight(290);
    area.removeFromRight(6);

    auto titleArea = area.removeFromLeft(210).reduced(4);
    titleLabel.setBounds(titleArea.removeFromTop(26));
    subtitleLabel.setBounds(titleArea.removeFromTop(18));

    auto topRow = area.removeFromTop(38);
    area.removeFromTop(3);
    auto bottomRow = area.removeFromTop(38);

    const auto placeSlider = [](juce::Rectangle<int>& row, int width, juce::Label& label, juce::Component& comp)
    {
        auto slot = row.removeFromLeft(width);
        label.setBounds(slot.removeFromTop(15));
        comp.setBounds(slot.removeFromTop(21));
    };

    placeSlider(topRow, 114, bpmLabel, bpmSlider);
    syncTempoToggle.setBounds(topRow.removeFromLeft(70).reduced(2));
    placeSlider(topRow, 108, swingLabel, swingSlider);
    placeSlider(topRow, 108, velocityLabel, velocitySlider);
    placeSlider(topRow, 108, timingLabel, timingSlider);
    placeSlider(topRow, 108, humanizeLabel, humanizeSlider);
    placeSlider(topRow, 108, densityLabel, densitySlider);
    placeSlider(topRow, 88, tempoInterpretationLabel, tempoInterpretationCombo);
    placeSlider(topRow, 70, barsLabel, barsCombo);
    placeSlider(topRow, 92, genreLabel, genreCombo);
    placeSlider(topRow, 98, substyleLabel, substyleCombo);

    placeSlider(bottomRow, 126, seedLabel, seedSlider);
    placeSlider(bottomRow, 84, gridResolutionLabel, gridResolutionCombo);
    seedLockToggle.setBounds(bottomRow.removeFromLeft(84).reduced(2));
    placeSlider(bottomRow, 112, zoomLabel, zoomSlider);
    gridModeIndicatorLabel.setBounds(bottomRow.removeFromLeft(78).reduced(2));
    placeSlider(bottomRow, 116, laneHeightLabel, laneHeightSlider);
    generateButton.setBounds(bottomRow.removeFromLeft(128).reduced(2));
    mutateButton.setBounds(bottomRow.removeFromLeft(84).reduced(2));
    playButton.setBounds(bottomRow.removeFromLeft(96).reduced(2));
    exportFullButton.setBounds(bottomRow.removeFromLeft(98).reduced(2));
    dragFullButton.setBounds(bottomRow.removeFromLeft(86).reduced(2));

    auto masterTop = masterArea.removeFromTop(18);
    masterSectionLabel.setBounds(masterTop.removeFromLeft(74));
    standaloneWindowButton.setBounds(masterTop.removeFromRight(56).reduced(2));

    masterArea.removeFromTop(4);
    auto masterRow = masterArea.removeFromTop(58);

    const auto placeMaster = [](juce::Rectangle<int>& row, int width, juce::Label& label, juce::Slider& slider)
    {
        auto slot = row.removeFromLeft(width);
        label.setBounds(slot.removeFromTop(15));
        slider.setBounds(slot.removeFromTop(21));
    };

    placeMaster(masterRow, 96, masterVolumeLabel, masterVolumeSlider);
    placeMaster(masterRow, 96, masterCompressorLabel, masterCompressorSlider);
    placeMaster(masterRow, 96, masterLofiLabel, masterLofiSlider);
}

void MainHeaderComponent::setBpmLocked(bool locked)
{
    bpmSlider.setEnabled(!locked);
    bpmSlider.setAlpha(locked ? 0.55f : 1.0f);
}

void MainHeaderComponent::setPreviewPlaying(bool isPlaying)
{
    playButton.setButtonText(isPlaying ? "Stop" : "Play");
    playButton.setColour(juce::TextButton::buttonColourId,
                         isPlaying ? juce::Colour::fromRGB(98, 54, 54)
                                   : juce::Colour::fromRGB(74, 122, 186));
}

void MainHeaderComponent::setStandaloneWindowMaximized(bool isMaximized)
{
    standaloneWindowButton.setButtonText(isMaximized ? "Restore" : "Max");
}

void MainHeaderComponent::setStandaloneWindowButtonVisible(bool visible)
{
    standaloneWindowButton.setVisible(visible);
}

void MainHeaderComponent::setupSlider(juce::Slider& slider, double min, double max, double step, const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 18);
    slider.setRange(min, max, step);
    if (suffix.isNotEmpty())
        slider.setTextValueSuffix(suffix);
    addAndMakeVisible(slider);
}
} // namespace bbg
