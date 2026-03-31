#include "StyleLabComponent.h"

#include <algorithm>

#include "StyleLabReferenceBrowserComponent.h"

namespace bbg
{
namespace
{
constexpr int kRowHeight = 74;

juce::String makeLaneId()
{
    return juce::Uuid().toString();
}

juce::StringArray genreOptions()
{
    return { "Boom Bap", "Rap", "Trap", "Drill" };
}

juce::StringArray moodOptions()
{
    return { "Unspecified", "Dark", "Aggressive", "Melancholic", "Warm", "Bouncy", "Tense", "Atmospheric" };
}

juce::StringArray densityProfileOptions()
{
    return { "Unspecified", "Sparse", "Balanced", "Dense", "Busy", "Minimal", "Punchy" };
}

juce::StringArray groupOptions()
{
    return { "Kick", "Snare", "Hats", "Bass", "Perc", "Texture", "FX", "Support", "Custom" };
}

void setupLabel(juce::Label& label, const juce::String& text, const juce::Colour colour = juce::Colour::fromRGB(182, 190, 202))
{
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, colour);
}

void setupCombo(juce::ComboBox& combo, const juce::StringArray& items)
{
    int itemId = 1;
    for (const auto& item : items)
        combo.addItem(item, itemId++);
}

void setupTempoSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
    slider.setRange(60.0, 180.0, 1.0);
    slider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(84, 128, 188));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(216, 228, 244));
}

void setupMetadataEditor(juce::TextEditor& editor, bool multiLine = false)
{
    editor.setMultiLine(multiLine);
    editor.setReturnKeyStartsNewLine(multiLine);
    editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(30, 35, 44));
    editor.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(54, 64, 80));
    editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour::fromRGB(104, 148, 214));
    editor.setColour(juce::TextEditor::textColourId, juce::Colour::fromRGB(220, 228, 238));
}

class StyleLabLaneRowComponent final : public juce::Component
{
public:
    std::function<void(const juce::String&, const juce::String&)> onNameCommitted;
    std::function<void(const juce::String&, const juce::String&)> onGroupChanged;
    std::function<void(const juce::String&, const juce::String&)> onDependencyChanged;
    std::function<void(const juce::String&, int)> onPriorityChanged;
    std::function<void(const juce::String&, bool)> onCoreChanged;
    std::function<void(const juce::String&, int)> onMoveRequested;
    std::function<void(const juce::String&, juce::Component*)> onMenuRequested;

