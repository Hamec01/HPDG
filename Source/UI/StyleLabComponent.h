#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Services/StyleLabReferenceBrowserService.h"
#include "../Services/StyleLabReferenceService.h"
#include "StyleLabDraftState.h"

namespace bbg
{
class StyleLabComponent final : public juce::Component
{
public:
    StyleLabComponent(const StyleLabState& initialState,
                      const StyleLabDraftState& initialDraftState,
                      std::function<juce::StringArray(const juce::String&)> substyleOptionsProvider = {});

    void paint(juce::Graphics& g) override;
    void resized() override;

    const StyleLabState& getState() const { return state; }
    const StyleLabDraftState& getDraftState() const { return draftState; }

    std::function<void(const StyleLabState&)> onStateChanged;
    std::function<void(const StyleLabDraftState&)> onDraftStateChanged;
    std::function<std::optional<PatternProject>()> onCaptureCurrentPattern;
    std::function<bool(const StyleLabState&, const StyleLabDraftState&, juce::String&)> onSaveReferencePattern;
    std::function<bool(const StyleLabReferenceRecord&, StyleLabState&, juce::String&)> onApplyReferencePattern;
    std::function<std::optional<StyleLabState>()> onResetToRuntimeLayout;

private:
    static juce::StringArray moodOptions();
    static juce::StringArray densityProfileOptions();
    static juce::StringArray parseTagsFromText(const juce::String& text);
    static juce::String joinTagsForText(const juce::StringArray& tags);
    void rebuildSubstyleComboForCurrentGenre();
    void syncControlsFromState();
    void refreshLaneRows();
    void syncDraftUi();
    void updateStatusText(const juce::String& text, juce::Colour colour);
    void emitStateChanged();
    void emitDraftStateChanged();
    void showReferenceBrowser();
    void captureCurrentPattern();
    void addLane();
    void resetLaneLayout();
    void duplicateLane(const juce::String& laneId);
    void deleteLane(const juce::String& laneId);
    void moveLane(const juce::String& laneId, int delta);
    void showLaneMenu(const juce::String& laneId, juce::Component* target);

    StyleLabLaneDefinition* findLane(const juce::String& laneId);
    const StyleLabLaneDefinition* findLane(const juce::String& laneId) const;

    StyleLabState state;
    StyleLabState defaultState;
    StyleLabDraftState draftState;
    std::function<juce::StringArray(const juce::String&)> substyleOptionsProvider;
    bool syncingControls = false;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label conflictLabel;
    juce::Label draftStatusLabel;
    juce::Label genreLabel;
    juce::Label substyleLabel;
    juce::Label barsLabel;
    juce::Label tempoMinLabel;
    juce::Label tempoMaxLabel;
    juce::Label tagsLabel;
    juce::Label moodLabel;
    juce::Label densityProfileLabel;
    juce::Label referencePriorityLabel;
    juce::Label referencePriorityValueLabel;
    juce::Label authoringNotesLabel;
    juce::ComboBox genreCombo;
    juce::ComboBox substyleCombo;
    juce::ComboBox barsCombo;
    juce::Slider tempoMinSlider;
    juce::Slider tempoMaxSlider;
    juce::TextEditor tagsEditor;
    juce::ComboBox moodCombo;
    juce::ComboBox densityProfileCombo;
    juce::Slider referencePrioritySlider;
    juce::TextEditor authoringNotesEditor;
    juce::TextButton captureButton { "Capture Current Pattern" };
    juce::TextButton addLaneButton { "Add Lane" };
    juce::TextButton resetLayoutButton { "Reset Layout" };
    juce::TextButton browseReferencesButton { "Browse References" };
    juce::TextButton saveReferenceButton { "Save Reference Pattern" };
    juce::Label statusLabel;
    juce::Viewport laneViewport;
    juce::Component laneRowsContent;
    std::vector<std::unique_ptr<juce::Component>> laneRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StyleLabComponent)
};
} // namespace bbg