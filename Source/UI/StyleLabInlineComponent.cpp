#include "StyleLabInlineComponent.h"

#include <cmath>

namespace bbg
{
namespace
{
juce::StringArray genreOptions()
{
    return { "Boom Bap", "Rap", "Trap", "Drill" };
}

void setupLabel(juce::Label& label, const juce::String& text, const juce::Colour colour = juce::Colour::fromRGB(182, 190, 202))
{
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, colour);
}

void setupCombo(juce::ComboBox& combo, const juce::StringArray& items)
{
    combo.clear(juce::dontSendNotification);
    for (int i = 0; i < items.size(); ++i)
        combo.addItem(items[i], i + 1);
}

void setupTempoSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
    slider.setRange(60.0, 180.0, 1.0);
    slider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(84, 128, 188));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(216, 228, 244));
}
} // namespace

StyleLabInlineComponent::StyleLabInlineComponent()
{
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(subtitleLabel);
    addAndMakeVisible(genreLabel);
    addAndMakeVisible(substyleLabel);
    addAndMakeVisible(barsLabel);
    addAndMakeVisible(tempoMinLabel);
    addAndMakeVisible(tempoMaxLabel);
    addAndMakeVisible(targetLaneLabel);
    addAndMakeVisible(statsLabel);
    addAndMakeVisible(workflowLabel);
    addAndMakeVisible(genreCombo);
    addAndMakeVisible(substyleCombo);
    addAndMakeVisible(barsCombo);
    addAndMakeVisible(tempoMinSlider);
    addAndMakeVisible(tempoMaxSlider);
    addAndMakeVisible(importMidiButton);
    addAndMakeVisible(saveReferenceButton);
    addAndMakeVisible(openDesignerButton);

    titleLabel.setText("Style Lab Mode", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    subtitleLabel.setText("Main workflow: draw, audition, and save reference patterns without leaving the editor.", juce::dontSendNotification);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 172, 188));
    subtitleLabel.setFont(juce::Font(juce::FontOptions(11.0f)));

    setupLabel(genreLabel, "Genre");
    setupLabel(substyleLabel, "Substyle");
    setupLabel(barsLabel, "Bars");
    setupLabel(tempoMinLabel, "Tempo Min");
    setupLabel(tempoMaxLabel, "Tempo Max");
    setupLabel(targetLaneLabel, "Import Target: Kick", juce::Colour::fromRGB(212, 220, 232));
    setupLabel(statsLabel, "Notes 0 | Tagged 0 | Tagged Lanes 0", juce::Colour::fromRGB(158, 176, 198));
    setupLabel(workflowLabel, "Workflow: draw or import MIDI, fix notes, tag roles, save reference.", juce::Colour::fromRGB(152, 196, 244));
    workflowLabel.setJustificationType(juce::Justification::centredLeft);

    setupCombo(genreCombo, genreOptions());
    barsCombo.addItem("1", 1);
    barsCombo.addItem("2", 2);
    barsCombo.addItem("4", 3);
    barsCombo.addItem("8", 4);
    barsCombo.addItem("16", 5);

    setupTempoSlider(tempoMinSlider);
    setupTempoSlider(tempoMaxSlider);

    importMidiButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(76, 112, 170));
    saveReferenceButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(90, 138, 204));
    openDesignerButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(60, 70, 86));

    genreCombo.onChange = [this]
    {
        if (!syncingControls && onGenreChanged)
            onGenreChanged(genreCombo.getText());
    };

    substyleCombo.onChange = [this]
    {
        if (!syncingControls && onSubstyleChanged)
            onSubstyleChanged(substyleCombo.getText());
    };

    barsCombo.onChange = [this]
    {
        if (!syncingControls && onBarsChanged)
            onBarsChanged(juce::jmax(1, barsCombo.getText().getIntValue()));
    };

    tempoMinSlider.onValueChange = [this]
    {
        if (!syncingControls)
            emitTempoRangeChanged();
    };

    tempoMaxSlider.onValueChange = [this]
    {
        if (!syncingControls)
            emitTempoRangeChanged();
    };

    importMidiButton.onClick = [this]
    {
        if (onImportMidi)
            onImportMidi();
    };

    saveReferenceButton.onClick = [this]
    {
        if (onSaveReferencePattern)
            onSaveReferencePattern();
    };

    openDesignerButton.onClick = [this]
    {
        if (onOpenDesigner)
            onOpenDesigner();
    };
}

void StyleLabInlineComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour::fromRGB(18, 22, 28));
    g.fillRoundedRectangle(bounds.reduced(1.0f), 8.0f);

    g.setColour(juce::Colour::fromRGB(48, 58, 74));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 8.0f, 1.0f);
}

void StyleLabInlineComponent::resized()
{
    auto area = getLocalBounds().reduced(10, 8);

    auto top = area.removeFromTop(18);
    titleLabel.setBounds(top.removeFromLeft(128));
    subtitleLabel.setBounds(top);

    area.removeFromTop(6);
    auto row = area.removeFromTop(42);

    const auto setField = [](juce::Rectangle<int>& work, int width, juce::Label& label, juce::Component& component)
    {
        auto slot = work.removeFromLeft(width);
        label.setBounds(slot.removeFromTop(12));
        component.setBounds(slot.removeFromTop(24));
        work.removeFromLeft(6);
    };

    setField(row, 92, genreLabel, genreCombo);
    setField(row, 120, substyleLabel, substyleCombo);
    setField(row, 52, barsLabel, barsCombo);
    setField(row, 124, tempoMinLabel, tempoMinSlider);
    setField(row, 124, tempoMaxLabel, tempoMaxSlider);

    importMidiButton.setBounds(row.removeFromLeft(96).reduced(0, 10));
    row.removeFromLeft(6);
    saveReferenceButton.setBounds(row.removeFromLeft(116).reduced(0, 10));
    row.removeFromLeft(6);
    openDesignerButton.setBounds(row.removeFromLeft(88).reduced(0, 10));

    area.removeFromTop(4);
    auto summaryRow = area.removeFromTop(18);
    targetLaneLabel.setBounds(summaryRow.removeFromLeft(180));
    statsLabel.setBounds(summaryRow);

    area.removeFromTop(2);
    workflowLabel.setBounds(area.removeFromTop(18));
}

void StyleLabInlineComponent::setSubstyleOptions(const juce::StringArray& options)
{
    const auto currentText = substyleCombo.getText();

    syncingControls = true;
    setupCombo(substyleCombo, options);
    if (options.contains(currentText))
        substyleCombo.setText(currentText, juce::dontSendNotification);
    else if (options.size() > 0)
        substyleCombo.setSelectedId(1, juce::dontSendNotification);
    syncingControls = false;
}

void StyleLabInlineComponent::setState(const StyleLabState& state)
{
    syncingControls = true;
    genreCombo.setText(state.genre, juce::dontSendNotification);
    substyleCombo.setText(state.substyle, juce::dontSendNotification);
    barsCombo.setText(juce::String(juce::jmax(1, state.bars)), juce::dontSendNotification);
    tempoMinSlider.setValue(state.tempoRange.getStart(), juce::dontSendNotification);
    tempoMaxSlider.setValue(state.tempoRange.getEnd(), juce::dontSendNotification);
    syncingControls = false;
}

void StyleLabInlineComponent::setWorkflowSummary(const juce::String& targetLane,
                                                 int totalNotes,
                                                 int taggedNotes,
                                                 int taggedLaneCount,
                                                 const juce::String& statusText)
{
    targetLaneLabel.setText("Import Target: " + (targetLane.isNotEmpty() ? targetLane : "Kick"), juce::dontSendNotification);
    statsLabel.setText("Notes " + juce::String(totalNotes)
                           + " | Tagged " + juce::String(taggedNotes)
                           + " | Tagged Lanes " + juce::String(taggedLaneCount),
                       juce::dontSendNotification);

    if (statusText.isNotEmpty())
        workflowLabel.setText(statusText, juce::dontSendNotification);
}

void StyleLabInlineComponent::emitTempoRangeChanged()
{
    const int minTempo = static_cast<int>(std::lround(tempoMinSlider.getValue()));
    const int maxTempo = static_cast<int>(std::lround(tempoMaxSlider.getValue()));
    const auto range = juce::Range<int>(juce::jmin(minTempo, maxTempo), juce::jmax(minTempo, maxTempo));

    syncingControls = true;
    tempoMinSlider.setValue(range.getStart(), juce::dontSendNotification);
    tempoMaxSlider.setValue(range.getEnd(), juce::dontSendNotification);
    syncingControls = false;

    if (onTempoRangeChanged)
        onTempoRangeChanged(range);
}
} // namespace bbg