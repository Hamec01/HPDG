#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Services/StyleLabReferenceBrowserService.h"

namespace bbg
{
class StyleLabReferenceBrowserComponent final : public juce::Component,
                                                private juce::ListBoxModel
{
public:
    StyleLabReferenceBrowserComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<bool(const StyleLabReferenceRecord&, juce::String&)> onApplyReference;

private:
    int getNumRows() override;
    void paintListBoxItem(int rowNumber,
                          juce::Graphics& g,
                          int width,
                          int height,
                          bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void reloadCatalog();
    void rebuildFilteredReferences();
    void refreshFilterChoices();
    void applySelectedReference();
    void refreshDetailPanel();
    juce::String buildReferenceSummary(const StyleLabReferenceRecord& record) const;
    juce::String buildLaneDetailText(const StyleLabReferenceRecord& record) const;
    const StyleLabReferenceRecord* selectedReference() const;

    StyleLabReferenceCatalog catalog;
    std::vector<int> filteredReferenceIndices;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label statusLabel;
    juce::TextButton refreshButton { "Refresh" };
    juce::TextButton openFolderButton { "Open Folder" };
    juce::TextButton applyButton { "Apply To Editor" };
    juce::Label tagFilterLabel;
    juce::Label moodFilterLabel;
    juce::Label densityFilterLabel;
    juce::Label sortLabel;
    juce::TextEditor tagFilterEditor;
    juce::ComboBox moodFilterCombo;
    juce::ComboBox densityFilterCombo;
    juce::ComboBox sortCombo;
    juce::ListBox referenceList;
    juce::Label summaryLabel;
    juce::TextEditor laneDetailEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StyleLabReferenceBrowserComponent)
};
} // namespace bbg