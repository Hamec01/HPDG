#include "MainHeaderComponent.h"

namespace bbg
{
namespace
{
constexpr auto kUiBuildVersion = "0.004V";

double parseKnobText(const juce::String& text)
{
    return text.retainCharacters("0123456789.-").getDoubleValue();
}

void drawHeaderTexture(juce::Graphics& g, juce::Rectangle<float> area, float alphaScale, int seed)
{
    juce::Random rng(seed);

    for (int i = 0; i < 18; ++i)
    {
        const float x = area.getX() + rng.nextFloat() * area.getWidth();
        const float y = area.getY() + rng.nextFloat() * area.getHeight();
        const float w = 34.0f + rng.nextFloat() * 120.0f;
        const float h = 7.0f + rng.nextFloat() * 26.0f;
        g.setColour(juce::Colour::fromRGBA(255, 170, 78,
                                           static_cast<juce::uint8>((0.016f + rng.nextFloat() * 0.020f) * alphaScale * 255.0f)));
        g.fillEllipse(x, y, w, h);
    }

    for (int i = 0; i < 12; ++i)
    {
        juce::Path line;
        line.startNewSubPath(area.getX() + rng.nextFloat() * area.getWidth(),
                             area.getY() + rng.nextFloat() * area.getHeight());
        for (int seg = 0; seg < 3; ++seg)
        {
            line.quadraticTo(area.getX() + rng.nextFloat() * area.getWidth(),
                             area.getY() + rng.nextFloat() * area.getHeight(),
                             area.getX() + rng.nextFloat() * area.getWidth(),
                             area.getY() + rng.nextFloat() * area.getHeight());
        }

        g.setColour(juce::Colour::fromRGBA(255, 220, 180,
                                           static_cast<juce::uint8>((0.008f + rng.nextFloat() * 0.010f) * alphaScale * 255.0f)));
        g.strokePath(line, juce::PathStrokeType(1.2f + rng.nextFloat() * 1.8f,
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
}
}

MainHeaderComponent::MainHeaderComponent()
{
    const auto styleSecondaryLabel = [](juce::Label& label)
    {
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(170, 176, 186));
    };

    titleLabel.setText("HPDG", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("HamloProdDrumGenerator " + juce::String(kUiBuildVersion), juce::dontSendNotification);
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(170, 176, 186));
    subtitleLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(subtitleLabel);

    diagnosticsLabel.setText({}, juce::dontSendNotification);
    diagnosticsLabel.setJustificationType(juce::Justification::centredLeft);
    diagnosticsLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(162, 208, 255));
    diagnosticsLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(diagnosticsLabel);

    bpmLabel.setText("BPM", juce::dontSendNotification);
    addAndMakeVisible(bpmLabel);
    setupSlider(bpmSlider, 60.0, 180.0, 0.1, " BPM");
    bpmLockToggle.setTooltip("Lock BPM from auto style changes");
    bpmLockToggle.setClickingTogglesState(true);
    addAndMakeVisible(bpmLockToggle);

    addAndMakeVisible(syncTempoToggle);

    swingLabel.setText("Swing", juce::dontSendNotification);
    styleSecondaryLabel(swingLabel);
    addAndMakeVisible(swingLabel);
    setupKnob(swingSlider,
              swingValueLabel,
              "Swing",
              50.0,
              75.0,
              0.1,
              [] (double value) { return juce::String(value, 1) + "%"; },
              [] (const juce::String& text) { return parseKnobText(text); });

    velocityLabel.setText("Velocity", juce::dontSendNotification);
    styleSecondaryLabel(velocityLabel);
    addAndMakeVisible(velocityLabel);
    setupKnob(velocitySlider,
              velocityValueLabel,
              "Velocity",
              0.0,
              1.0,
              0.01,
              [] (double value) { return juce::String(value, 2); },
              [] (const juce::String& text) { return parseKnobText(text); });

    timingLabel.setText("Timing", juce::dontSendNotification);
    styleSecondaryLabel(timingLabel);
    addAndMakeVisible(timingLabel);
    setupKnob(timingSlider,
              timingValueLabel,
              "Timing",
              0.0,
              1.0,
              0.01,
              [] (double value) { return juce::String(value, 2); },
              [] (const juce::String& text) { return parseKnobText(text); });

    humanizeLabel.setText("Humanize", juce::dontSendNotification);
    styleSecondaryLabel(humanizeLabel);
    addAndMakeVisible(humanizeLabel);
    setupKnob(humanizeSlider,
              humanizeValueLabel,
              "Humanize",
              0.0,
              1.0,
              0.01,
              [] (double value) { return juce::String(value, 2); },
              [] (const juce::String& text) { return parseKnobText(text); });

    densityLabel.setText("Density", juce::dontSendNotification);
    styleSecondaryLabel(densityLabel);
    addAndMakeVisible(densityLabel);
    setupKnob(densitySlider,
              densityValueLabel,
              "Density",
              0.0,
              1.0,
              0.01,
              [] (double value) { return juce::String(value, 2); },
              [] (const juce::String& text) { return parseKnobText(text); });

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

    gridResolutionLabel.setText("Snap", juce::dontSendNotification);
    addAndMakeVisible(gridResolutionLabel);
    gridResolutionCombo.addItem("Adaptive", 1);
    gridResolutionCombo.addItem("Micro", 2);
    gridResolutionCombo.addSeparator();
    gridResolutionCombo.addItem("1/4", 10);
    gridResolutionCombo.addItem("1/4T", 11);
    gridResolutionCombo.addItem("1/8", 12);
    gridResolutionCombo.addItem("1/8T", 13);
    gridResolutionCombo.addItem("1/16", 14);
    gridResolutionCombo.addItem("1/16T", 15);
    gridResolutionCombo.addItem("1/32", 16);
    gridResolutionCombo.addItem("1/32T", 17);
    gridResolutionCombo.addItem("1/64", 18);
    gridResolutionCombo.addItem("1/64T", 19);
    gridResolutionCombo.setSelectedId(14, juce::dontSendNotification);
    gridModeIndicatorLabel.setText("Snap: 1/16", juce::dontSendNotification);
    gridModeIndicatorLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(170, 176, 186));
    addAndMakeVisible(gridModeIndicatorLabel);
    gridResolutionCombo.onChange = [this]
    {
        const auto selected = gridResolutionCombo.getText();
        gridModeIndicatorLabel.setText("Snap: " + selected, juce::dontSendNotification);
        if (onGridResolutionChanged)
            onGridResolutionChanged(gridResolutionCombo.getSelectedId());
    };
    addAndMakeVisible(gridResolutionCombo);