    StyleLabLaneRowComponent()
    {
        addAndMakeVisible(nameEditor);
        addAndMakeVisible(groupCombo);
        addAndMakeVisible(dependencyCombo);
        addAndMakeVisible(prioritySlider);
        addAndMakeVisible(priorityValueLabel);
        addAndMakeVisible(coreToggle);
        addAndMakeVisible(upButton);
        addAndMakeVisible(downButton);
        addAndMakeVisible(menuButton);
        addAndMakeVisible(runtimeBadgeLabel);

        nameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(30, 35, 44));
        nameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(54, 64, 80));
        nameEditor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour::fromRGB(104, 148, 214));
        auto commitName = [this]
        {
            if (onNameCommitted)
                onNameCommitted(laneId, nameEditor.getText().trim());
        };
        nameEditor.onReturnKey = commitName;
        nameEditor.onFocusLost = commitName;

        setupCombo(groupCombo, groupOptions());
        groupCombo.onChange = [this]
        {
            if (onGroupChanged)
                onGroupChanged(laneId, groupCombo.getText());
        };

        dependencyCombo.onChange = [this]
        {
            if (onDependencyChanged)
                onDependencyChanged(laneId, dependencyCombo.getSelectedId() == 1 ? juce::String() : dependencyCombo.getText());
        };

        prioritySlider.setSliderStyle(juce::Slider::LinearHorizontal);
        prioritySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        prioritySlider.setRange(0.0, 100.0, 1.0);
        prioritySlider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(90, 136, 198));
        prioritySlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(220, 230, 244));
        prioritySlider.onValueChange = [this]
        {
            const auto priority = static_cast<int>(std::lround(prioritySlider.getValue()));
            priorityValueLabel.setText(juce::String(priority), juce::dontSendNotification);
            if (onPriorityChanged)
                onPriorityChanged(laneId, priority);
        };
        priorityValueLabel.setJustificationType(juce::Justification::centred);
        priorityValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(196, 204, 214));

        coreToggle.setButtonText("Core");
        coreToggle.onClick = [this]
        {
            if (onCoreChanged)
                onCoreChanged(laneId, coreToggle.getToggleState());
        };

        upButton.setButtonText("Up");
        upButton.onClick = [this]
        {
            if (onMoveRequested)
                onMoveRequested(laneId, -1);
        };

        downButton.setButtonText("Down");
        downButton.onClick = [this]
        {
            if (onMoveRequested)
                onMoveRequested(laneId, 1);
        };

        menuButton.setButtonText("...");
        menuButton.onClick = [this]
        {
            if (onMenuRequested)
                onMenuRequested(laneId, &menuButton);
        };

        runtimeBadgeLabel.setJustificationType(juce::Justification::centredLeft);
        runtimeBadgeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(126, 210, 166));
    }

    void configure(const StyleLabLaneDefinition& lane,
                   const juce::StringArray& dependencies,
                   bool canMoveUp,
                   bool canMoveDown)
    {
        laneId = lane.id;
        nameEditor.setText(lane.laneName, juce::dontSendNotification);
        groupCombo.setText(lane.groupName, juce::dontSendNotification);

        dependencyCombo.clear(juce::dontSendNotification);
        dependencyCombo.addItem("None", 1);
        int itemId = 2;
        for (const auto& dependency : dependencies)
        {
            if (dependency != lane.laneName)
                dependencyCombo.addItem(dependency, itemId++);
        }
        dependencyCombo.setText(lane.dependencyName.isNotEmpty() ? lane.dependencyName : "None", juce::dontSendNotification);

        prioritySlider.setValue(lane.generationPriority, juce::dontSendNotification);
        priorityValueLabel.setText(juce::String(lane.generationPriority), juce::dontSendNotification);
        coreToggle.setToggleState(lane.isCore, juce::dontSendNotification);
        coreToggle.setButtonText(lane.isCore ? "Core" : "Optional");
        upButton.setEnabled(canMoveUp);
        downButton.setEnabled(canMoveDown);
        const auto runtimeBadge = lane.isRuntimeRegistryLane ? "Runtime-linked" : "Reference-only lane";
        runtimeBadgeLabel.setText(juce::String(runtimeBadge) + (lane.enabled ? " | Enabled" : " | Disabled"),
                                  juce::dontSendNotification);
        runtimeBadgeLabel.setColour(juce::Label::textColourId,
                                    lane.enabled ? juce::Colour::fromRGB(126, 210, 166)
                                                 : juce::Colour::fromRGB(214, 156, 120));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour::fromRGB(21, 25, 31));
        g.fillRoundedRectangle(bounds.reduced(1.0f), 8.0f);

        g.setColour(juce::Colour::fromRGB(44, 54, 68));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 8.0f, 1.0f);

        g.setColour(juce::Colour::fromRGB(180, 188, 201));
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText("Lane", 14, 8, 80, 18, juce::Justification::centredLeft, false);
        g.drawText("Group", 214, 8, 80, 18, juce::Justification::centredLeft, false);
        g.drawText("Dependency", 366, 8, 96, 18, juce::Justification::centredLeft, false);
        g.drawText("Priority", 558, 8, 72, 18, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12, 10);
        area.removeFromTop(18);

        auto row = area.removeFromTop(28);
        nameEditor.setBounds(row.removeFromLeft(184));
        row.removeFromLeft(10);
        groupCombo.setBounds(row.removeFromLeft(132));
        row.removeFromLeft(10);
        dependencyCombo.setBounds(row.removeFromLeft(168));
        row.removeFromLeft(10);
        prioritySlider.setBounds(row.removeFromLeft(118));
        priorityValueLabel.setBounds(row.removeFromLeft(36));
        row.removeFromLeft(8);
        coreToggle.setBounds(row.removeFromLeft(82));
        row.removeFromLeft(8);
        upButton.setBounds(row.removeFromLeft(46));
        row.removeFromLeft(4);
        downButton.setBounds(row.removeFromLeft(56));
        row.removeFromLeft(4);
        menuButton.setBounds(row.removeFromLeft(40));

        runtimeBadgeLabel.setBounds(14, getHeight() - 24, 180, 16);
    }

private:
    juce::String laneId;
    juce::TextEditor nameEditor;
    juce::ComboBox groupCombo;
    juce::ComboBox dependencyCombo;
    juce::Slider prioritySlider;
    juce::Label priorityValueLabel;
    juce::ToggleButton coreToggle;
    juce::TextButton upButton;
    juce::TextButton downButton;
    juce::TextButton menuButton;
    juce::Label runtimeBadgeLabel;
};
}

