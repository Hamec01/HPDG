#include "StyleLabReferenceBrowserComponent.h"

#include "../Services/StyleLabReferenceService.h"

namespace bbg
{
namespace
{
juce::String yesNo(bool value)
{
    return value ? "yes" : "no";
}

juce::String quotedOrDash(const juce::String& value)
{
    return value.isNotEmpty() ? ("\"" + value + "\"") : "-";
}
}

StyleLabReferenceBrowserComponent::StyleLabReferenceBrowserComponent()
{
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(subtitleLabel);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(refreshButton);
    addAndMakeVisible(openFolderButton);
    addAndMakeVisible(applyButton);
    addAndMakeVisible(referenceList);
    addAndMakeVisible(summaryLabel);
    addAndMakeVisible(laneDetailEditor);

    titleLabel.setText("Reference Browser", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    subtitleLabel.setText("Read-side view of saved references using runtimeLaneOrder + runtime lane layout metadata.", juce::dontSendNotification);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(162, 174, 188));

    statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(162, 174, 188));
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    referenceList.setModel(this);
    referenceList.setRowHeight(42);
    referenceList.setColour(juce::ListBox::backgroundColourId, juce::Colour::fromRGB(19, 23, 29));
    referenceList.setOutlineThickness(1);

    summaryLabel.setJustificationType(juce::Justification::topLeft);
    summaryLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(216, 224, 234));
    summaryLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(21, 25, 31));
    summaryLabel.setBorderSize({ 10, 10, 10, 10 });

    laneDetailEditor.setMultiLine(true);
    laneDetailEditor.setReadOnly(true);
    laneDetailEditor.setScrollbarsShown(true);
    laneDetailEditor.setCaretVisible(false);
    laneDetailEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(21, 25, 31));
    laneDetailEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(44, 54, 68));
    laneDetailEditor.setColour(juce::TextEditor::textColourId, juce::Colour::fromRGB(216, 224, 234));
    laneDetailEditor.setFont(juce::Font(juce::FontOptions(13.0f)));

    refreshButton.onClick = [this]
    {
        reloadCatalog();
    };

    openFolderButton.onClick = [this]
    {
        if (const auto* record = selectedReference())
            record->directory.startAsProcess();
    };

    applyButton.onClick = [this]
    {
        applySelectedReference();
    };

    reloadCatalog();
}

void StyleLabReferenceBrowserComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(14, 17, 22));

    auto bounds = getLocalBounds().toFloat().reduced(12.0f);
    g.setColour(juce::Colour::fromRGB(22, 27, 34));
    g.fillRoundedRectangle(bounds, 12.0f);

    g.setColour(juce::Colour::fromRGB(42, 52, 66));
    g.drawRoundedRectangle(bounds, 12.0f, 1.0f);
}

void StyleLabReferenceBrowserComponent::resized()
{
    auto area = getLocalBounds().reduced(22, 18);

    titleLabel.setBounds(area.removeFromTop(28));
    subtitleLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(10);

    auto toolbar = area.removeFromTop(30);
    refreshButton.setBounds(toolbar.removeFromLeft(96));
    toolbar.removeFromLeft(8);
    openFolderButton.setBounds(toolbar.removeFromLeft(112));
    toolbar.removeFromLeft(8);
    applyButton.setBounds(toolbar.removeFromLeft(124));
    statusLabel.setBounds(toolbar.reduced(12, 0));

    area.removeFromTop(10);
    auto content = area;
    auto left = content.removeFromLeft(290);
    referenceList.setBounds(left);
    content.removeFromLeft(12);

    summaryLabel.setBounds(content.removeFromTop(118));
    content.removeFromTop(10);
    laneDetailEditor.setBounds(content);
}

int StyleLabReferenceBrowserComponent::getNumRows()
{
    return static_cast<int>(catalog.references.size());
}