    previewPlaybackModeLabel.setText("Playback", juce::dontSendNotification);
    addAndMakeVisible(previewPlaybackModeLabel);
    previewPlaybackModeCombo.addItem("Play Flag", 1);
    previewPlaybackModeCombo.addItem("Loop Range", 2);
    previewPlaybackModeCombo.setSelectedId(1, juce::dontSendNotification);
    previewPlaybackModeCombo.onChange = [this]
    {
        if (onPreviewPlaybackModeChanged)
            onPreviewPlaybackModeChanged(previewPlaybackModeCombo.getSelectedId());
    };
    addAndMakeVisible(previewPlaybackModeCombo);

    hatFxDensityLabel.setText("Hat Accent", juce::dontSendNotification);
    styleSecondaryLabel(hatFxDensityLabel);
    addAndMakeVisible(hatFxDensityLabel);

    setupKnob(hatFxDensitySlider,
              hatFxDensityValueLabel,
              "Hat Accent",
              0.0,
              2.0,
              0.01,
              [] (double value) { return juce::String(value, 2); },
              [] (const juce::String& text) { return parseKnobText(text); });
    hatFxDensitySlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(110, 176, 248));
    hatFxDensitySlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(196, 222, 255));
    hatFxDensitySlider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(126, 162, 214));
    hatFxDensitySlider.setTooltip("Hat Accent density: 0 = no notes, 1 = original, >1 = more notes");

    hatFxDensityLockToggle.setTooltip("Lock Hat Accent density");
    addAndMakeVisible(hatFxDensityLockToggle);

    addAndMakeVisible(standaloneWindowButton);
    standaloneWindowButton.onClick = [this]
    {
        if (onToggleStandaloneWindow)
            onToggleStandaloneWindow();
    };
    standaloneWindowButton.setVisible(false);

    addAndMakeVisible(advancedModeButton);
    advancedModeButton.onClick = [this]
    {
        switch (controlsMode)
        {
            case HeaderControlsMode::Expanded: setHeaderControlsMode(HeaderControlsMode::Compact); break;
            case HeaderControlsMode::Compact: setHeaderControlsMode(HeaderControlsMode::Hidden); break;
            case HeaderControlsMode::Hidden: setHeaderControlsMode(HeaderControlsMode::Expanded); break;
            default: setHeaderControlsMode(HeaderControlsMode::Expanded); break;
        }

        if (onHeaderControlsModeChanged)
            onHeaderControlsModeChanged(controlsMode);
    };

    masterSectionLabel.setText("OUTPUT", juce::dontSendNotification);
    masterSectionLabel.setJustificationType(juce::Justification::centredLeft);
    masterSectionLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(200, 206, 214));
    addAndMakeVisible(masterSectionLabel);

    masterVolumeLabel.setText("Vol", juce::dontSendNotification);
    addAndMakeVisible(masterVolumeLabel);
    setupSlider(masterVolumeSlider, 0.0, 1.5, 0.01);

    zoomLabel.setText("Zoom", juce::dontSendNotification);
    addAndMakeVisible(zoomLabel);
    setupSlider(zoomSlider, 0.25, 8.0, 0.05);
    zoomSlider.setValue(1.0, juce::dontSendNotification);

    laneHeightLabel.setText("Lane H", juce::dontSendNotification);
    addAndMakeVisible(laneHeightLabel);
    setupSlider(laneHeightSlider, 22.0, 64.0, 1.0);
    laneHeightSlider.setValue(30.0, juce::dontSendNotification);

    addAndMakeVisible(seedLockToggle);
    addAndMakeVisible(playButton);
    addAndMakeVisible(transportToStartButton);
    addAndMakeVisible(transportStepBackButton);
    addAndMakeVisible(transportStepForwardButton);
    addAndMakeVisible(transportToEndButton);
    addAndMakeVisible(mutateButton);
    addAndMakeVisible(clearAllButton);
    addAndMakeVisible(exportFullButton);
    addAndMakeVisible(exportLoopWavButton);
    addAndMakeVisible(dragFullButton);
    addAndMakeVisible(generateButton);
    addAndMakeVisible(startPlayWithDawToggle);
    startPlayWithDawToggle.setClickingTogglesState(true);
    startPlayWithDawToggle.onClick = [this]
    {
        if (onStartPlayWithDawToggled)
            onStartPlayWithDawToggled(startPlayWithDawToggle.getToggleState());
    };

    generateButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(232, 153, 66));
    generateButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(18, 19, 22));
    playButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(74, 122, 186));
    transportToStartButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(54, 59, 70));
    transportStepBackButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(54, 59, 70));
    transportStepForwardButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(54, 59, 70));
    transportToEndButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(54, 59, 70));
    mutateButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(156, 124, 58));
    clearAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(122, 68, 68));
    exportFullButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(54, 59, 70));
    exportLoopWavButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(58, 86, 104));
    dragFullButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(54, 59, 70));

    transportStepBackButton.setRepeatSpeed(300, 75, 50);
    transportStepForwardButton.setRepeatSpeed(300, 75, 50);

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

    exportLoopWavButton.onClick = [this]
    {
        if (onExportLoopWavPressed)
            onExportLoopWavPressed();
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

    transportToStartButton.onClick = [this]
    {
        if (onTransportToStart)
            onTransportToStart();
    };

    transportStepBackButton.onClick = [this]
    {
        if (onTransportStepBack)
            onTransportStepBack();
    };

    transportStepForwardButton.onClick = [this]
    {
        if (onTransportStepForward)
            onTransportStepForward();
    };

    transportToEndButton.onClick = [this]
    {
        if (onTransportToEnd)
            onTransportToEnd();
    };

    mutateButton.onClick = [this]
    {
        if (onMutatePressed)
            onMutatePressed();
    };

    clearAllButton.onClick = [this]
    {
        if (onClearAllPressed)
            onClearAllPressed();
    };

    const auto zoomChanged = [this]
    {
        if (onZoomChanged)
            onZoomChanged(static_cast<float>(zoomSlider.getValue()), static_cast<float>(laneHeightSlider.getValue()));
    };
    zoomSlider.onValueChange = zoomChanged;
    laneHeightSlider.onValueChange = zoomChanged;

    updateAdvancedModeButtonText();
}

