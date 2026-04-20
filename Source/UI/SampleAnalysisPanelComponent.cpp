#include "SampleAnalysisPanelComponent.h"

#include <algorithm>

namespace bbg
{
namespace
{
bool isSupportedAnalysisFile(const juce::String& path)
{
    const auto ext = juce::File(path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3";
}
}

SampleAnalysisPanelComponent::SampleAnalysisPanelComponent()
{
    setupUi();
}

void SampleAnalysisPanelComponent::paint(juce::Graphics& g)
{
    auto panel = getLocalBounds().toFloat().reduced(1.0f);
    juce::ColourGradient fill(juce::Colour::fromRGB(21, 19, 18), panel.getTopLeft(),
                              juce::Colour::fromRGB(11, 12, 14), panel.getBottomLeft(), false);
    fill.addColour(0.30, juce::Colour::fromRGB(30, 24, 21));
    fill.addColour(0.72, juce::Colour::fromRGB(16, 17, 20));
    g.setGradientFill(fill);
    g.fillRoundedRectangle(panel, 8.0f);

    auto header = panel.reduced(1.0f).removeFromTop(22.0f);
    g.setColour(juce::Colour::fromRGBA(229, 178, 102, 24));
    g.fillRoundedRectangle(header, 6.0f);
    g.setColour(juce::Colour::fromRGBA(255, 210, 142, 34));
    g.fillRoundedRectangle(header.removeFromTop(2.5f), 2.5f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
    g.drawRoundedRectangle(panel, 8.0f, 1.0f);
    g.setColour(juce::Colour::fromRGBA(229, 178, 102, 76));
    g.drawRoundedRectangle(panel.reduced(0.5f), 8.0f, 1.0f);

    if (analysisFileDropHighlight)
    {
        g.setColour(juce::Colour::fromRGBA(130, 188, 255, 58));
        g.fillRoundedRectangle(chooseFileButton.getBounds().toFloat().reduced(-2.0f), 6.0f);
        g.setColour(juce::Colour::fromRGBA(178, 218, 255, 190));
        g.drawRoundedRectangle(chooseFileButton.getBounds().toFloat().reduced(-2.0f), 6.0f, 1.5f);
    }
}

void SampleAnalysisPanelComponent::resized()
{
    auto panel = getLocalBounds().reduced(8, 8);
    if (panel.isEmpty())
        return;

    analysisTitleLabel.setBounds(panel.removeFromTop(22));
    panel.removeFromTop(2);

    auto line1 = panel.removeFromTop(26);
    sourceLabel.setBounds(line1.removeFromLeft(52));
    sourceCombo.setBounds(line1.removeFromLeft(118));
    line1.removeFromLeft(6);
    modeLabel.setBounds(line1.removeFromLeft(42));
    modeCombo.setBounds(line1.removeFromLeft(144));
    line1.removeFromLeft(6);
    applyModeLabel.setBounds(line1.removeFromLeft(42));
    applyModeCombo.setBounds(line1);

    panel.removeFromTop(6);
    auto line2 = panel.removeFromTop(26);
    barsLabel.setBounds(line2.removeFromLeft(52));
    barsCombo.setBounds(line2.removeFromLeft(72));
    line2.removeFromLeft(6);
    tempoLabel.setBounds(line2.removeFromLeft(40));
    tempoCombo.setBounds(line2.removeFromLeft(100));
    line2.removeFromLeft(8);
    chooseFileButton.setBounds(line2.removeFromLeft(90));
    line2.removeFromLeft(6);
    analyzeButton.setBounds(line2.removeFromLeft(80));

    panel.removeFromTop(6);
    fileLabel.setBounds(panel.removeFromTop(20));

    panel.removeFromTop(4);
    auto line3 = panel.removeFromTop(24);
    reactivityLabel.setBounds(line3.removeFromLeft(78));
    reactivitySlider.setBounds(line3);

    panel.removeFromTop(4);
    auto line4 = panel.removeFromTop(24);
    supportLabel.setBounds(line4.removeFromLeft(78));
    supportSlider.setBounds(line4);

    panel.removeFromTop(6);
    statusLabel.setBounds(panel.removeFromTop(36));

    panel.removeFromTop(4);
    detailsLabel.setBounds(panel.removeFromTop(48));

    panel.removeFromTop(6);
    debugLabel.setBounds(panel.removeFromTop(18));

    panel.removeFromTop(2);
    debugTextBox.setBounds(panel);
}

bool SampleAnalysisPanelComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const juce::String& path)
    {
        return isSupportedAnalysisFile(path);
    });
}

