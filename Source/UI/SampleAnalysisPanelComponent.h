#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Analysis/SampleAnalysisRequest.h"

namespace bbg
{
class SampleAnalysisPanelComponent : public juce::Component,
                                     public juce::FileDragAndDropTarget
{
public:
    SampleAnalysisPanelComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void setPanelState(SampleAnalysisRequest::SourceType source,
                       AnalysisMode mode,
                       int barsToCapture,
                       SampleAnalysisRequest::TempoHandling tempoHandling,
                       float reactivity,
                       float supportVsContrast,
                       const juce::String& fileLabel,
                       const juce::String& statusText,
                       const juce::String& detailsText,
                       const juce::String& debugText,
                       bool analysisReady);

    std::function<void(SampleAnalysisRequest::SourceType)> onAnalysisSourceChanged;
    std::function<void(AnalysisMode)> onAnalysisModeChanged;
    std::function<void(int)> onAnalysisBarsChanged;
    std::function<void(SampleAnalysisRequest::TempoHandling)> onAnalysisTempoHandlingChanged;
    std::function<void(float)> onAnalysisReactivityChanged;
    std::function<void(float)> onSupportVsContrastChanged;
    std::function<void()> onChooseAnalysisFile;
    std::function<void(const juce::File&)> onAnalysisFileDropped;
    std::function<void()> onRunAnalysis;

private:
    void setupUi();

    bool analysisFileDropHighlight = false;

    juce::Label analysisTitleLabel;
    juce::Label sourceLabel;
    juce::Label modeLabel;
    juce::Label barsLabel;
    juce::Label tempoLabel;
    juce::Label reactivityLabel;
    juce::Label supportLabel;
    juce::Label fileLabel;
    juce::Label statusLabel;
    juce::Label detailsLabel;
    juce::Label debugLabel;

    juce::ComboBox sourceCombo;
    juce::ComboBox modeCombo;
    juce::ComboBox barsCombo;
    juce::ComboBox tempoCombo;
    juce::Slider reactivitySlider;
    juce::Slider supportSlider;
    juce::TextButton chooseFileButton { "Open File" };
    juce::TextButton analyzeButton { "Analyze" };
    juce::TextEditor debugTextBox;
};
} // namespace bbg