void MainHeaderComponent::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    if (vst3GeneratorChromeEnabled)
    {
        juce::ColourGradient fill(juce::Colour::fromRGB(26, 21, 18), area.getTopLeft(),
                                  juce::Colour::fromRGB(14, 15, 17), area.getBottomLeft(), false);
        fill.addColour(0.28, juce::Colour::fromRGB(42, 31, 23));
        fill.addColour(0.70, juce::Colour::fromRGB(18, 18, 20));
        g.setGradientFill(fill);
    }
    else
    {
        g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGB(30, 33, 38), area.getTopLeft(),
                                               juce::Colour::fromRGB(20, 22, 26), area.getBottomLeft(), false));
    }
    g.fillRoundedRectangle(area.reduced(0.5f), 10.0f);

    if (vst3GeneratorChromeEnabled)
        drawHeaderTexture(g, area.reduced(6.0f), 0.82f, 0x48454144);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 20));
    g.drawRoundedRectangle(area.reduced(0.5f), 10.0f, 1.0f);

    g.setColour(vst3GeneratorChromeEnabled ? juce::Colour::fromRGBA(246, 191, 112, 70)
                                           : juce::Colour::fromRGBA(232, 153, 66, 40));
    g.fillRect(0, 0, getWidth(), 2);
}

