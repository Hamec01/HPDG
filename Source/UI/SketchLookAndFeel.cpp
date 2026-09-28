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
    const float corner = juce::jlimit(3.0f, 9.0f, bounds.getHeight() * 0.28f);
    const bool active = button.getToggleState() || down;
    const int seed = button.getComponentID().hashCode() + button.getButtonText().hashCode();

    if (!down)
        sketch::dropShadow(g, bounds, corner, active ? 3.2f : 2.2f, highlighted ? 1.0f : 0.85f);

    auto baseColour = background.getAlpha() > 20 ? background : sketch::Theme::paperLight();
    juce::Colour top, bottom;
    if (active)
    {
        top = sketch::Theme::ochreGlow();
        bottom = sketch::Theme::ochreDeep();
    }
    else if (highlighted)
    {
        top = baseColour.brighter(0.18f).overlaidWith(sketch::Theme::ochreWash().withMultipliedAlpha(0.35f));
        bottom = baseColour.darker(0.06f);
    }
    else
    {
        top = baseColour.brighter(0.12f);
        bottom = baseColour.darker(0.08f);
    }

    auto fillBounds = down ? bounds.translated(0.0f, 0.6f) : bounds;
    g.setGradientFill(sketch::raisedPaperGradient(fillBounds, top, bottom));
    g.fillRoundedRectangle(fillBounds, corner);

    // Thin gloss highlight along the top edge to sell the raised-paper bevel.
    if (!down)
    {
        auto glossBounds = fillBounds.reduced(fillBounds.getWidth() * 0.08f, 1.4f).withHeight(fillBounds.getHeight() * 0.42f);
        g.setColour(sketch::Theme::highlight().withMultipliedAlpha(active ? 0.28f : 0.4f));
        g.fillRoundedRectangle(glossBounds, corner * 0.7f);
    }
    else
    {
        // Pressed: a soft inner shadow curling down from the top edge.
        auto inset = fillBounds.reduced(1.0f);
        g.setColour(sketch::Theme::shadowStrong().withMultipliedAlpha(0.35f));
        g.drawRoundedRectangle(inset, corner, 2.0f);
    }

    sketch::drawFrame(g, fillBounds, active ? sketch::Theme::ochreDeep().darker(0.2f) : sketch::Theme::graphiteSoft(),
                      down ? 1.7f : 1.15f, seed, corner);
}

void SketchLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    const auto background = button.findColour(juce::TextButton::buttonColourId);
    const bool active = button.getToggleState() || down;
    const auto textColour = active || background.getPerceivedBrightness() < 0.30f
                                ? sketch::Theme::paperLight()
                                : sketch::Theme::graphite();

    if (active)
    {
        g.setColour(sketch::Theme::ink().withAlpha(0.35f));
        g.setFont(getTextButtonFont(button, button.getHeight()));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(5).translated(0, down ? 2 : 1),
                         juce::Justification::centred, 1);
    }

    g.setColour(textColour);
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(5).translated(0, down ? 1 : 0),
                     juce::Justification::centred, 1);
}

void SketchLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    auto box = juce::Rectangle<float>(2.0f, (button.getHeight() - 15.0f) * 0.5f, 15.0f, 15.0f);
    sketch::dropShadow(g, box, 2.0f, 1.6f, highlighted ? 1.0f : 0.75f);
    g.setGradientFill(sketch::raisedPaperGradient(box, sketch::Theme::paperLight().brighter(0.1f), sketch::Theme::paperShadow()));
    g.fillRoundedRectangle(box, 2.0f);
    sketch::drawFrame(g, box, sketch::Theme::graphite(), 1.2f, button.getButtonText().hashCode(), 1.5f);
    if (button.getToggleState())
    {
        g.setColour(sketch::Theme::ochreDeep().withAlpha(0.9f));
        g.fillRoundedRectangle(box.reduced(2.6f), 1.0f);
        sketch::drawLine(g, box.getTopLeft() + juce::Point<float>(2.5f, 7.5f), box.getCentre() + juce::Point<float>(-1.0f, 4.0f), sketch::Theme::ink(), 1.8f, 4);
        sketch::drawLine(g, box.getCentre() + juce::Point<float>(-1.0f, 4.0f), box.getTopRight() + juce::Point<float>(-1.0f, 2.0f), sketch::Theme::ink(), 1.8f, 7);
    }
    g.setColour(sketch::Theme::graphite());
    g.setFont(sketch::notebookFont(14.0f));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(22), juce::Justification::centredLeft, 1);
}

void SketchLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool down,
                                      int buttonX, int, int buttonW, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width - 1), static_cast<float>(height - 1));
    const float corner = juce::jlimit(3.0f, 8.0f, bounds.getHeight() * 0.26f);
    const int seed = box.getComponentID().hashCode() + width;

    sketch::dropShadow(g, bounds, corner, 2.0f, down ? 0.7f : 0.9f);

    auto top = down ? sketch::Theme::ochreWash().overlaidWith(sketch::Theme::paperLight().withAlpha(0.5f))
                    : sketch::Theme::paperLight().brighter(0.08f);
    auto bottom = down ? sketch::Theme::paperShadow() : sketch::Theme::paper();
    g.setGradientFill(sketch::raisedPaperGradient(bounds, top, bottom));
    g.fillRoundedRectangle(bounds, corner);

    auto gloss = bounds.reduced(bounds.getWidth() * 0.06f, 1.2f).withHeight(bounds.getHeight() * 0.4f);
    g.setColour(sketch::Theme::highlight().withMultipliedAlpha(0.32f));
    g.fillRoundedRectangle(gloss, corner * 0.7f);

    sketch::drawFrame(g, bounds, sketch::Theme::graphiteSoft(), 1.0f, seed, corner);

    const float cx = buttonX + buttonW * 0.5f;
    const float cy = height * 0.52f;
    juce::Path arrow;
    arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
    arrow.lineTo(cx, cy + 2.0f);
    arrow.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(sketch::Theme::graphite());
    g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SketchLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(7, 1, box.getWidth() - 25, box.getHeight() - 2);
    label.setFont(sketch::notebookFont(14.0f));
}

void SketchLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                                          float startAngle, float endAngle, juce::Slider& slider)
{
    const auto radius = juce::jmin(width, height) * 0.5f - 2.0f;
    const auto centre = juce::Point<float>(x + width * 0.5f, y + height * 0.5f);
    const int seed = x * 31 + y;

    sketch::dropShadow(g, juce::Rectangle<float>(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f),
                       radius, 2.4f, 0.9f);

    juce::ColourGradient dial(sketch::Theme::paperLight().brighter(0.1f), centre.x - radius * 0.5f, centre.y - radius * 0.6f,
                              sketch::Theme::paperDeep(), centre.x + radius * 0.6f, centre.y + radius * 0.7f, false);
    g.setGradientFill(dial);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(sketch::Theme::graphite());
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);

    // Track + fill arc so the knob reads its value at a glance, like a real dial.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius * 0.86f, radius * 0.86f, 0.0f, startAngle, endAngle, true);
    g.setColour(sketch::Theme::graphiteSoft().withAlpha(0.3f));
    g.strokePath(track, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float angle = startAngle + position * (endAngle - startAngle);
    juce::Path fillArc;
    fillArc.addCentredArc(centre.x, centre.y, radius * 0.86f, radius * 0.86f, 0.0f, startAngle, angle, true);
    auto fillColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
    g.setColour(fillColour.isTransparent() ? sketch::Theme::ochreDeep() : fillColour);
    g.strokePath(fillArc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Small tick marks at the travel extremes and centre.
    for (float t : { 0.0f, 0.5f, 1.0f })
    {
        const float tickAngle = startAngle + t * (endAngle - startAngle);
        const auto dir = juce::Point<float>(std::sin(tickAngle), -std::cos(tickAngle));
        const auto inner = centre + dir * (radius * 0.92f);
        const auto outer = centre + dir * (radius * 1.06f);
        g.setColour(sketch::Theme::graphiteSoft().withAlpha(0.55f));
        g.drawLine({ inner.x, inner.y, outer.x, outer.y }, 1.1f);
    }

    const auto pointerEnd = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * (radius * 0.7f);
    sketch::drawLine(g, centre, pointerEnd, sketch::Theme::ink(), 1.9f, seed);
    g.setColour(sketch::Theme::highlight().withMultipliedAlpha(0.55f));
    g.fillEllipse(centre.x - radius * 0.42f, centre.y - radius * 0.52f, radius * 0.34f, radius * 0.24f);
    g.setColour(sketch::Theme::ink());
    g.fillEllipse(centre.x - 1.8f, centre.y - 1.8f, 3.6f, 3.6f);
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

    // Recessed track groove.
    g.setColour(sketch::Theme::shadowSoft().withMultipliedAlpha(0.7f));
    g.drawLine({ left, cy + 1.0f, right, cy + 1.0f }, 2.6f);
    sketch::drawLine(g, { left, cy }, { right, cy }, sketch::Theme::graphiteSoft().withAlpha(0.4f), 2.0f, x + y + width);

    auto active = slider.findColour(juce::Slider::trackColourId);
    if (active.isTransparent())
        active = sketch::Theme::blue();
    juce::ColourGradient fillGradient(active.brighter(0.25f), left, cy, active.darker(0.12f), sliderPos, cy, false);
    g.setGradientFill(fillGradient);
    g.drawLine({ left, cy, sliderPos, cy }, 3.0f);

    const auto thumbBounds = juce::Rectangle<float>(sliderPos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
    sketch::dropShadow(g, thumbBounds, 6.0f, 2.0f, 0.85f);
    juce::ColourGradient thumbGradient(sketch::Theme::paperLight().brighter(0.12f), sliderPos - 3.0f, cy - 4.0f,
                                       sketch::Theme::paperDeep(), sliderPos + 3.0f, cy + 4.0f, false);
    g.setGradientFill(thumbGradient);
    g.fillEllipse(thumbBounds);
    g.setColour(sketch::Theme::graphite());
    g.drawEllipse(thumbBounds, 1.15f);
    g.setColour(sketch::Theme::highlight().withMultipliedAlpha(0.6f));
    g.fillEllipse(sliderPos - 2.6f, cy - 4.2f, 2.6f, 1.8f);
}
}