void SampleAnalysisPanelComponent::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    fileDragMove(files, x, y);
}

void SampleAnalysisPanelComponent::fileDragMove(const juce::StringArray& files, int x, int y)
{
    const bool shouldHighlight = chooseFileButton.getBounds().contains(x, y)
        && std::any_of(files.begin(), files.end(), [](const juce::String& path)
        {
            return isSupportedAnalysisFile(path);
        });

    if (analysisFileDropHighlight == shouldHighlight)
        return;

    analysisFileDropHighlight = shouldHighlight;
    repaint();
}

void SampleAnalysisPanelComponent::fileDragExit(const juce::StringArray& files)
{
    juce::ignoreUnused(files);
    if (!analysisFileDropHighlight)
        return;

    analysisFileDropHighlight = false;
    repaint();
}

void SampleAnalysisPanelComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    const bool droppedOnButton = chooseFileButton.getBounds().contains(x, y);
    analysisFileDropHighlight = false;
    repaint();

    if (!droppedOnButton)
        return;

    auto it = std::find_if(files.begin(), files.end(), [](const juce::String& path)
    {
        return isSupportedAnalysisFile(path);
    });

    if (it == files.end())
        return;

    if (onAnalysisFileDropped)
        onAnalysisFileDropped(juce::File(*it));
}

void SampleAnalysisPanelComponent::setPanelState(SampleAnalysisRequest::SourceType source,
                                                 AnalysisMode mode,
                                                 SampleApplyMode applyMode,
                                                 int barsToCapture,
                                                 SampleAnalysisRequest::TempoHandling tempoHandling,
                                                 float reactivity,
                                                 float supportVsContrast,
                                                 const juce::String& fileLabelText,
                                                 const juce::String& statusText,
                                                 const juce::String& detailsText,
                                                 const juce::String& debugText,
                                                 bool analysisReady)
{
    const int sourceId = source == SampleAnalysisRequest::SourceType::AudioFile ? 3
        : (source == SampleAnalysisRequest::SourceType::LiveInput ? 2 : 1);
    sourceCombo.setSelectedId(sourceId, juce::dontSendNotification);

    const int modeId = mode == AnalysisMode::AnalyzeOnly ? 2
        : (mode == AnalysisMode::GenerateFromSample ? 3
            : (mode == AnalysisMode::ExtractFromSample ? 4 : 1));
    modeCombo.setSelectedId(modeId, juce::dontSendNotification);

    const int applyModeId = applyMode == SampleApplyMode::GenreFirst ? 1
        : (applyMode == SampleApplyMode::SampleFirst ? 3
            : (applyMode == SampleApplyMode::ExactCopy ? 4 : 2));
    applyModeCombo.setSelectedId(applyModeId, juce::dontSendNotification);

    const int barsId = barsToCapture <= 2 ? 1
        : (barsToCapture <= 4 ? 2 : (barsToCapture <= 8 ? 3 : 4));
    barsCombo.setSelectedId(barsId, juce::dontSendNotification);

    const int tempoId = tempoHandling == SampleAnalysisRequest::TempoHandling::PreferHalfTime ? 2
        : (tempoHandling == SampleAnalysisRequest::TempoHandling::PreferDoubleTime ? 3
            : (tempoHandling == SampleAnalysisRequest::TempoHandling::KeepDetected ? 4 : 1));
    tempoCombo.setSelectedId(tempoId, juce::dontSendNotification);

    reactivitySlider.setValue(juce::jlimit(0.0, 1.0, static_cast<double>(reactivity)), juce::dontSendNotification);
    supportSlider.setValue(juce::jlimit(0.0, 1.0, static_cast<double>(supportVsContrast)), juce::dontSendNotification);

    fileLabel.setText(fileLabelText.isNotEmpty() ? fileLabelText : "File: (none)", juce::dontSendNotification);

    const juce::String prefix = analysisReady ? "Result: ready" : "Result: none";
    statusLabel.setText(statusText.isNotEmpty() ? (prefix + " | " + statusText) : prefix,
                        juce::dontSendNotification);
    detailsLabel.setText(detailsText.isNotEmpty() ? detailsText : "Details: -",
                         juce::dontSendNotification);
    debugTextBox.setText(debugText.isNotEmpty() ? debugText : "No generator debug details yet.",
                         juce::dontSendNotification);
}