void MainHeaderComponent::setGridModeIndicatorText(const juce::String& text)
{
    gridModeIndicatorLabel.setText(text, juce::dontSendNotification);
}

void MainHeaderComponent::setStyleLabDiagnosticsText(const juce::String& text)
{
    diagnosticsLabel.setText(text, juce::dontSendNotification);
    diagnosticsLabel.setVisible(text.isNotEmpty() && controlsMode != HeaderControlsMode::Hidden);
}

void MainHeaderComponent::setPreviewPlaybackModeId(int id)
{
    previewPlaybackModeCombo.setSelectedId(id, juce::dontSendNotification);
}

void MainHeaderComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    const int fixedRowHeight = 42;
    const int advancedRowHeight = controlsMode == HeaderControlsMode::Hidden
        ? 0
        : (controlsMode == HeaderControlsMode::Compact ? 52 : 64);

    auto fixedRow = area.removeFromTop(fixedRowHeight);
    if (advancedRowHeight > 0)
        area.removeFromTop(4);
    auto advancedRow = area.removeFromTop(advancedRowHeight);

    auto masterArea = fixedRow.removeFromRight(vst3GeneratorChromeEnabled ? 236 : 320);
    fixedRow.removeFromRight(6);

    auto titleArea = fixedRow.removeFromLeft(vst3GeneratorChromeEnabled ? 262 : 380).reduced(2);
    titleLabel.setBounds(titleArea.removeFromTop(22));
    subtitleLabel.setBounds(titleArea.removeFromTop(16));
    diagnosticsLabel.setBounds(titleArea.removeFromTop(14));

    auto bpmArea = fixedRow.removeFromLeft(vst3GeneratorChromeEnabled ? 224 : 196);
    bpmLabel.setBounds(bpmArea.removeFromTop(14));
    auto bpmControlRow = bpmArea.removeFromTop(24);

    if (vst3GeneratorChromeEnabled)
    {
        bpmLockToggle.setBounds(bpmControlRow.removeFromLeft(44).reduced(2));
        bpmControlRow.removeFromLeft(4);
        bpmSlider.setBounds(bpmControlRow.removeFromLeft(106));
        bpmControlRow.removeFromLeft(6);
        syncTempoToggle.setBounds(bpmControlRow.removeFromLeft(64).reduced(1));
    }
    else
    {
        bpmLockToggle.setBounds(bpmControlRow.removeFromLeft(50).reduced(2));
        bpmSlider.setBounds(bpmControlRow.removeFromLeft(102));
        syncTempoToggle.setBounds(bpmControlRow.reduced(2));
    }

    fixedRow.removeFromLeft(4);
    generateButton.setBounds(fixedRow.removeFromLeft(124).reduced(2));
    mutateButton.setBounds(fixedRow.removeFromLeft(vst3GeneratorChromeEnabled ? 76 : 84).reduced(2));
    clearAllButton.setBounds(fixedRow.removeFromLeft(vst3GeneratorChromeEnabled ? 78 : 82).reduced(2));
    playButton.setBounds(fixedRow.removeFromLeft(vst3GeneratorChromeEnabled ? 72 : 88).reduced(2));
    exportFullButton.setBounds(fixedRow.removeFromLeft(vst3GeneratorChromeEnabled ? 88 : 92).reduced(2));

    if (vst3GeneratorChromeEnabled)
    {
        exportLoopWavButton.setBounds({});
        dragFullButton.setBounds(fixedRow.removeFromLeft(76).reduced(2));
        transportToStartButton.setBounds({});
        transportStepBackButton.setBounds({});
        transportStepForwardButton.setBounds({});
        transportToEndButton.setBounds({});
    }
    else
    {
        exportLoopWavButton.setBounds(fixedRow.removeFromLeft(118).reduced(2));
        dragFullButton.setBounds(fixedRow.removeFromLeft(82).reduced(2));
        transportToStartButton.setBounds(fixedRow.removeFromLeft(40).reduced(2));
        transportStepBackButton.setBounds(fixedRow.removeFromLeft(42).reduced(2));
        transportStepForwardButton.setBounds(fixedRow.removeFromLeft(42).reduced(2));
        transportToEndButton.setBounds(fixedRow.removeFromLeft(40).reduced(2));
    }

    auto masterTop = masterArea.removeFromTop(16);
    juce::Rectangle<int> masterRow;

    if (vst3GeneratorChromeEnabled)
    {
        masterSectionLabel.setBounds({});
        masterVolumeLabel.setBounds({});
        masterVolumeSlider.setBounds({});
        standaloneWindowButton.setBounds({});
        startPlayWithDawToggle.setBounds(masterTop.removeFromRight(128).reduced(1));
        masterTop.removeFromRight(6);
        advancedModeButton.setBounds(masterTop.removeFromRight(98).reduced(1));
    }
    else
    {
        masterSectionLabel.setBounds(masterTop.removeFromLeft(70));
        startPlayWithDawToggle.setBounds(masterTop.removeFromRight(152).reduced(1));
        advancedModeButton.setBounds(masterTop.removeFromRight(96).reduced(1));
        standaloneWindowButton.setBounds(masterTop.removeFromRight(56).reduced(1));

        masterArea.removeFromTop(2);
        masterRow = masterArea.removeFromTop(24);
    }

    const auto placeMaster = [](juce::Rectangle<int>& row, int width, juce::Label& label, juce::Slider& slider)
    {
        auto slot = row.removeFromLeft(width);
        label.setBounds(slot.removeFromTop(10));
        slider.setBounds(slot.removeFromTop(14));
    };

    const auto placeField = [](juce::Rectangle<int>& row,
                               int width,
                               juce::Label& label,
                               juce::Component& comp,
                               int labelHeight = 12,
                               int controlHeight = 18)
    {
        auto slot = row.removeFromLeft(width).reduced(2, 0);
        label.setBounds(slot.removeFromTop(labelHeight));
        comp.setBounds(slot.removeFromTop(controlHeight));
    };

    const auto placeKnob = [](juce::Rectangle<int>& row,
                              int width,
                              juce::Label& label,
                              juce::Component& knob,
                              juce::Label& valueLabel,
                              int knobSize,
                              int labelHeight,
                              int valueHeight)
    {
        auto slot = row.removeFromLeft(width).reduced(2, 0);
        label.setBounds(slot.removeFromTop(labelHeight));
        valueLabel.setBounds(slot.removeFromBottom(valueHeight));

        auto knobArea = slot.reduced(0, 1);
        const int diameter = juce::jmin(knobSize, juce::jmin(knobArea.getWidth(), knobArea.getHeight()));
        knob.setBounds(knobArea.withSizeKeepingCentre(diameter, diameter));
    };

    const auto placeToggle = [](juce::Rectangle<int>& row, int width, juce::Component& comp, int height)
    {
        auto slot = row.removeFromLeft(width).reduced(2, 0);
        const int top = slot.getY() + juce::jmax(0, (slot.getHeight() - height) / 2);
        comp.setBounds(slot.getX(), top, slot.getWidth(), height);
    };

    if (!vst3GeneratorChromeEnabled)
        placeMaster(masterRow, 120, masterVolumeLabel, masterVolumeSlider);

    if (controlsMode == HeaderControlsMode::Hidden)
    {
        placeField(masterRow, 88, gridResolutionLabel, gridResolutionCombo, 10, 14);
        gridModeIndicatorLabel.setBounds(masterRow.removeFromLeft(104).reduced(2, 4));
        return;
    }

    if (controlsMode == HeaderControlsMode::Compact)
    {
        placeField(advancedRow, 94, genreLabel, genreCombo);
        placeField(advancedRow, 112, substyleLabel, substyleCombo);
        placeField(advancedRow, 74, barsLabel, barsCombo);
        placeField(advancedRow, 114, tempoInterpretationLabel, tempoInterpretationCombo);
        placeKnob(advancedRow, 76, swingLabel, swingSlider, swingValueLabel, 24, 12, 12);
        placeKnob(advancedRow, 76, densityLabel, densitySlider, densityValueLabel, 24, 12, 12);
        placeField(advancedRow, 92, gridResolutionLabel, gridResolutionCombo);
        placeField(advancedRow, 118, previewPlaybackModeLabel, previewPlaybackModeCombo);
        placeField(advancedRow, 118, seedLabel, seedSlider);
        gridModeIndicatorLabel.setBounds(advancedRow.removeFromLeft(96).reduced(2, 14));
        return;
    }

    placeKnob(advancedRow, 80, swingLabel, swingSlider, swingValueLabel, 32, 12, 12);
    placeKnob(advancedRow, 80, velocityLabel, velocitySlider, velocityValueLabel, 32, 12, 12);
    placeKnob(advancedRow, 80, timingLabel, timingSlider, timingValueLabel, 32, 12, 12);
    placeKnob(advancedRow, 80, humanizeLabel, humanizeSlider, humanizeValueLabel, 32, 12, 12);
    placeKnob(advancedRow, 80, densityLabel, densitySlider, densityValueLabel, 32, 12, 12);
    placeField(advancedRow, 118, tempoInterpretationLabel, tempoInterpretationCombo);
    placeField(advancedRow, 74, barsLabel, barsCombo);
    placeField(advancedRow, 98, genreLabel, genreCombo);
    placeField(advancedRow, 114, substyleLabel, substyleCombo);
    placeField(advancedRow, 118, seedLabel, seedSlider);
    placeField(advancedRow, 92, gridResolutionLabel, gridResolutionCombo);
    placeToggle(advancedRow, 84, seedLockToggle, 24);
    placeField(advancedRow, 108, zoomLabel, zoomSlider);
    placeField(advancedRow, 110, laneHeightLabel, laneHeightSlider);
    gridModeIndicatorLabel.setBounds(advancedRow.removeFromLeft(92).reduced(2, 20));
    placeField(advancedRow, 118, previewPlaybackModeLabel, previewPlaybackModeCombo);
    placeKnob(advancedRow, 82, hatFxDensityLabel, hatFxDensitySlider, hatFxDensityValueLabel, 32, 12, 12);
    placeToggle(advancedRow, 42, hatFxDensityLockToggle, 24);
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