StyleLabComponent::StyleLabComponent(const StyleLabState& initialState,
                                     const StyleLabDraftState& initialDraftState,
                                     std::function<juce::StringArray(const juce::String&)> provider)
    : state(initialState)
    , defaultState(initialState)
    , draftState(initialDraftState)
    , substyleOptionsProvider(std::move(provider))
{
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(subtitleLabel);
    addAndMakeVisible(conflictLabel);
    addAndMakeVisible(draftStatusLabel);
    addAndMakeVisible(genreLabel);
    addAndMakeVisible(substyleLabel);
    addAndMakeVisible(barsLabel);
    addAndMakeVisible(tempoMinLabel);
    addAndMakeVisible(tempoMaxLabel);
    addAndMakeVisible(tagsLabel);
    addAndMakeVisible(moodLabel);
    addAndMakeVisible(densityProfileLabel);
    addAndMakeVisible(referencePriorityLabel);
    addAndMakeVisible(referencePriorityValueLabel);
    addAndMakeVisible(authoringNotesLabel);
    addAndMakeVisible(genreCombo);
    addAndMakeVisible(substyleCombo);
    addAndMakeVisible(barsCombo);
    addAndMakeVisible(tempoMinSlider);
    addAndMakeVisible(tempoMaxSlider);
    addAndMakeVisible(tagsEditor);
    addAndMakeVisible(moodCombo);
    addAndMakeVisible(densityProfileCombo);
    addAndMakeVisible(referencePrioritySlider);
    addAndMakeVisible(authoringNotesEditor);
    addAndMakeVisible(captureButton);
    addAndMakeVisible(addLaneButton);
    addAndMakeVisible(resetLayoutButton);
    addAndMakeVisible(browseReferencesButton);
    addAndMakeVisible(saveReferenceButton);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(laneViewport);

    laneViewport.setViewedComponent(&laneRowsContent, false);
    laneViewport.setScrollBarsShown(true, false);

    titleLabel.setText("Style Lab", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    subtitleLabel.setText("Current Pattern, Style Lab Draft, and saved references are separate. Draft updates only when you capture it.", juce::dontSendNotification);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(162, 174, 188));

    conflictLabel.setText("[REFERENCE NOTE] Lane Layout Designer stays reference-only. Save Reference exports the current runtime lane model from PatternProject; designer lane edits stay secondary metadata until applied.", juce::dontSendNotification);
    conflictLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(246, 186, 88));

    draftStatusLabel.setJustificationType(juce::Justification::centredRight);
    draftStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(162, 174, 188));

    setupLabel(genreLabel, "Genre");
    setupLabel(substyleLabel, "Substyle");
    setupLabel(barsLabel, "Bars");
    setupLabel(tempoMinLabel, "Tempo Min");
    setupLabel(tempoMaxLabel, "Tempo Max");
    setupLabel(tagsLabel, "Tags");
    setupLabel(moodLabel, "Mood");
    setupLabel(densityProfileLabel, "Density");
    setupLabel(referencePriorityLabel, "Priority");
    setupLabel(authoringNotesLabel, "Authoring Notes");

    setupCombo(genreCombo, genreOptions());
    setupCombo(moodCombo, moodOptions());
    setupCombo(densityProfileCombo, densityProfileOptions());
    barsCombo.addItem("1", 1);
    barsCombo.addItem("2", 2);
    barsCombo.addItem("4", 3);
    barsCombo.addItem("8", 4);
    barsCombo.addItem("16", 5);

    setupTempoSlider(tempoMinSlider);
    setupTempoSlider(tempoMaxSlider);
    setupMetadataEditor(tagsEditor, false);
    setupMetadataEditor(authoringNotesEditor, false);

    referencePrioritySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    referencePrioritySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    referencePrioritySlider.setRange(0.0, 100.0, 1.0);
    referencePrioritySlider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(84, 128, 188));
    referencePrioritySlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(216, 228, 244));
    referencePriorityValueLabel.setJustificationType(juce::Justification::centredRight);
    referencePriorityValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(196, 204, 214));

    genreCombo.onChange = [this]
    {
        if (syncingControls)
            return;
        state.genre = genreCombo.getText();
        rebuildSubstyleComboForCurrentGenre();
        emitStateChanged();
    };

    substyleCombo.onChange = [this]
    {
        if (syncingControls)
            return;
        state.substyle = substyleCombo.getText();
        emitStateChanged();
    };

    barsCombo.onChange = [this]
    {
        if (syncingControls)
            return;
        state.bars = juce::jmax(1, barsCombo.getText().getIntValue());
        emitStateChanged();
    };

    tempoMinSlider.onValueChange = [this]
    {
        if (syncingControls)
            return;

        const auto minTempo = static_cast<int>(std::lround(tempoMinSlider.getValue()));
        const auto maxTempo = static_cast<int>(std::lround(tempoMaxSlider.getValue()));
        state.tempoRange = { juce::jmin(minTempo, maxTempo), juce::jmax(minTempo, maxTempo) };
        syncControlsFromState();
        emitStateChanged();
    };

    tempoMaxSlider.onValueChange = [this]
    {
        if (syncingControls)
            return;

        const auto minTempo = static_cast<int>(std::lround(tempoMinSlider.getValue()));
        const auto maxTempo = static_cast<int>(std::lround(tempoMaxSlider.getValue()));
        state.tempoRange = { juce::jmin(minTempo, maxTempo), juce::jmax(minTempo, maxTempo) };
        syncControlsFromState();
        emitStateChanged();
    };

    auto commitTags = [this]
    {
        if (syncingControls)
            return;
        state.tags = parseTagsFromText(tagsEditor.getText());
        emitStateChanged();
    };
    tagsEditor.onReturnKey = commitTags;
    tagsEditor.onFocusLost = commitTags;

    moodCombo.onChange = [this]
    {
        if (syncingControls)
            return;
        state.mood = moodCombo.getSelectedId() <= 1 ? juce::String() : moodCombo.getText();
        emitStateChanged();
    };

    densityProfileCombo.onChange = [this]
    {
        if (syncingControls)
            return;
        state.densityProfile = densityProfileCombo.getSelectedId() <= 1 ? juce::String() : densityProfileCombo.getText();
        emitStateChanged();
    };

    referencePrioritySlider.onValueChange = [this]
    {
        if (syncingControls)
            return;
        state.referencePriority = static_cast<int>(std::lround(referencePrioritySlider.getValue()));
        referencePriorityValueLabel.setText(juce::String(state.referencePriority), juce::dontSendNotification);
        emitStateChanged();
    };

    auto commitAuthoringNotes = [this]
    {
        if (syncingControls)
            return;
        state.authoringNotes = authoringNotesEditor.getText().trim();
        emitStateChanged();
    };
    authoringNotesEditor.onReturnKey = commitAuthoringNotes;
    authoringNotesEditor.onFocusLost = commitAuthoringNotes;

    captureButton.onClick = [this] { captureCurrentPattern(); };
    addLaneButton.onClick = [this] { addLane(); };
    resetLayoutButton.onClick = [this] { resetLaneLayout(); };
    browseReferencesButton.onClick = [this] { showReferenceBrowser(); };
    saveReferenceButton.onClick = [this]
    {
        if (!draftState.hasCapturedPattern)
        {
            updateStatusText("No draft captured. Use Capture Current Pattern before saving.", juce::Colour::fromRGB(246, 186, 88));
            return;
        }

        if (!onSaveReferencePattern)
        {
            updateStatusText("Save path is not connected.", juce::Colour::fromRGB(246, 186, 88));
            return;
        }

        updateStatusText("Saving Style Lab draft as a reference...", juce::Colour::fromRGB(182, 190, 202));
        juce::String statusMessage;
        if (onSaveReferencePattern(state, draftState, statusMessage))
        {
            draftState.isDirty = false;
            emitDraftStateChanged();
            syncDraftUi();
            updateStatusText(statusMessage.isNotEmpty() ? statusMessage : "Draft saved as a reference.",
                             juce::Colour::fromRGB(152, 204, 168));
        }
        else
        {
            updateStatusText(statusMessage.isNotEmpty() ? statusMessage : "Failed to save Style Lab draft.",
                             juce::Colour::fromRGB(246, 186, 88));
        }
    };

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(162, 174, 188));
    updateStatusText("Ready. Capture the current pattern when you want to refresh the Style Lab draft.", juce::Colour::fromRGB(162, 174, 188));

    syncControlsFromState();
    syncDraftUi();
    refreshLaneRows();
}

void StyleLabComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(14, 17, 22));

    auto bounds = getLocalBounds().toFloat().reduced(14.0f);
    g.setColour(juce::Colour::fromRGB(22, 27, 34));
    g.fillRoundedRectangle(bounds, 14.0f);

    g.setColour(juce::Colour::fromRGB(42, 52, 66));
    g.drawRoundedRectangle(bounds, 14.0f, 1.0f);

    auto glow = bounds.removeFromTop(120.0f);
    juce::ColourGradient gradient(juce::Colour::fromRGBA(90, 138, 210, 90), glow.getTopLeft(),
                                  juce::Colour::fromRGBA(14, 17, 22, 0), glow.getBottomRight(), false);
    g.setGradientFill(gradient);
    g.fillRoundedRectangle(glow, 14.0f);
}

void StyleLabComponent::resized()
{
    auto area = getLocalBounds().reduced(28, 24);

    titleLabel.setBounds(area.removeFromTop(32));
    subtitleLabel.setBounds(area.removeFromTop(22));
    auto infoRow = area.removeFromTop(22);
    conflictLabel.setBounds(infoRow.removeFromLeft(juce::jmax(240, infoRow.getWidth() - 220)));
    draftStatusLabel.setBounds(infoRow);
    area.removeFromTop(10);

    auto controls = area.removeFromTop(126);
    const int labelHeight = 18;
    const int fieldHeight = 24;
    const int columnGap = 12;
    const int columnWidth = (controls.getWidth() - columnGap * 4) / 5;

    auto setField = [labelHeight, fieldHeight](juce::Label& label, juce::Component& field, juce::Rectangle<int> bounds)
    {
        label.setBounds(bounds.removeFromTop(labelHeight));
        field.setBounds(bounds.removeFromTop(fieldHeight));
    };

    juce::Rectangle<int> column = controls.removeFromLeft(columnWidth);
    setField(genreLabel, genreCombo, column);
    controls.removeFromLeft(columnGap);

    column = controls.removeFromLeft(columnWidth);
    setField(substyleLabel, substyleCombo, column);
    controls.removeFromLeft(columnGap);

    column = controls.removeFromLeft(columnWidth);
    setField(barsLabel, barsCombo, column);
    controls.removeFromLeft(columnGap);

    column = controls.removeFromLeft(columnWidth);
    setField(tempoMinLabel, tempoMinSlider, column);
    controls.removeFromLeft(columnGap);

    column = controls.removeFromLeft(columnWidth);
    setField(tempoMaxLabel, tempoMaxSlider, column);

    controls.removeFromTop(8);
    auto metadataRow = controls.removeFromTop(42);
    auto tagsBounds = metadataRow.removeFromLeft(columnWidth * 2 + columnGap);
    setField(tagsLabel, tagsEditor, tagsBounds);
    metadataRow.removeFromLeft(columnGap);

    column = metadataRow.removeFromLeft(columnWidth);
    setField(moodLabel, moodCombo, column);
    metadataRow.removeFromLeft(columnGap);

    column = metadataRow.removeFromLeft(columnWidth);
    setField(densityProfileLabel, densityProfileCombo, column);
    metadataRow.removeFromLeft(columnGap);

    auto priorityBounds = metadataRow.removeFromLeft(columnWidth);
    referencePriorityLabel.setBounds(priorityBounds.removeFromTop(labelHeight));
    auto priorityField = priorityBounds.removeFromTop(fieldHeight);
    referencePriorityValueLabel.setBounds(priorityField.removeFromRight(34));
    referencePrioritySlider.setBounds(priorityField);

    controls.removeFromTop(8);
    auto notesRow = controls.removeFromTop(42);
    authoringNotesLabel.setBounds(notesRow.removeFromTop(labelHeight));
    authoringNotesEditor.setBounds(notesRow.removeFromTop(fieldHeight));

    area.removeFromTop(8);
    auto toolbar = area.removeFromTop(30);
    captureButton.setBounds(toolbar.removeFromLeft(172));
    toolbar.removeFromLeft(8);
    addLaneButton.setBounds(toolbar.removeFromLeft(110));
    toolbar.removeFromLeft(8);
    resetLayoutButton.setBounds(toolbar.removeFromLeft(108));
    toolbar.removeFromLeft(8);
    browseReferencesButton.setBounds(toolbar.removeFromLeft(148));
    toolbar.removeFromLeft(8);
    saveReferenceButton.setBounds(toolbar.removeFromLeft(180));
    statusLabel.setBounds(toolbar.reduced(12, 0));

    area.removeFromTop(8);
    laneViewport.setBounds(area);
    laneRowsContent.setSize(laneViewport.getWidth() - 18, static_cast<int>(laneRows.size()) * kRowHeight);

    int y = 0;
    for (auto& row : laneRows)
    {
        row->setBounds(0, y, laneRowsContent.getWidth(), kRowHeight - 6);
        y += kRowHeight;
    }
}

