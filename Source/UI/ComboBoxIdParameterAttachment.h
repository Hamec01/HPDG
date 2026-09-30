#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace bbg
{
// Binds a ComboBox to a choice parameter by item ID (ID = choice index + 1), not by position.
// juce::ComboBoxParameterAttachment maps the selected *position* across the whole parameter
// range, which breaks as soon as some choices are hidden from the list: with only
// "Boom Bap" and "Trap" shown, picking Trap (last position) selected the last choice, Drill.
class ComboBoxIdParameterAttachment final : private juce::ComboBox::Listener
{
public:
    ComboBoxIdParameterAttachment(juce::RangedAudioParameter& parameter,
                                  juce::ComboBox& combo,
                                  juce::UndoManager* undoManager = nullptr)
        : comboBox(combo),
          attachment(parameter, [this](float value) { setValue(value); }, undoManager)
    {
        attachment.sendInitialUpdate();
        comboBox.addListener(this);
    }

    ~ComboBoxIdParameterAttachment() override { comboBox.removeListener(this); }

private:
    void setValue(float choiceIndex)
    {
        const int id = juce::roundToInt(choiceIndex) + 1;
        if (id == comboBox.getSelectedId() || comboBox.indexOfItemId(id) < 0)
            return; // unchanged, or a choice that is not offered in this list

        const juce::ScopedValueSetter<bool> guard(ignoreCallbacks, true);
        comboBox.setSelectedId(id, juce::sendNotificationSync);
    }

    void comboBoxChanged(juce::ComboBox*) override
    {
        if (ignoreCallbacks)
            return;

        const int id = comboBox.getSelectedId();
        if (id > 0)
            attachment.setValueAsCompleteGesture(static_cast<float>(id - 1));
    }

    juce::ComboBox& comboBox;
    juce::ParameterAttachment attachment;
    bool ignoreCallbacks = false;
};
} // namespace bbg