void MainHeaderComponent::setStartPlayWithDawEnabled(bool enabled)
{
    startPlayWithDawToggle.setToggleState(enabled, juce::dontSendNotification);
}

void MainHeaderComponent::setHeaderControlsMode(HeaderControlsMode mode)
{
    if (controlsMode == mode)
        return;

    controlsMode = mode;

    const bool expanded = controlsMode == HeaderControlsMode::Expanded;
    const bool hidden = controlsMode == HeaderControlsMode::Hidden;
    const bool advancedVisible = !hidden;

    swingLabel.setVisible(advancedVisible);
    swingSlider.setVisible(advancedVisible);
    swingValueLabel.setVisible(advancedVisible);
    bpmLockToggle.setVisible(true);
    velocityLabel.setVisible(expanded);
    velocitySlider.setVisible(expanded);
    velocityValueLabel.setVisible(expanded);
    timingLabel.setVisible(expanded);
    timingSlider.setVisible(expanded);
    timingValueLabel.setVisible(expanded);
    humanizeLabel.setVisible(expanded);
    humanizeSlider.setVisible(expanded);
    humanizeValueLabel.setVisible(expanded);
    densityLabel.setVisible(advancedVisible);
    densitySlider.setVisible(advancedVisible);
    densityValueLabel.setVisible(advancedVisible);
    tempoInterpretationLabel.setVisible(advancedVisible);
    tempoInterpretationCombo.setVisible(advancedVisible);
    barsLabel.setVisible(advancedVisible);
    barsCombo.setVisible(advancedVisible);
    genreLabel.setVisible(advancedVisible);
    genreCombo.setVisible(advancedVisible);
    substyleLabel.setVisible(advancedVisible);
    substyleCombo.setVisible(advancedVisible);
    seedLabel.setVisible(advancedVisible);
    seedSlider.setVisible(advancedVisible);
    gridResolutionLabel.setVisible(advancedVisible || hidden);
    gridResolutionCombo.setVisible(advancedVisible || hidden);
    previewPlaybackModeLabel.setVisible(advancedVisible);
    previewPlaybackModeCombo.setVisible(advancedVisible);
    seedLockToggle.setVisible(expanded);
    zoomLabel.setVisible(expanded);
    zoomSlider.setVisible(expanded);
    laneHeightLabel.setVisible(expanded);
    laneHeightSlider.setVisible(expanded);
    gridModeIndicatorLabel.setVisible(expanded || controlsMode == HeaderControlsMode::Compact || hidden);
    diagnosticsLabel.setVisible(!hidden && diagnosticsLabel.getText().isNotEmpty());
    hatFxDensityLabel.setVisible(expanded);
    hatFxDensitySlider.setVisible(expanded);
    hatFxDensityValueLabel.setVisible(expanded);
    hatFxDensityLockToggle.setVisible(expanded);

    updateAdvancedModeButtonText();
    resized();
    repaint();
}