void StyleLabComponent::rebuildSubstyleComboForCurrentGenre()
{
    const auto validGenres = genreOptions();
    const auto resolvedGenre = validGenres.contains(state.genre) ? state.genre : validGenres[0];
    auto options = substyleOptionsProvider ? substyleOptionsProvider(resolvedGenre) : juce::StringArray {};
    const auto preferredSubstyle = state.substyle.isNotEmpty() ? state.substyle : substyleCombo.getText();

    if (options.isEmpty() && preferredSubstyle.isNotEmpty())
        options.add(preferredSubstyle);

    syncingControls = true;
    genreCombo.setText(resolvedGenre, juce::dontSendNotification);
    substyleCombo.clear(juce::dontSendNotification);
    setupCombo(substyleCombo, options);

    juce::String resolvedSubstyle;
    if (options.contains(preferredSubstyle))
        resolvedSubstyle = preferredSubstyle;
    else if (!options.isEmpty())
        resolvedSubstyle = options[0];

    substyleCombo.setText(resolvedSubstyle, juce::dontSendNotification);
    syncingControls = false;

    state.genre = resolvedGenre;
    state.substyle = resolvedSubstyle;
}

void StyleLabComponent::syncControlsFromState()
{
    syncingControls = true;
    genreCombo.setText(state.genre, juce::dontSendNotification);
    syncingControls = false;
    rebuildSubstyleComboForCurrentGenre();
    syncingControls = true;
    barsCombo.setText(juce::String(state.bars), juce::dontSendNotification);
    tempoMinSlider.setValue(state.tempoRange.getStart(), juce::dontSendNotification);
    tempoMaxSlider.setValue(state.tempoRange.getEnd(), juce::dontSendNotification);
    tagsEditor.setText(joinTagsForText(state.tags), juce::dontSendNotification);
    moodCombo.setSelectedId(1, juce::dontSendNotification);
    if (state.mood.isNotEmpty())
        moodCombo.setText(state.mood, juce::dontSendNotification);
    densityProfileCombo.setSelectedId(1, juce::dontSendNotification);
    if (state.densityProfile.isNotEmpty())
        densityProfileCombo.setText(state.densityProfile, juce::dontSendNotification);
    referencePrioritySlider.setValue(state.referencePriority, juce::dontSendNotification);
    referencePriorityValueLabel.setText(juce::String(state.referencePriority), juce::dontSendNotification);
    authoringNotesEditor.setText(state.authoringNotes, juce::dontSendNotification);
    syncingControls = false;
}

