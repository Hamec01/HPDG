#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "UI/HardwareKnob.h"

namespace bbg::b808
{
// HPDG's Dark skin (Source/UI/SketchDrawing.cpp, darkPalette): matte charcoal, bone text, a dried-blood
// accent, a cold grey-teal second colour. The knobs are HPDG's own (UI/HardwareKnob.h).
namespace colour
{
inline const juce::Colour background { 20, 19, 20 };
inline const juce::Colour panel { 32, 30, 32 };
inline const juce::Colour panelEdge { 48, 45, 47 };
inline const juce::Colour deep { 10, 9, 10 };
inline const juce::Colour text { 196, 188, 176 };
inline const juce::Colour textBright { 226, 219, 207 };
inline const juce::Colour textMuted { 118, 111, 104 };
inline const juce::Colour accent { 150, 44, 38 };
inline const juce::Colour accentGlow { 196, 72, 60 };
inline const juce::Colour teal { 84, 104, 106 };
inline const juce::Colour tealBright { 128, 160, 162 };
} // namespace colour

class Look final : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        using namespace colour;
        setColour(juce::ResizableWindow::backgroundColourId, background);
        setColour(juce::Label::textColourId, text);
        setColour(juce::ComboBox::backgroundColourId, deep);
        setColour(juce::ComboBox::textColourId, textBright);
        setColour(juce::ComboBox::outlineColourId, panelEdge);
        setColour(juce::ComboBox::arrowColourId, accentGlow);
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::headerTextColourId, accentGlow);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.55f));
        setColour(juce::PopupMenu::highlightedTextColourId, textBright);
        setColour(juce::TextButton::buttonColourId, deep);
        setColour(juce::TextButton::buttonOnColourId, accent);
        setColour(juce::TextButton::textColourOffId, text);
        setColour(juce::TextButton::textColourOnId, textBright);
        setColour(juce::TextEditor::backgroundColourId, deep);
        setColour(juce::TextEditor::textColourId, textBright);
        setColour(juce::CaretComponent::caretColourId, accentGlow);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = button.getToggleState();
        g.setColour(on ? colour::accent : colour::deep);
        g.fillRoundedRectangle(r, 4.0f);
        if (over || down)
        {
            g.setColour(juce::Colours::white.withAlpha(down ? 0.10f : 0.05f));
            g.fillRoundedRectangle(r, 4.0f);
        }
        g.setColour(on ? colour::accentGlow : colour::panelEdge);
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override { return juce::Font(juce::FontOptions(12.0f, juce::Font::bold)); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::Font(juce::FontOptions(13.0f)); }
    juce::Font getPopupMenuFont() override { return juce::Font(juce::FontOptions(14.0f)); }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
        g.setColour(colour::deep);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(box.hasKeyboardFocus(true) ? colour::accent : colour::panelEdge);
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
        juce::Path arrow;
        const float cx = static_cast<float>(width) - 12.0f, cy = static_cast<float>(height) * 0.5f;
        arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour(colour::accentGlow);
        g.fillPath(arrow);
    }
};

// Number shown under a knob: as many decimals as the size of the value needs.
inline juce::String formatValue(float value, const juce::String& unit)
{
    const float magnitude = std::abs(value);
    const int decimals = magnitude >= 100.0f ? 0 : (magnitude >= 10.0f ? 1 : 2);
    return juce::String(value, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
}

// A knob bound to a parameter: caption above, value below (double-click types a value, as in HPDG).
class ParamKnob final : public juce::Component
{
public:
    ParamKnob(juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text, int knobSize = 52)
        : size(knobSize)
    {
        caption.setText(text, juce::dontSendNotification);
        caption.setJustificationType(juce::Justification::centred);
        caption.setFont(juce::FontOptions(knobSize < 44 ? 11.0f : 12.5f));
        caption.setColour(juce::Label::textColourId, colour::text);
        value.setJustificationType(juce::Justification::centred);
        value.setFont(juce::FontOptions(knobSize < 44 ? 10.5f : 11.5f));
        value.setColour(juce::Label::textColourId, colour::tealBright);
        slider.setPopupTitle(text);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, id, slider);
        if (auto* parameter = state.getParameter(id))
        {
            slider.onValueChange = [this, parameter] { value.setText(parameter->getCurrentValueAsText(), juce::dontSendNotification); };
            value.setText(parameter->getCurrentValueAsText(), juce::dontSendNotification);
        }
        addAndMakeVisible(caption);
        addAndMakeVisible(slider);
        addAndMakeVisible(value);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int captionHeight = size < 44 ? 13 : 15;
        caption.setBounds(r.removeFromTop(captionHeight));
        value.setBounds(r.removeFromBottom(captionHeight));
        slider.setBounds(r.withSizeKeepingCentre(size, size));
    }

private:
    int size;
    juce::Label caption, value;
    RotaryKnobSlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class ParamCombo final : public juce::Component
{
public:
    ParamCombo(juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text)
    {
        caption.setText(text, juce::dontSendNotification);
        caption.setJustificationType(juce::Justification::centred);
        caption.setFont(juce::FontOptions(12.5f));
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(id)))
            box.addItemList(choice->choices, 1);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, id, box);
        addAndMakeVisible(caption);
        addAndMakeVisible(box);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        caption.setBounds(r.removeFromTop(15));
        box.setBounds(r.withSizeKeepingCentre(juce::jmin(r.getWidth() - 4, 92), 26));
    }

    juce::ComboBox box;