int MainHeaderComponent::getPreferredHeight() const
{
    if (controlsMode == HeaderControlsMode::Hidden)
        return 58;
    if (controlsMode == HeaderControlsMode::Compact)
        return vst3GeneratorChromeEnabled ? 106 : 114;
    return 128;
}

void MainHeaderComponent::setVst3GeneratorChromeEnabled(bool enabled)
{
    if (vst3GeneratorChromeEnabled == enabled)
        return;

    vst3GeneratorChromeEnabled = enabled;

    transportToStartButton.setVisible(!enabled);
    transportStepBackButton.setVisible(!enabled);
    transportStepForwardButton.setVisible(!enabled);
    transportToEndButton.setVisible(!enabled);
    exportLoopWavButton.setVisible(!enabled);

    if (enabled)
    {
        const auto styleHeaderButton = [](juce::TextButton& button,
                                          juce::Colour fill,
                                          juce::Colour down,
                                          juce::Colour text)
        {
            button.setColour(juce::TextButton::buttonColourId, fill);
            button.setColour(juce::TextButton::buttonOnColourId, down);
            button.setColour(juce::TextButton::textColourOffId, text);
            button.setColour(juce::TextButton::textColourOnId, text);
        };

        const auto styleToggle = [](juce::ToggleButton& button, juce::Colour text, juce::Colour tick)
        {
            button.setColour(juce::ToggleButton::textColourId, text);
            button.setColour(juce::ToggleButton::tickColourId, tick);
            button.setColour(juce::ToggleButton::tickDisabledColourId, tick.withAlpha(0.35f));
        };

        const auto styleFieldSlider = [](juce::Slider& slider, juce::Colour accent)
        {
            slider.setColour(juce::Slider::trackColourId, accent);
            slider.setColour(juce::Slider::thumbColourId, accent.brighter(0.35f));
            slider.setColour(juce::Slider::backgroundColourId, juce::Colour::fromRGBA(255, 255, 255, 18));
            slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(236, 224, 208));
            slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGB(32, 28, 26));
            slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour::fromRGB(104, 76, 44));
        };

        const auto styleCombo = [](juce::ComboBox& combo)
        {
            combo.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGB(33, 29, 27));
            combo.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(118, 88, 54));
            combo.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(238, 224, 208));
            combo.setColour(juce::ComboBox::arrowColourId, juce::Colour::fromRGB(232, 187, 118));
        };

        syncTempoToggle.setButtonText("DAW BPM");
        startPlayWithDawToggle.setButtonText("DAW Start");
        masterSectionLabel.setVisible(false);
        masterVolumeLabel.setVisible(false);
        masterVolumeSlider.setVisible(false);

        styleHeaderButton(generateButton,
                          juce::Colour::fromRGB(232, 167, 78),
                          juce::Colour::fromRGB(204, 138, 58),
                          juce::Colour::fromRGB(20, 18, 16));
        mutateButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(169, 136, 73));
        clearAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(126, 74, 64));
        playButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(83, 122, 176));
        styleHeaderButton(mutateButton,
                          juce::Colour::fromRGB(166, 132, 70),
                          juce::Colour::fromRGB(138, 108, 56),
                          juce::Colour::fromRGB(252, 245, 232));
        styleHeaderButton(clearAllButton,
                          juce::Colour::fromRGB(126, 74, 64),
                          juce::Colour::fromRGB(102, 58, 50),
                          juce::Colour::fromRGB(252, 245, 232));
        styleHeaderButton(playButton,
                          juce::Colour::fromRGB(83, 122, 176),
                          juce::Colour::fromRGB(68, 103, 156),
                          juce::Colour::fromRGB(252, 245, 232));
        styleHeaderButton(exportFullButton,
                          juce::Colour::fromRGB(56, 48, 41),
                          juce::Colour::fromRGB(42, 37, 33),
                          juce::Colour::fromRGB(234, 220, 205));
        styleHeaderButton(dragFullButton,
                          juce::Colour::fromRGB(66, 54, 43),
                          juce::Colour::fromRGB(48, 40, 33),
                          juce::Colour::fromRGB(242, 228, 208));
        styleHeaderButton(advancedModeButton,
                          juce::Colour::fromRGB(62, 53, 47),
                          juce::Colour::fromRGB(48, 41, 37),
                          juce::Colour::fromRGB(232, 216, 198));

        styleToggle(bpmLockToggle, juce::Colour::fromRGB(214, 198, 180), juce::Colour::fromRGB(230, 178, 96));
        styleToggle(syncTempoToggle, juce::Colour::fromRGB(232, 214, 194), juce::Colour::fromRGB(102, 176, 232));
        styleToggle(seedLockToggle, juce::Colour::fromRGB(214, 198, 180), juce::Colour::fromRGB(230, 178, 96));
        styleToggle(startPlayWithDawToggle, juce::Colour::fromRGB(232, 214, 194), juce::Colour::fromRGB(230, 178, 96));

        styleFieldSlider(bpmSlider, juce::Colour::fromRGB(86, 170, 214));
        styleFieldSlider(seedSlider, juce::Colour::fromRGB(86, 170, 214));
        styleFieldSlider(zoomSlider, juce::Colour::fromRGB(86, 170, 214));
        styleFieldSlider(laneHeightSlider, juce::Colour::fromRGB(86, 170, 214));

        styleCombo(tempoInterpretationCombo);
        styleCombo(barsCombo);
        styleCombo(genreCombo);
        styleCombo(substyleCombo);
        styleCombo(gridResolutionCombo);
        styleCombo(previewPlaybackModeCombo);
    }
    else
    {
        syncTempoToggle.setButtonText("Sync");
        startPlayWithDawToggle.setButtonText("Start play with DAW");
        masterSectionLabel.setVisible(true);
        masterVolumeLabel.setVisible(true);
        masterVolumeSlider.setVisible(true);
    }

    resized();
    repaint();
}