void StyleLabComponent::refreshLaneRows()
{
    laneRows.clear();
    laneRowsContent.removeAllChildren();

    juce::StringArray dependencies;
    for (const auto& lane : state.laneDefinitions)
        dependencies.add(lane.laneName);

    for (size_t index = 0; index < state.laneDefinitions.size(); ++index)
    {
        const auto& lane = state.laneDefinitions[index];
        auto row = std::make_unique<StyleLabLaneRowComponent>();
        row->configure(lane, dependencies, index > 0, index + 1 < state.laneDefinitions.size());

        row->onNameCommitted = [this](const juce::String& laneId, const juce::String& value)
        {
            if (auto* lane = findLane(laneId))
            {
                lane->laneName = value.isNotEmpty() ? value : "Unnamed Lane";
                refreshLaneRows();
                emitStateChanged();
            }
        };

        row->onGroupChanged = [this](const juce::String& laneId, const juce::String& value)
        {
            if (auto* lane = findLane(laneId))
            {
                lane->groupName = value;
                emitStateChanged();
            }
        };

        row->onDependencyChanged = [this](const juce::String& laneId, const juce::String& value)
        {
            if (auto* lane = findLane(laneId))
            {
                lane->dependencyName = value;
                emitStateChanged();
            }
        };

        row->onPriorityChanged = [this](const juce::String& laneId, int value)
        {
            if (auto* lane = findLane(laneId))
            {
                lane->generationPriority = value;
                emitStateChanged();
            }
        };

        row->onCoreChanged = [this](const juce::String& laneId, bool isCore)
        {
            if (auto* lane = findLane(laneId))
            {
                lane->isCore = isCore;
                refreshLaneRows();
                emitStateChanged();
            }
        };

        row->onMoveRequested = [this](const juce::String& laneId, int delta)
        {
            moveLane(laneId, delta);
        };

        row->onMenuRequested = [this](const juce::String& laneId, juce::Component* target)
        {
            showLaneMenu(laneId, target);
        };

        laneRowsContent.addAndMakeVisible(*row);
        laneRows.push_back(std::move(row));
    }

    resized();
}