private:
    juce::Label caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// A panel of controls: knobs, menus, switches (bound to parameters) and action buttons, in a grid.
class Page final : public juce::Component
{
public:
    struct Item
    {
        enum Kind { Knob, Combo, Toggle, Action } kind;
        const char* id;   // parameter id (Action: unused)
        const char* text;
        std::function<void()> action = {};
    };
    struct Group
    {
        const char* title;
        int rows;
        std::vector<Item> items;
    };

    static constexpr int kCell = 80, kRow = 100, kTitle = 24;

    Page(juce::AudioProcessorValueTreeState& state, std::vector<Group> newGroups) : groups(std::move(newGroups))
    {
        for (auto& group : groups)
            for (auto& item : group.items)
            {
                std::unique_ptr<juce::Component> component;
                switch (item.kind)
                {
                    case Item::Knob: component = std::make_unique<ParamKnob>(state, item.id, item.text); break;
                    case Item::Combo: component = std::make_unique<ParamCombo>(state, item.id, item.text); break;
                    case Item::Toggle:
                    {
                        auto button = std::make_unique<juce::TextButton>(item.text);
                        button->setClickingTogglesState(true);
                        toggles.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, item.id, *button));
                        component = std::move(button);
                        break;
                    }
                    case Item::Action:
                    {
                        auto button = std::make_unique<juce::TextButton>(item.text);
                        button->onClick = item.action;
                        component = std::move(button);
                        break;
                    }
                }
                addAndMakeVisible(*component);
                components.push_back(std::move(component));
            }
    }

    void paint(juce::Graphics& g) override
    {
        for (const auto& [title, area] : panels)
        {
            g.setColour(colour::panel);
            g.fillRoundedRectangle(area, 7.0f);
            g.setColour(colour::panelEdge);
            g.drawRoundedRectangle(area.reduced(0.5f), 7.0f, 1.0f);
            g.setColour(colour::accentGlow);
            g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
            g.drawText(title, area.reduced(12.0f, 0.0f).withHeight(static_cast<float>(kTitle)), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        panels.clear();
        int x = 0;
        size_t index = 0;
        for (const auto& group : groups)
        {
            const int count = static_cast<int>(group.items.size());
            const int perRow = (count + group.rows - 1) / group.rows;
            const int width = perRow * kCell + 16;
            panels.push_back({ group.title, juce::Rectangle<float>(static_cast<float>(x), 0.0f, static_cast<float>(width),
                                                                   static_cast<float>(kTitle + group.rows * kRow + 8)) });
            for (int i = 0; i < count; ++i, ++index)
            {
                const auto& item = group.items[static_cast<size_t>(i)];
                const juce::Rectangle<int> cell(x + 8 + (i % perRow) * kCell, kTitle + (i / perRow) * kRow, kCell, kRow);
                auto& component = *components[index];
                if (item.kind == Item::Toggle || item.kind == Item::Action)
                    component.setBounds(cell.withSizeKeepingCentre(kCell - 10, 26));
                else
                    component.setBounds(cell);
            }
            x += width + 10;
        }
    }

private:
    std::vector<Group> groups;
    std::vector<std::unique_ptr<juce::Component>> components;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggles;
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> panels;
};
} // namespace bbg::b808
