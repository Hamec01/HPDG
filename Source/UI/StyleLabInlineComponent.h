#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Services/StyleLabReferenceService.h"

namespace bbg
{
class StyleLabInlineComponent final : public juce::Component
{
public:
    StyleLabInlineComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

    int getPreferredHeight() const { return 104; }
    void setSubstyleOptions(const juce::StringArray& options);
    void setState(const StyleLabState& state);
    void setWorkflowSummary(const juce::String& targetLane,
                            int totalNotes,
                            int taggedNotes,
                            int taggedLaneCount,
                            const juce::String& statusText);

    std::function<void(const juce::String&)> onGenreChanged;
    std::function<void(const juce::String&)> onSubstyleChanged;
    std::function<void(int)> onBarsChanged;
    std::function<void(juce::Range<int>)> onTempoRangeChanged;
    std::function<void()> onImportMidi;
    std::function<void()> onSaveReferencePattern;
    std::function<void()> onOpenDesigner;

private:
    void emitTempoRangeChanged();

    bool syncingControls = false;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label genreLabel;
    juce::Label substyleLabel;
    juce::Label barsLabel;
    juce::Label tempoMinLabel;
    juce::Label tempoMaxLabel;
    juce::Label targetLaneLabel;
    juce::Label statsLabel;
    juce::Label workflowLabel;
    juce::Label libraryMetaLabel;
    juce::ComboBox genreCombo;
    juce::ComboBox substyleCombo;
    juce::ComboBox barsCombo;
    juce::Slider tempoMinSlider;
    juce::Slider tempoMaxSlider;
    juce::TextButton importMidiButton { "Import MIDI" };
    juce::TextButton saveReferenceButton { "Save Reference" };
    juce::TextButton openDesignerButton { "Designer" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StyleLabInlineComponent)
};
} // namespace bbg