void StyleLabComponent::updateStatusText(const juce::String& text, juce::Colour colour)
{
    statusLabel.setColour(juce::Label::textColourId, colour);
    statusLabel.setText(text, juce::dontSendNotification);
}

void StyleLabComponent::syncDraftUi()
{
    if (!draftState.hasCapturedPattern)
    {
        draftStatusLabel.setText("Draft: No Capture", juce::dontSendNotification);
        draftStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(206, 176, 120));
    }
    else if (draftState.isDirty)
    {
        draftStatusLabel.setText("Draft: Unsaved Draft", juce::dontSendNotification);
        draftStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(246, 186, 88));
    }
    else
    {
        draftStatusLabel.setText("Draft: Saved", juce::dontSendNotification);
        draftStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 204, 168));
    }

    saveReferenceButton.setEnabled(draftState.hasCapturedPattern);
}

void StyleLabComponent::emitStateChanged()
{
    if (onStateChanged)
        onStateChanged(state);

    const auto conflict = StyleLabReferenceService::buildConflictDescription(state);
    if (conflict.isNotEmpty())
        updateStatusText(conflict, juce::Colour::fromRGB(246, 186, 88));
}

void StyleLabComponent::emitDraftStateChanged()
{
    if (onDraftStateChanged)
        onDraftStateChanged(draftState);
}

void StyleLabComponent::captureCurrentPattern()
{
    if (!onCaptureCurrentPattern)
    {
        updateStatusText("Capture path is not connected.", juce::Colour::fromRGB(246, 186, 88));
        return;
    }

    const auto captured = onCaptureCurrentPattern();
    if (!captured.has_value())
    {
        updateStatusText("Failed to capture the current pattern.", juce::Colour::fromRGB(246, 186, 88));
        return;
    }

    draftState.draftPattern = *captured;
    draftState.isDirty = true;
    draftState.hasCapturedPattern = true;
    emitDraftStateChanged();
    syncDraftUi();
    updateStatusText("Current pattern captured into the Style Lab draft.", juce::Colour::fromRGB(152, 204, 168));
}

void StyleLabComponent::showReferenceBrowser()
{
    auto component = std::make_unique<StyleLabReferenceBrowserComponent>();
    component->onApplyReference = [this](const StyleLabReferenceRecord& record, juce::String& statusMessage)
    {
        if (!onApplyReferencePattern)
        {
            statusMessage = "Editor apply path is not connected.";
            return false;
        }

        StyleLabState nextState = state;
        if (!onApplyReferencePattern(record, nextState, statusMessage))
            return false;

        state = nextState;
        defaultState = nextState;
        syncControlsFromState();
        refreshLaneRows();
        updateStatusText(statusMessage, juce::Colour::fromRGB(152, 204, 168));
        return true;
    };

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Style Lab Reference Browser";
    options.content.setOwned(component.release());
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour::fromRGB(16, 18, 23);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    if (auto* window = options.launchAsync())
    {
        window->setResizeLimits(900, 560, 1660, 1080);
        window->setSize(1280, 760);
    }
}

void StyleLabComponent::addLane()
{
    StyleLabLaneDefinition lane;
    lane.id = makeLaneId();
    lane.laneName = "Custom Lane " + juce::String(static_cast<int>(state.laneDefinitions.size()) + 1);
    lane.groupName = "Custom";
    lane.generationPriority = 50;
    lane.isCore = false;
    lane.isRuntimeRegistryLane = false;
    state.laneDefinitions.push_back(std::move(lane));
    refreshLaneRows();
    emitStateChanged();
}

void StyleLabComponent::resetLaneLayout()
{
    if (onResetToRuntimeLayout)
    {
        if (const auto nextState = onResetToRuntimeLayout())
        {
            state = *nextState;
            defaultState = *nextState;
            syncControlsFromState();
            refreshLaneRows();
            emitStateChanged();
            updateStatusText("Lane layout reset from current runtime project.", juce::Colour::fromRGB(162, 174, 188));
            return;
        }
    }

    state.laneDefinitions = defaultState.laneDefinitions;
    refreshLaneRows();
    emitStateChanged();
    updateStatusText("Lane layout reset to runtime-linked defaults.", juce::Colour::fromRGB(162, 174, 188));
}

