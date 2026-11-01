#include "Bass808Editor.h"

namespace bbg
{
namespace
{
const juce::Colour kBackground { 0xff15161a };
const juce::Colour kPanel { 0xff1f2127 };
const juce::Colour kAccent { 0xffe0a040 };
const juce::Colour kText { 0xffd8d8dc };

// Groups of knobs: title + how many of the knobs (in addKnob order) it holds.
struct Group
{
    const char* title;
    int count;
};
constexpr Group kGroups[] { { "PITCH", 3 }, { "TONE", 4 }, { "SHAPE", 6 }, { "PLAY", 3 } };
} // namespace

Bass808Editor::Bass808Editor(Bass808Processor& processor)
    : juce::AudioProcessorEditor(processor),
      bass(processor),
      keyboard(processor.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    look.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    look.setColour(juce::Slider::rotarySliderOutlineColourId, kPanel.brighter(0.25f));
    look.setColour(juce::Slider::thumbColourId, kAccent.brighter(0.3f));
    look.setColour(juce::Slider::textBoxTextColourId, kText);
    look.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    look.setColour(juce::Label::textColourId, kText);
    look.setColour(juce::ComboBox::backgroundColourId, kPanel.brighter(0.1f));
    look.setColour(juce::ComboBox::textColourId, kText);
    look.setColour(juce::ComboBox::outlineColourId, kAccent.withAlpha(0.5f));
    look.setColour(juce::PopupMenu::backgroundColourId, kPanel);
    look.setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.4f));
    setLookAndFeel(&look);

    for (int i = 0; i < Synth808Voice::kPresetCount; ++i)
        presetBox.addItem(Synth808Voice::presets()[static_cast<size_t>(i)].name, i + 1);
    presetBox.setSelectedId(bass.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { bass.applyPreset(presetBox.getSelectedId() - 1); };
    addAndMakeVisible(presetBox);

    modeBox.addItemList({ "Retrigger", "Legato", "Glide" }, 1);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(bass.apvts, "mode", modeBox);
    addAndMakeVisible(modeBox);

    addKnob("punch", "Punch");
    addKnob("punchTime", "Punch Time");
    addKnob("tune", "Tune");
    addKnob("drive", "Drive");
    addKnob("even", "Even");
    addKnob("saturation", "Saturation");
    addKnob("tone", "Tone");
    addKnob("attack", "Attack");
    addKnob("boost", "Boost");
    addKnob("hold", "Hold");
    addKnob("decay", "Decay");
    addKnob("release", "Release");
    addKnob("click", "Click");
    addKnob("glide", "Glide");
    addKnob("velocity", "Velocity");
    addKnob("gain", "Gain");

    keyboard.setAvailableRange(24, 60); // C1-C4: where an 808 lives
    keyboard.setLowestVisibleKey(24);
    addAndMakeVisible(keyboard);

    setSize(840, 400);
}

Bass808Editor::~Bass808Editor()
{
    setLookAndFeel(nullptr);
}

void Bass808Editor::addKnob(const char* id, const char* text)
{
    auto knob = std::make_unique<Knob>();
    knob->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    knob->label.setText(text, juce::dontSendNotification);
    knob->label.setJustificationType(juce::Justification::centred);
    knob->label.setFont(juce::FontOptions(13.0f));
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(bass.apvts, id, knob->slider);
    addAndMakeVisible(knob->slider);
    addAndMakeVisible(knob->label);
    knobs.push_back(std::move(knob));
}

void Bass808Editor::paint(juce::Graphics& g)
{
    g.fillAll(kBackground);
    g.setColour(kAccent);
    g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    g.drawText("HPDG 808", 16, 10, 200, 30, juce::Justification::centredLeft);
    g.setColour(kText.withAlpha(0.6f));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("Preset", 250, 10, 60, 30, juce::Justification::centredRight);
    g.drawText("Mode", 540, 10, 60, 30, juce::Justification::centredRight);

    // group panels and titles
    const int top = 52;
    const int knobWidth = 84;
    int x = 12;
    for (const auto& group : kGroups)
    {
        const int width = (group.count + 1) / 2 * knobWidth + 8;
        g.setColour(kPanel);
        g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(top), static_cast<float>(width), 214.0f, 8.0f);
        g.setColour(kAccent.withAlpha(0.8f));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(group.title, x + 10, top + 4, 120, 16, juce::Justification::centredLeft);
        x += width + 8;
    }
}

void Bass808Editor::resized()
{
    presetBox.setBounds(316, 13, 200, 26);
    modeBox.setBounds(606, 13, 140, 26);

    // Knobs: PITCH, TONE, SHAPE, PLAY side by side, two rows of knobs inside every panel.
    const int top = 52;
    const int knobWidth = 84;
    int x = 12;
    size_t index = 0;
    for (const auto& group : kGroups)
    {
        const int perRow = (group.count + 1) / 2;
        const int width = perRow * knobWidth + 8;
        const int cell = knobWidth;
        for (int i = 0; i < group.count && index < knobs.size(); ++i, ++index)
        {
            const int row = i / perRow;
            const int column = i % perRow;
            auto& knob = *knobs[index];
            const int kx = x + 4 + column * cell;
            const int ky = top + 22 + row * 96;
            knob.label.setBounds(kx, ky, cell, 16);
            knob.slider.setBounds(kx + (cell - 76) / 2, ky + 16, 76, 76);
        }
        x += width + 8;
    }
    keyboard.setBounds(12, 278, getWidth() - 24, getHeight() - 290);
}
} // namespace bbg