void SampleAnalysisPanelComponent::setupUi()
{
    analysisTitleLabel.setText("SAMPLE ANALYSIS", juce::dontSendNotification);
    analysisTitleLabel.setFont(juce::Font(12.5f, juce::Font::bold));
    analysisTitleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(239, 225, 209));
    analysisTitleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(analysisTitleLabel);

    sourceLabel.setText("Source", juce::dontSendNotification);
    modeLabel.setText("Mode", juce::dontSendNotification);
    applyModeLabel.setText("Apply", juce::dontSendNotification);
    barsLabel.setText("Scan", juce::dontSendNotification);
    tempoLabel.setText("BPM", juce::dontSendNotification);
    reactivityLabel.setText("React", juce::dontSendNotification);
    supportLabel.setText("Intent", juce::dontSendNotification);
    fileLabel.setText("File: (none)", juce::dontSendNotification);
    statusLabel.setText("Result: none", juce::dontSendNotification);
    detailsLabel.setText("Details: -", juce::dontSendNotification);
    debugLabel.setText("Generator Debug", juce::dontSendNotification);

    auto styleLabel = [](juce::Label& label)
    {
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(186, 168, 146));
        label.setFont(juce::Font(11.0f));
    };

    styleLabel(sourceLabel);
    styleLabel(modeLabel);
    styleLabel(applyModeLabel);
    styleLabel(barsLabel);
    styleLabel(tempoLabel);
    styleLabel(reactivityLabel);
    styleLabel(supportLabel);
    styleLabel(fileLabel);
    styleLabel(statusLabel);
    styleLabel(detailsLabel);
    styleLabel(debugLabel);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(248, 207, 132));
    detailsLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(201, 189, 174));
    debugLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 198, 180));

    debugTextBox.setMultiLine(true);
    debugTextBox.setReadOnly(true);
    debugTextBox.setCaretVisible(false);
    debugTextBox.setScrollbarsShown(true);
    debugTextBox.setPopupMenuEnabled(false);
    debugTextBox.setText("No generator debug details yet.", juce::dontSendNotification);
    debugTextBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(18, 16, 15));
    debugTextBox.setColour(juce::TextEditor::textColourId, juce::Colour::fromRGB(223, 206, 182));
    debugTextBox.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(96, 71, 46));
    debugTextBox.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour::fromRGB(188, 132, 72));

    sourceCombo.addItem("None", 1);
    sourceCombo.addItem("Live Input", 2);
    sourceCombo.addItem("Audio File", 3);
    sourceCombo.setSelectedId(1);

    modeCombo.addItem("Off", 1);
    modeCombo.addItem("Analyze Only", 2);
    modeCombo.addItem("Generate From Sample", 3);
    modeCombo.addItem("Extract / Copy", 4);
    modeCombo.setSelectedId(1);

    applyModeCombo.addItem("Genre First", 1);
    applyModeCombo.addItem("Blend", 2);
    applyModeCombo.addItem("Sample First", 3);
    applyModeCombo.addItem("Exact Copy", 4);
    applyModeCombo.setSelectedId(2);

    barsCombo.addItem("2 bars", 1);
    barsCombo.addItem("4 bars", 2);
    barsCombo.addItem("8 bars", 3);
    barsCombo.addItem("16 bars", 4);
    barsCombo.setSelectedId(3);

    tempoCombo.addItem("Auto", 1);
    tempoCombo.addItem("Half", 2);
    tempoCombo.addItem("Double", 3);
    tempoCombo.addItem("Orig", 4);
    tempoCombo.setSelectedId(1);

    reactivitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    reactivitySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    reactivitySlider.setRange(0.0, 1.0, 0.01);
    reactivitySlider.setValue(0.7, juce::dontSendNotification);
    reactivitySlider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(98, 170, 242));
    reactivitySlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(192, 220, 255));

    supportSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    supportSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    supportSlider.setRange(0.0, 1.0, 0.01);
    supportSlider.setValue(0.5, juce::dontSendNotification);
    supportSlider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(234, 150, 88));
    supportSlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(248, 210, 168));

    auto styleCombo = [](juce::ComboBox& combo)
    {
        combo.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGB(24, 22, 20));
        combo.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(222, 167, 94, 120));
        combo.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(231, 219, 201));
        combo.setColour(juce::ComboBox::arrowColourId, juce::Colour::fromRGB(248, 207, 132));
    };

    styleCombo(sourceCombo);
    styleCombo(modeCombo);
    styleCombo(applyModeCombo);
    styleCombo(barsCombo);
    styleCombo(tempoCombo);

    chooseFileButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(94, 68, 44));
    chooseFileButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(238, 227, 210));
    analyzeButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(176, 122, 62));
    analyzeButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(24, 21, 19));

    addAndMakeVisible(sourceLabel);
    addAndMakeVisible(modeLabel);
    addAndMakeVisible(applyModeLabel);
    addAndMakeVisible(barsLabel);
    addAndMakeVisible(tempoLabel);
    addAndMakeVisible(reactivityLabel);
    addAndMakeVisible(supportLabel);
    addAndMakeVisible(fileLabel);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(detailsLabel);
    addAndMakeVisible(debugLabel);
    addAndMakeVisible(sourceCombo);
    addAndMakeVisible(modeCombo);
    addAndMakeVisible(applyModeCombo);
    addAndMakeVisible(barsCombo);
    addAndMakeVisible(tempoCombo);
    addAndMakeVisible(reactivitySlider);
    addAndMakeVisible(supportSlider);
    addAndMakeVisible(chooseFileButton);
    addAndMakeVisible(analyzeButton);
    addAndMakeVisible(debugTextBox);

    sourceCombo.onChange = [this]
    {
        if (!onAnalysisSourceChanged)
            return;

        const auto id = sourceCombo.getSelectedId();
        const auto source = id == 3 ? SampleAnalysisRequest::SourceType::AudioFile
            : (id == 2 ? SampleAnalysisRequest::SourceType::LiveInput : SampleAnalysisRequest::SourceType::None);
        onAnalysisSourceChanged(source);
    };

    modeCombo.onChange = [this]
    {
        if (!onAnalysisModeChanged)
            return;

        const auto id = modeCombo.getSelectedId();
        const auto mode = id == 2 ? AnalysisMode::AnalyzeOnly
            : (id == 3 ? AnalysisMode::GenerateFromSample
                : (id == 4 ? AnalysisMode::ExtractFromSample : AnalysisMode::Off));
        onAnalysisModeChanged(mode);
    };

    applyModeCombo.onChange = [this]
    {
        if (!onSampleApplyModeChanged)
            return;

        const auto id = applyModeCombo.getSelectedId();
        const auto mode = id == 1 ? SampleApplyMode::GenreFirst
            : (id == 3 ? SampleApplyMode::SampleFirst
                : (id == 4 ? SampleApplyMode::ExactCopy : SampleApplyMode::Blend));
        onSampleApplyModeChanged(mode);
    };

    barsCombo.onChange = [this]
    {
        if (!onAnalysisBarsChanged)
            return;

        const auto id = barsCombo.getSelectedId();
        const int bars = id == 1 ? 2 : (id == 2 ? 4 : (id == 3 ? 8 : 16));
        onAnalysisBarsChanged(bars);
    };

    tempoCombo.onChange = [this]
    {
        if (!onAnalysisTempoHandlingChanged)
            return;

        const auto id = tempoCombo.getSelectedId();
        const auto mode = id == 2 ? SampleAnalysisRequest::TempoHandling::PreferHalfTime
            : (id == 3 ? SampleAnalysisRequest::TempoHandling::PreferDoubleTime
                : (id == 4 ? SampleAnalysisRequest::TempoHandling::KeepDetected
                    : SampleAnalysisRequest::TempoHandling::Auto));
        onAnalysisTempoHandlingChanged(mode);
    };

    reactivitySlider.onValueChange = [this]
    {
        if (onAnalysisReactivityChanged)
            onAnalysisReactivityChanged(static_cast<float>(reactivitySlider.getValue()));
    };

    supportSlider.onValueChange = [this]
    {
        if (onSupportVsContrastChanged)
            onSupportVsContrastChanged(static_cast<float>(supportSlider.getValue()));
    };

    chooseFileButton.onClick = [this]
    {
        if (onChooseAnalysisFile)
            onChooseAnalysisFile();
    };

    analyzeButton.onClick = [this]
    {
        if (onRunAnalysis)
            onRunAnalysis();
    };
}
} // namespace bbg
