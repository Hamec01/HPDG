#include "SketchLookAndFeel.h"
#include "SketchDrawing.h"
#include "SketchFonts.h"
#include "SketchTheme.h"

namespace bbg
{
namespace
{
// Steel / Dark: a button colour set for the paper skin (light paper tints, pastel washes) becomes
// a dark tone of the same hue, so it reads as part of the plate instead of a light sticker.
juce::Colour skinnedBase(juce::Colour colour)
{
    if (sketch::isHandDrawn() || colour.getPerceivedBrightness() <= 0.55f)
        return colour;
    const bool steel = sketch::activeSkin() == sketch::Skin::Steel;
    return colour.withBrightness(steel ? 0.33f : 0.21f).withMultipliedSaturation(steel ? 0.75f : 0.6f);
}

// Steel / Dark: a saturated colour picked for the paper skin (orange volume, sky-blue pan, the
// blue / copper drag buttons) takes the skin's own accent of the same temperature: warm -> the
// skin accent (amber / ember red), cool -> its second colour (steel blue / cold grey-teal).
juce::Colour paletteTone(juce::Colour colour)
{
    return sketch::toneForSkin(colour);
}

juce::Colour textOn(juce::Colour fill)
{
    return fill.getPerceivedBrightness() > 0.55f ? juce::Colour::fromRGB(18, 20, 24) : sketch::Theme::ink();
}

// The fill a Steel / Dark button is drawn with (used for its text colour too).
juce::Colour machinedFill(juce::Button& button, const juce::Colour& background, bool highlighted, bool down)
{
    const bool active = button.getToggleState() || down;
    if (active)
        return sketch::Theme::ochre();
    auto base = skinnedBase(paletteTone(background.getAlpha() > 20 ? background : sketch::Theme::paperLight()));
    return highlighted ? base.brighter(0.12f) : base;
}

void drawMachinedButton(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour fill, bool active, bool down)
{
    const bool steel = sketch::activeSkin() == sketch::Skin::Steel;
    const float corner = steel ? 2.5f : 3.0f;
    if (steel)
    {
        // cut into the plate: a dark slot around the cap
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(bounds.expanded(1.0f), corner + 1.0f);
        juce::ColourGradient cap(fill.brighter(down ? 0.0f : 0.16f), 0.0f, bounds.getY(), fill.darker(down ? 0.1f : 0.32f), 0.0f, bounds.getBottom(), false);
        g.setGradientFill(cap);
        g.fillRoundedRectangle(bounds, corner);
        if (active)
        {
            // backlit amber: a glow from the centre
            juce::ColourGradient glow(sketch::Theme::ochreGlow().withAlpha(0.55f), bounds.getCentreX(), bounds.getCentreY(),
                                      juce::Colours::transparentBlack, bounds.getRight(), bounds.getBottom(), true);
            g.setGradientFill(glow);
            g.fillRoundedRectangle(bounds, corner);
        }
        g.setColour(juce::Colour::fromRGB(200, 218, 238).withAlpha(down ? 0.06f : 0.26f));
        g.drawLine(bounds.getX() + corner, bounds.getY() + 0.6f, bounds.getRight() - corner, bounds.getY() + 0.6f, 1.0f);
        g.setColour(juce::Colours::black.withAlpha(down ? 0.2f : 0.5f));
        g.drawLine(bounds.getX() + corner, bounds.getBottom() - 0.6f, bounds.getRight() - corner, bounds.getBottom() - 0.6f, 1.0f);
        return;
    }
    // Dark: matte, flat, a hairline edge; active = a dull ember
    g.setColour(fill.darker(down ? 0.25f : 0.0f));
    g.fillRoundedRectangle(bounds, corner);
    if (active)
    {
        juce::ColourGradient ember(sketch::Theme::ochreGlow().withAlpha(0.35f), bounds.getCentreX(), bounds.getBottom(),
                                   juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getY(), false);
        g.setGradientFill(ember);
        g.fillRoundedRectangle(bounds, corner);
    }
    g.setColour((active ? sketch::Theme::ochreGlow() : sketch::Theme::graphiteSoft()).withAlpha(active ? 0.6f : 0.35f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 0.8f);
}
}

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
    if (!sketch::isHandDrawn())
    {
        drawMachinedButton(g, bounds, machinedFill(button, background, highlighted, down), button.getToggleState() || down, down);
        return;
    }
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

void SketchLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool highlighted, bool down)
{
    const auto background = button.findColour(juce::TextButton::buttonColourId);
    if (!sketch::isHandDrawn())
    {
        const auto fill = machinedFill(button, background, highlighted, down);
        const auto area = button.getLocalBounds().reduced(5).translated(0, down ? 1 : 0);
        g.setFont(getTextButtonFont(button, button.getHeight()));
        const auto text = button.getButtonText();
        if (sketch::activeSkin() == sketch::Skin::Steel)
        {
            // engraved: a dark cut just above the letters
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.drawFittedText(text, area.translated(0, -1), juce::Justification::centred, 1);
        }
        g.setColour(textOn(fill).withAlpha(button.isEnabled() ? 1.0f : 0.4f));
        g.drawFittedText(text, area, juce::Justification::centred, 1);
        return;
    }
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
    if (!sketch::isHandDrawn())
    {
        // a lamp in a dark socket: lit amber (Steel) / ember red (Dark) when on
        auto lamp = juce::Rectangle<float>(3.0f, (button.getHeight() - 12.0f) * 0.5f, 12.0f, 12.0f);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillEllipse(lamp.expanded(1.5f));
        if (button.getToggleState())
        {
            juce::ColourGradient lit(sketch::Theme::ochreGlow(), lamp.getCentreX() - 2.0f, lamp.getCentreY() - 2.0f,
                                     sketch::Theme::ochreDeep(), lamp.getRight(), lamp.getBottom(), true);
            g.setGradientFill(lit);
            g.fillEllipse(lamp);
            g.setColour(sketch::Theme::ochreGlow().withAlpha(0.25f));
            g.fillEllipse(lamp.expanded(3.0f));
        }
        else
        {
            g.setColour(sketch::Theme::paperDeep().brighter(highlighted ? 0.25f : 0.1f));
            g.fillEllipse(lamp);
        }
        g.setColour(sketch::Theme::graphite());
        g.setFont(sketch::notebookFont(14.0f));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(20), juce::Justification::centredLeft, 1);
        return;
    }
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
    if (!sketch::isHandDrawn())
    {
        // a recessed slot with a readout
        const float slotCorner = sketch::activeSkin() == sketch::Skin::Steel ? 2.0f : 3.0f;
        g.setColour(sketch::Theme::paperDeep().darker(down ? 0.0f : 0.2f));
        g.fillRoundedRectangle(bounds, slotCorner);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawLine(bounds.getX() + 1.0f, bounds.getY() + 0.8f, bounds.getRight() - 1.0f, bounds.getY() + 0.8f, 1.2f);
        g.setColour(sketch::Theme::highlight());
        g.drawLine(bounds.getX() + 1.0f, bounds.getBottom() - 0.5f, bounds.getRight() - 1.0f, bounds.getBottom() - 0.5f, 0.8f);
        g.setColour(sketch::Theme::graphiteSoft().withAlpha(0.45f));
        g.drawRoundedRectangle(bounds, slotCorner, 0.8f);
        const float ax = buttonX + buttonW * 0.5f, ay = height * 0.5f;
        juce::Path tri;
        tri.addTriangle(ax - 3.5f, ay - 1.8f, ax + 3.5f, ay - 1.8f, ax, ay + 2.6f);
        g.setColour(sketch::Theme::ochre());
        g.fillPath(tri);
        return;
    }
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
    if (!sketch::isHandDrawn())
    {
        const bool steel = sketch::activeSkin() == sketch::Skin::Steel;
        const float angle = startAngle + position * (endAngle - startAngle);
        juce::Path track, value;
        track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.strokePath(track, juce::PathStrokeType(2.2f));
        auto fillColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
        g.setColour(fillColour.isTransparent() ? sketch::Theme::ochre() : paletteTone(fillColour));
        g.strokePath(value, juce::PathStrokeType(2.0f));

        const float capR = radius * 0.78f;
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillEllipse(centre.x - capR - 1.0f, centre.y - capR + 0.5f, (capR + 1.0f) * 2.0f, (capR + 1.0f) * 2.0f);
        if (steel)
        {
            // machined cap with a knurled rim
            juce::ColourGradient cap(juce::Colour::fromRGB(150, 166, 186), centre.x - capR * 0.6f, centre.y - capR * 0.7f,
                                     juce::Colour::fromRGB(40, 50, 64), centre.x + capR * 0.6f, centre.y + capR * 0.8f, false);
            g.setGradientFill(cap);
            g.fillEllipse(centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
            for (int k = 0; k < 28; ++k)
            {
                const float a = k * juce::MathConstants<float>::twoPi / 28.0f;
                const juce::Point<float> d(std::sin(a), -std::cos(a));
                g.setColour(juce::Colours::black.withAlpha(0.35f));
                g.drawLine({ centre + d * (capR * 0.82f), centre + d * capR }, 0.7f);
            }
            juce::ColourGradient face(juce::Colour::fromRGB(120, 136, 156), centre.x, centre.y - capR * 0.6f,
                                      juce::Colour::fromRGB(58, 70, 86), centre.x, centre.y + capR * 0.6f, false);
            g.setGradientFill(face);
            g.fillEllipse(centre.x - capR * 0.72f, centre.y - capR * 0.72f, capR * 1.44f, capR * 1.44f);
        }
        else
        {
            g.setColour(juce::Colour::fromRGB(26, 25, 26));
            g.fillEllipse(centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
            g.setColour(sketch::Theme::graphiteSoft().withAlpha(0.35f));
            g.drawEllipse(centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f, 0.8f);
        }
        const juce::Point<float> dir(std::sin(angle), -std::cos(angle));
        g.setColour(steel ? sketch::Theme::ochreGlow() : sketch::Theme::graphite());
        g.drawLine({ centre + dir * (capR * 0.2f), centre + dir * (capR * 0.9f) }, 1.8f);
        return;
    }

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
    if (!sketch::isHandDrawn())
    {
        // a slot with a lit section and a square metal (Steel) / matte (Dark) cap
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(left, cy - 2.0f, right - left, 4.0f, 1.5f);
        auto lit = slider.findColour(juce::Slider::trackColourId);
        g.setColour(lit.isTransparent() || sketch::activeSkin() == sketch::Skin::Steel ? sketch::Theme::ochre() : lit);
        g.fillRoundedRectangle(left, cy - 1.0f, juce::jmax(0.0f, sliderPos - left), 2.0f, 1.0f);
        const auto cap = juce::Rectangle<float>(sliderPos - 4.5f, cy - 7.0f, 9.0f, 14.0f);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(cap.translated(0.0f, 1.0f), 1.5f);
        juce::ColourGradient capFill(sketch::Theme::paperLight().brighter(0.35f), 0.0f, cap.getY(), sketch::Theme::paperDeep(), 0.0f, cap.getBottom(), false);
        g.setGradientFill(capFill);
        g.fillRoundedRectangle(cap, 1.5f);
        g.setColour(sketch::Theme::ochreGlow().withAlpha(0.8f));
        g.drawLine(cap.getCentreX(), cap.getY() + 3.0f, cap.getCentreX(), cap.getBottom() - 3.0f, 1.0f);
        return;
    }

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