void StyleLabReferenceBrowserComponent::paintListBoxItem(int rowNumber,
                                                         juce::Graphics& g,
                                                         int width,
                                                         int height,
                                                         bool rowIsSelected)
{
    juce::ignoreUnused(height);
    g.fillAll(rowIsSelected ? juce::Colour::fromRGB(48, 64, 92) : juce::Colour::fromRGB(21, 25, 31));

    if (!juce::isPositiveAndBelow(rowNumber, static_cast<int>(catalog.references.size())))
        return;

    const auto& record = catalog.references[static_cast<size_t>(rowNumber)];
    const auto name = record.directory.getFileName();
    const auto laneSummary = juce::String(record.backedLaneCount) + " backed / " + juce::String(record.unbackedLaneCount) + " unbacked";

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText(name, 10, 4, width - 20, 18, juce::Justification::centredLeft, true);

    g.setColour(juce::Colour::fromRGB(172, 182, 194));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(record.genre + " / " + record.substyle + "  |  " + laneSummary,
               10,
               20,
               width - 20,
               16,
               juce::Justification::centredLeft,
               true);
}

void StyleLabReferenceBrowserComponent::selectedRowsChanged(int)
{
    refreshDetailPanel();
}

void StyleLabReferenceBrowserComponent::reloadCatalog()
{
    catalog = StyleLabReferenceBrowserService::loadReferenceCatalog(StyleLabReferenceService::getReferenceRootDirectory());
    referenceList.updateContent();

    if (!catalog.references.empty())
        referenceList.selectRow(0);
    else
        referenceList.deselectAllRows();

    if (!catalog.warnings.isEmpty())
        statusLabel.setText("Loaded with warnings: " + juce::String(catalog.warnings.size()), juce::dontSendNotification);
    else if (catalog.references.empty())
        statusLabel.setText("No references found.", juce::dontSendNotification);
    else
        statusLabel.setText("Loaded " + juce::String(static_cast<int>(catalog.references.size())) + " references.", juce::dontSendNotification);

    refreshDetailPanel();
}

void StyleLabReferenceBrowserComponent::applySelectedReference()
{
    const auto* record = selectedReference();
    if (record == nullptr)
    {
        statusLabel.setText("Select a reference first.", juce::dontSendNotification);
        return;
    }

    if (!onApplyReference)
    {
        statusLabel.setText("Apply callback is not connected.", juce::dontSendNotification);
        return;
    }

    juce::String statusMessage;
    if (onApplyReference(*record, statusMessage))
    {
        statusLabel.setText(statusMessage.isNotEmpty() ? statusMessage : "Reference applied.", juce::dontSendNotification);
        return;
    }

    statusLabel.setText(statusMessage.isNotEmpty() ? statusMessage : "Failed to apply reference.", juce::dontSendNotification);
}

void StyleLabReferenceBrowserComponent::refreshDetailPanel()
{
    const auto* record = selectedReference();
    openFolderButton.setEnabled(record != nullptr);
    applyButton.setEnabled(record != nullptr);

    if (record == nullptr)
    {
        summaryLabel.setText("Select a saved reference to inspect runtime lane metadata.", juce::dontSendNotification);
        laneDetailEditor.setText({}, juce::dontSendNotification);
        return;
    }

    summaryLabel.setText(buildReferenceSummary(*record), juce::dontSendNotification);
    laneDetailEditor.setText(buildLaneDetailText(*record), juce::dontSendNotification);
}

juce::String StyleLabReferenceBrowserComponent::buildReferenceSummary(const StyleLabReferenceRecord& record) const
{
    juce::String summary;
    summary << "Folder: " << record.directory.getFileName() << "\n";
    summary << "Genre/Substyle: " << record.genre << " / " << record.substyle << "\n";
    summary << "Bars/BPM Range: " << record.bars << " / " << record.tempoMin << "-" << record.tempoMax << "\n";
    summary << "Runtime Lanes: " << record.totalRuntimeLaneCount << " total, "
            << record.backedLaneCount << " backed, " << record.unbackedLaneCount << " unbacked\n";
    summary << "Notes: " << record.totalNotes << " total, " << record.taggedNotes << " tagged\n";
    summary << "Selected Lane: " << (record.selectedTrackLaneId.isNotEmpty() ? record.selectedTrackLaneId : "-") << "\n";
    summary << "Sound Target: " << (record.soundModuleTrackLaneId.isNotEmpty() ? record.soundModuleTrackLaneId : "-");

    if (record.hasDesignerMetadata)
        summary << "\nDesigner Metadata: secondary only (" << record.designerLaneCount << " lanes)";

    return summary;
}