void MainHeaderComponent::setHatFxDensityState(float density, bool locked)
{
    const float clamped = juce::jlimit(0.0f, 2.0f, density);
    hatFxDensitySlider.setValue(clamped, juce::dontSendNotification);
    hatFxDensityValueLabel.setText(hatFxDensitySlider.getTextFromValue(clamped), juce::dontSendNotification);
    hatFxDensityLockToggle.setToggleState(locked, juce::dontSendNotification);
    hatFxDensitySlider.setEnabled(!locked);
}

void MainHeaderComponent::updateAdvancedModeButtonText()
{
    switch (controlsMode)
    {
        case HeaderControlsMode::Expanded: advancedModeButton.setButtonText("ADV:FULL"); break;
        case HeaderControlsMode::Compact: advancedModeButton.setButtonText("ADV:COMPACT"); break;
        case HeaderControlsMode::Hidden: advancedModeButton.setButtonText("ADV:HIDDEN"); break;
        default: advancedModeButton.setButtonText("ADV"); break;
    }
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

void MainHeaderComponent::setupKnob(RotaryKnobSlider& slider,
                                    juce::Label& valueLabel,
                                    const juce::String& popupTitle,
                                    double min,
                                    double max,
                                    double step,
                                    std::function<juce::String(double)> formatter,
                                    std::function<double(const juce::String&)> parser)
{
    valueLabel.setJustificationType(juce::Justification::centred);
    valueLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    valueLabel.setColour(juce::Label::textColourId, hardware_knob::amberBright());
    addAndMakeVisible(valueLabel);

    slider.setPopupTitle(popupTitle);
    slider.setRange(min, max, step);

    if (formatter)
        slider.textFromValueFunction = std::move(formatter);

    if (parser)
        slider.valueFromTextFunction = std::move(parser);

    slider.addListener(this);
    addAndMakeVisible(slider);
    valueLabel.setText(slider.getTextFromValue(slider.getValue()), juce::dontSendNotification);
}

void MainHeaderComponent::sliderValueChanged(juce::Slider* slider)
{
    const auto syncLabel = [] (juce::Slider& source, juce::Label& target)
    {
        target.setText(source.getTextFromValue(source.getValue()), juce::dontSendNotification);
    };

    if (slider == &swingSlider)
        syncLabel(swingSlider, swingValueLabel);
    else if (slider == &velocitySlider)
        syncLabel(velocitySlider, velocityValueLabel);
    else if (slider == &timingSlider)
        syncLabel(timingSlider, timingValueLabel);
    else if (slider == &humanizeSlider)
        syncLabel(humanizeSlider, humanizeValueLabel);
    else if (slider == &densitySlider)
        syncLabel(densitySlider, densityValueLabel);
    else if (slider == &hatFxDensitySlider)
        syncLabel(hatFxDensitySlider, hatFxDensityValueLabel);
}
} // namespace bbg