void StyleLabComponent::duplicateLane(const juce::String& laneId)
{
    for (size_t index = 0; index < state.laneDefinitions.size(); ++index)
    {
        if (state.laneDefinitions[index].id == laneId)
        {
            auto duplicate = state.laneDefinitions[index];
            duplicate.id = makeLaneId();
            duplicate.laneName += " Copy";
            duplicate.isRuntimeRegistryLane = false;
            duplicate.runtimeTrackType.reset();
            state.laneDefinitions.insert(state.laneDefinitions.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(duplicate));
            refreshLaneRows();
            emitStateChanged();
            return;
        }
    }
}

void StyleLabComponent::deleteLane(const juce::String& laneId)
{
    auto it = std::remove_if(state.laneDefinitions.begin(), state.laneDefinitions.end(), [&laneId](const auto& lane)
    {
        return lane.id == laneId;
    });

    if (it != state.laneDefinitions.end())
    {
        state.laneDefinitions.erase(it, state.laneDefinitions.end());
        refreshLaneRows();
        emitStateChanged();
    }
}

void StyleLabComponent::moveLane(const juce::String& laneId, int delta)
{
    for (size_t index = 0; index < state.laneDefinitions.size(); ++index)
    {
        if (state.laneDefinitions[index].id != laneId)
            continue;

        const auto newIndex = static_cast<int>(index) + delta;
        if (!juce::isPositiveAndBelow(newIndex, static_cast<int>(state.laneDefinitions.size())))
            return;

        std::iter_swap(state.laneDefinitions.begin() + static_cast<std::ptrdiff_t>(index),
                       state.laneDefinitions.begin() + static_cast<std::ptrdiff_t>(newIndex));
        refreshLaneRows();
        emitStateChanged();
        return;
    }
}

void StyleLabComponent::showLaneMenu(const juce::String& laneId, juce::Component* target)
{
    auto* lane = findLane(laneId);
    if (lane == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Duplicate Lane");
    menu.addItem(2, lane->isCore ? "Mark Optional" : "Mark Core");
    menu.addItem(3, "Delete Lane");
    menu.addSeparator();
    menu.addItem(4, "Move Up", true, false);
    menu.addItem(5, "Move Down", true, false);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target).withParentComponent(this),
                       [safe = juce::Component::SafePointer<StyleLabComponent>(this), laneId](int choice)
                       {
                           if (safe == nullptr)
                               return;

                           if (choice == 1)
                               safe->duplicateLane(laneId);
                           else if (choice == 2)
                           {
                               if (auto* lane = safe->findLane(laneId))
                               {
                                   lane->isCore = !lane->isCore;
                                   safe->refreshLaneRows();
                                   safe->emitStateChanged();
                               }
                           }
                           else if (choice == 3)
                               safe->deleteLane(laneId);
                           else if (choice == 4)
                               safe->moveLane(laneId, -1);
                           else if (choice == 5)
                               safe->moveLane(laneId, 1);
                       });
}

StyleLabLaneDefinition* StyleLabComponent::findLane(const juce::String& laneId)
{
    for (auto& lane : state.laneDefinitions)
    {
        if (lane.id == laneId)
            return &lane;
    }

    return nullptr;
}

const StyleLabLaneDefinition* StyleLabComponent::findLane(const juce::String& laneId) const
{
    for (const auto& lane : state.laneDefinitions)
    {
        if (lane.id == laneId)
            return &lane;
    }

    return nullptr;
}

juce::StringArray StyleLabComponent::moodOptions()
{
    return bbg::moodOptions();
}

juce::StringArray StyleLabComponent::densityProfileOptions()
{
    return bbg::densityProfileOptions();
}

juce::StringArray StyleLabComponent::parseTagsFromText(const juce::String& text)
{
    juce::StringArray tags;
    tags.addTokens(text, ",", {});
    for (int index = 0; index < tags.size(); ++index)
        tags.set(index, tags[index].trim());
    tags.removeEmptyStrings();
    return tags;
}

juce::String StyleLabComponent::joinTagsForText(const juce::StringArray& tags)
{
    return tags.joinIntoString(", ");
}
} // namespace bbg