juce::String StyleLabReferenceBrowserComponent::buildLaneDetailText(const StyleLabReferenceRecord& record) const
{
    juce::String detail;
    for (const auto& lane : record.runtimeLanes)
    {
        const bool isSelectedLane = lane.laneId == record.selectedTrackLaneId;
        const bool isSoundLane = lane.laneId == record.soundModuleTrackLaneId;

        detail << "[" << lane.orderIndex << "] " << lane.laneName << "  <" << lane.bindingKind << ">";
        if (isSelectedLane)
            detail << "  [selected]";
        if (isSoundLane)
            detail << "  [sound]";
        detail << "\n";
        detail << "  laneId: " << lane.laneId << "\n";
        detail << "  runtimeTrackType: " << (lane.runtimeTrackType.isNotEmpty() ? lane.runtimeTrackType : "-")
               << " | trackType: " << (lane.trackType.isNotEmpty() ? lane.trackType : "-") << "\n";
        detail << "  group/dependency: " << quotedOrDash(lane.groupName) << " / " << quotedOrDash(lane.dependencyName) << "\n";
        detail << "  registry/core/visible/drag/ghost: "
               << yesNo(lane.isRuntimeRegistryLane) << " / "
               << yesNo(lane.isCore) << " / "
               << yesNo(lane.isVisibleInEditor) << " / "
               << yesNo(lane.supportsDragExport) << " / "
               << yesNo(lane.isGhostTrack) << "\n";
        detail << "  defaultMidiNote: " << lane.defaultMidiNote
               << " | enabledByDefault: " << yesNo(lane.enabledByDefault)
               << " | hasBackingTrack: " << yesNo(lane.hasBackingTrack) << "\n";

        if (lane.laneParamsAvailable && lane.laneParams.available)
        {
            detail << "  laneParams: enabled=" << yesNo(lane.laneParams.enabled)
                   << ", muted=" << yesNo(lane.laneParams.muted)
                   << ", solo=" << yesNo(lane.laneParams.solo)
                   << ", locked=" << yesNo(lane.laneParams.locked)
                   << ", volume=" << juce::String(lane.laneParams.laneVolume, 2) << "\n";
            detail << "  sample: idx=" << lane.laneParams.selectedSampleIndex
                   << " name=" << quotedOrDash(lane.laneParams.selectedSampleName)
                   << " | laneRole=" << quotedOrDash(lane.laneParams.laneRole) << "\n";
            detail << "  notes/tagged: " << lane.laneParams.noteCount << " / " << lane.laneParams.taggedNoteCount << "\n";
            detail << "  sound: pan=" << juce::String(lane.laneParams.sound.pan, 2)
                   << ", width=" << juce::String(lane.laneParams.sound.width, 2)
                   << ", eq=" << juce::String(lane.laneParams.sound.eqTone, 2)
                   << ", comp=" << juce::String(lane.laneParams.sound.compression, 2)
                   << ", rev=" << juce::String(lane.laneParams.sound.reverb, 2)
                   << ", gate=" << juce::String(lane.laneParams.sound.gate, 2)
                   << ", trans=" << juce::String(lane.laneParams.sound.transient, 2)
                   << ", drive=" << juce::String(lane.laneParams.sound.drive, 2) << "\n";
        }
        else
        {
            detail << "  laneParams: unavailable for this lane metadata entry\n";
        }

        detail << "\n";
    }

    if (record.runtimeLanes.empty())
        detail = "No runtime lane metadata available.";

    return detail;
}

const StyleLabReferenceRecord* StyleLabReferenceBrowserComponent::selectedReference() const
{
    const int row = referenceList.getSelectedRow();
    if (!juce::isPositiveAndBelow(row, static_cast<int>(catalog.references.size())))
        return nullptr;

    return &catalog.references[static_cast<size_t>(row)];
}
} // namespace bbg