#include "SketchLookAndFeel.h"
#include "SketchDrawing.h"
#include "SketchFonts.h"
#include "SketchTheme.h"

namespace bbg
{
SketchLookAndFeel::SketchLookAndFeel()
{
    setDefaultSansSerifTypeface(sketch::notebookTypeface());
    setColour(juce::Label::textColourId, sketch::Theme::graphite());
    setColour(juce::TextButton::textColourOffId, sketch::Theme::graphite());
    setColour(juce::TextButton::textColourOnId, sketch::Theme::graphite());
    setColour(juce::ComboBox::textColourId, sketch::Theme::graphite());
    setColour(juce::ComboBox::backgroundColourId, sketch::Theme::paperLight());
    setColour(juce::ComboBox::outlineColourId, sketch::Theme::graphiteSoft());
    setColour(juce::ComboBox::arrowColourId, sketch::Theme::graphite());
    setColour(juce::PopupMenu::backgroundColourId, sketch::Theme::paperLight());
    setColour(juce::PopupMenu::textColourId, sketch::Theme::graphite());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, sketch::Theme::ochreWash());
    setColour(juce::PopupMenu::highlightedTextColourId, sketch::Theme::graphite());
    setColour(juce::ScrollBar::thumbColourId, sketch::Theme::graphiteSoft().withAlpha(0.72f));
    setColour(juce::Slider::textBoxTextColourId, sketch::Theme::graphite());
    setColour(juce::Slider::textBoxBackgroundColourId, sketch::Theme::paperLight());
    setColour(juce::Slider::textBoxOutlineColourId, sketch::Theme::graphiteSoft());
}

juce::Font SketchLookAndFeel::getLabelFont(juce::Label& label)
{
    return sketch::notebookFont(label.getFont().getHeight(), label.getFont().isBold());
}

juce::Font SketchLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return sketch::notebookFont(juce::jlimit(12.0f, 16.0f, box.getHeight() * 0.58f));
}

juce::Font SketchLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return sketch::notebookFont(juce::jlimit(12.0f, 18.0f, buttonHeight * 0.52f));
}

juce::Font SketchLookAndFeel::getPopupMenuFont()
{
    return sketch::notebookFont(15.0f);
}

juce::Label* SketchLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setColour(juce::Label::textColourId, sketch::Theme::graphite());
    label->setColour(juce::Label::textWhenEditingColourId, sketch::Theme::graphite());
    label->setColour(juce::Label::backgroundColourId, sketch::Theme::paperLight());
    label->setColour(juce::Label::outlineColourId, sketch::Theme::graphiteSoft());
    label->setColour(juce::TextEditor::textColourId, sketch::Theme::graphite());
    label->setColour(juce::TextEditor::backgroundColourId, sketch::Theme::paperLight());
    label->setColour(juce::TextEditor::outlineColourId, sketch::Theme::graphiteSoft());
    label->setFont(sketch::notebookFont(13.0f));
    return label;
}

void SketchLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                              bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.5f);
    auto fill = background.getAlpha() > 20 ? background : sketch::Theme::paperLight();
    if (button.getToggleState() || down)
        fill = sketch::Theme::ochre();
    else if (highlighted)
        fill = sketch::Theme::ochreWash().overlaidWith(sketch::Theme::paperLight().withAlpha(0.45f));
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 2.5f);
    sketch::drawFrame(g, bounds, sketch::Theme::graphiteSoft(), down ? 1.7f : 1.15f,
                      button.getComponentID().hashCode() + button.getButtonText().hashCode());
}

void SketchLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    const auto background = button.findColour(juce::TextButton::buttonColourId);
    g.setColour(background.getPerceivedBrightness() < 0.30f
                    ? sketch::Theme::paperLight()
                    : sketch::Theme::graphite());
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(5).translated(0, down ? 1 : 0),
                     juce::Justification::centred, 1);
}

void SketchLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    auto box = juce::Rectangle<float>(2.0f, (button.getHeight() - 15.0f) * 0.5f, 15.0f, 15.0f);
    g.setColour(sketch::Theme::paperLight());
    g.fillRect(box);
    sketch::drawFrame(g, box, sketch::Theme::graphite(), 1.2f, button.getButtonText().hashCode(), 1.0f);
    if (button.getToggleState())
    {
        sketch::drawLine(g, box.getTopLeft() + juce::Point<float>(2.5f, 7.5f), box.getCentre() + juce::Point<float>(-1.0f, 4.0f), sketch::Theme::graphite(), 1.8f, 4);
        sketch::drawLine(g, box.getCentre() + juce::Point<float>(-1.0f, 4.0f), box.getTopRight() + juce::Point<float>(-1.0f, 2.0f), sketch::Theme::graphite(), 1.8f, 7);
    }
    g.setColour(sketch::Theme::graphite());
    g.setFont(sketch::notebookFont(14.0f));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(22), juce::Justification::centredLeft, 1);
}

void SketchLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool down,
                                      int buttonX, int, int buttonW, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width - 1), static_cast<float>(height - 1));
    g.setColour(down ? sketch::Theme::ochreWash() : sketch::Theme::paperLight());
    g.fillRoundedRectangle(bounds, 2.0f);
    sketch::drawFrame(g, bounds, sketch::Theme::graphiteSoft(), 1.0f, box.getComponentID().hashCode() + width, 2.0f);
    const float cx = buttonX + buttonW * 0.5f;
    const float cy = height * 0.52f;
    juce::Path arrow;
    arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
    arrow.lineTo(cx, cy + 2.0f);
    arrow.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(sketch::Theme::graphite());
    g.strokePath(arrow, juce::PathStrokeType(1.5f));
}

void SketchLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(7, 1, box.getWidth() - 25, box.getHeight() - 2);
    label.setFont(sketch::notebookFont(14.0f));
}

void SketchLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                                          float startAngle, float endAngle, juce::Slider&)
{
    const auto radius = juce::jmin(width, height) * 0.5f - 2.0f;
    const auto centre = juce::Point<float>(x + width * 0.5f, y + height * 0.5f);
    g.setColour(sketch::Theme::paperShadow());
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(sketch::Theme::graphite());
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);
    const float angle = startAngle + position * (endAngle - startAngle);
    sketch::drawLine(g, centre, centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * (radius * 0.78f),
                     sketch::Theme::graphite(), 1.8f, x * 31 + y);
}

void SketchLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    const float cy = y + height * 0.5f;
    const float left = static_cast<float>(x + 4);
    const float right = static_cast<float>(x + width - 4);
    sketch::drawLine(g, { left, cy }, { right, cy }, sketch::Theme::graphiteSoft().withAlpha(0.42f), 2.0f, x + y + width);
    auto active = slider.findColour(juce::Slider::trackColourId);
    if (active.isTransparent())
        active = sketch::Theme::blue();
    sketch::drawLine(g, { left, cy }, { sliderPos, cy }, active, 3.0f, x + y + 19);
    g.setColour(sketch::Theme::paperShadow());
    g.fillEllipse(sliderPos - 5.5f, cy - 5.5f, 11.0f, 11.0f);
    g.setColour(sketch::Theme::graphite());
    g.drawEllipse(sliderPos - 5.5f, cy - 5.5f, 11.0f, 11.0f, 1.15f);
}
}
