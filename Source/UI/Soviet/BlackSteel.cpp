#include "BlackSteel.h"

#include <SovietAssets.h>

#include <cmath>

namespace bbg::ui::steel
{
namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;

juce::Image loadHardwareImage(const char* name)
{
    int size = 0;
    const char* data = SovietBinaryData::getNamedResource(name, size);
    return data != nullptr ? juce::ImageCache::getFromMemory(data, size) : juce::Image();
}

struct HardwareImages
{
    juce::Image knobAluminum { loadHardwareImage("knob_aluminum_brass_insert_png") };
    juce::Image knobBakelite { loadHardwareImage("knob_bakelite_ribbed_png") };
    juce::Image knobIron { loadHardwareImage("knob_chipped_iron_dark_png") };
    juce::Image knobLathe { loadHardwareImage("knob_lathe_steel_rust_png") };
    juce::Image pointerWhite { loadHardwareImage("pointer_white_enamel_png") };
    juce::Image pointerRed { loadHardwareImage("pointer_red_tip_brass_collar_png") };
    juce::Image pointerBrass { loadHardwareImage("pointer_brass_wedge_png") };
    juce::Image pointerBlack { loadHardwareImage("pointer_black_instrument_png") };
    juce::Image togglePlate { loadHardwareImage("toggle_switch_plate_png") };
    juce::Image toggleLeverUp { loadHardwareImage("toggle_switch_lever_up_png") };
    juce::Image toggleLeverDown { loadHardwareImage("toggle_switch_lever_down_png") };
    juce::Image lampOff { loadHardwareImage("lamp_panel_red_off_png") };
    juce::Image lampOn { loadHardwareImage("lamp_panel_red_on_png") };
};

// Function-local static construction is thread-safe and happens on the message thread on first
// paint. The audio processor never calls any of these UI drawing functions.
const HardwareImages& hardwareImages()
{
    static const HardwareImages images;
    return images;
}

const juce::Image& knobImage(const HardwareImages& images, KnobAsset asset)
{
    switch (asset)
    {
        case KnobAsset::BakeliteRibbed: return images.knobBakelite;
        case KnobAsset::ChippedIronDark: return images.knobIron;
        case KnobAsset::LatheSteelRust: return images.knobLathe;
        case KnobAsset::AluminumBrassInsert:
        default: return images.knobAluminum;
    }
}

const juce::Image& pointerImage(const HardwareImages& images, PointerAsset asset)
{
    switch (asset)
    {
        case PointerAsset::RedTipBrassCollar: return images.pointerRed;
        case PointerAsset::BrassWedge: return images.pointerBrass;
        case PointerAsset::BlackInstrument: return images.pointerBlack;
        case PointerAsset::WhiteEnamel:
        default: return images.pointerWhite;
    }
}

void drawCenteredImage(juce::Graphics& g, const juce::Image& image, juce::Point<float> centre, float width, float height)
{
    if (!image.isValid())
        return;

    g.drawImage(image, juce::Rectangle<float>(centre.x - width * 0.5f, centre.y - height * 0.5f, width, height),
                juce::RectanglePlacement::stretchToFit, false);
}
}

juce::FontOptions stencilFont(float size, bool bold)
{
    // The design uses Oswald (Google Fonts); Bahnschrift is the closest condensed face shipped with
    // Windows, so nothing is downloaded or embedded.
    return juce::FontOptions("Bahnschrift", size, bold ? juce::Font::bold : juce::Font::plain).withHorizontalScale(0.86f);
}

juce::FontOptions monoFont(float size)
{
    return juce::FontOptions("Consolas", size, juce::Font::plain);
}

juce::Colour rgba(int r, int g, int b, float a)
{
    return juce::Colour(static_cast<juce::uint8>(r), static_cast<juce::uint8>(g), static_cast<juce::uint8>(b), a);
}

void engraved(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float size, juce::Colour colour,
              juce::Justification just, float spacing)
{
    g.setFont(stencilFont(size).withKerningFactor(spacing));
    g.setColour(juce::Colours::black);
    g.drawText(text, r.translated(0.0f, 1.0f), just, false);
    g.setColour(colour.withAlpha(0.93f * colour.getFloatAlpha()));
    g.drawText(text, r, just, false);
}

void hexagon(juce::Graphics& g, juce::Point<float> c, float w, float h, juce::Colour light, juce::Colour dark)
{
    juce::Path p;
    p.startNewSubPath(c.x - w * 0.25f, c.y - h * 0.47f);
    p.lineTo(c.x + w * 0.25f, c.y - h * 0.47f);
    p.lineTo(c.x + w * 0.48f, c.y);
    p.lineTo(c.x + w * 0.25f, c.y + h * 0.47f);
    p.lineTo(c.x - w * 0.25f, c.y + h * 0.47f);
    p.lineTo(c.x - w * 0.48f, c.y);
    p.closeSubPath();
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillPath(p, juce::AffineTransform::translation(0.0f, 3.0f));
    juce::ColourGradient grad(light, c.x - w * 0.1f, c.y - h * 0.2f, dark, c.x + w * 0.4f, c.y + h * 0.5f, true);
    g.setGradientFill(grad);
    g.fillPath(p);
}

void panel(juce::Graphics& g, float kW, float kH, int seed)
{
    const juce::Rectangle<float> stage(0.0f, 0.0f, kW, kH);
    juce::ColourGradient border(juce::Colour(0xff444444), 0, 0, juce::Colour(0xff3a3a38), kW, kH, false);
    border.addColour(0.30, juce::Colour(0xff0a0a0a));
    border.addColour(0.55, juce::Colour(0xff2b2b2a));
    border.addColour(0.80, juce::Colour(0xff050505));
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.fillRoundedRectangle(stage.translated(0, 14).reduced(6), 12.0f);
    g.setGradientFill(border);
    g.fillRoundedRectangle(stage, 10.0f);
    const auto inner = stage.reduced(16.0f);
    juce::ColourGradient fill(juce::Colour(0xff30302e), kW * 0.5f, inner.getY(), juce::Colour(0xff111211), kW * 0.5f, inner.getY() + kW * 0.62f, true);
    fill.addColour(0.45, juce::Colour(0xff1d1e1d));
    g.setGradientFill(fill);
    g.fillRoundedRectangle(inner, 4.0f);

    g.saveState();
    g.reduceClipRegion(inner.toNearestInt());
    juce::Random r(seed);
    const float w = inner.getWidth(), h = inner.getHeight(), ox = inner.getX(), oy = inner.getY();
    const float area = (w * h) / (1148.0f * 668.0f); // densities of the HamloComp plate
    const auto count = [area](int n) { return juce::roundToInt(static_cast<float>(n) * area); };
    for (int i = 0; i < count(26); ++i)
    {
        const float x = ox + r.nextFloat() * w, y = oy + r.nextFloat() * h, rad = 60.0f + r.nextFloat() * 180.0f;
        juce::ColourGradient blob(juce::Colours::black.withAlpha(0.22f), x, y, juce::Colours::transparentBlack, x + rad, y, true);
        g.setGradientFill(blob);
        g.fillEllipse(x - rad, y - rad, 2 * rad, 2 * rad);
    }
    for (int i = 0; i < count(9000); ++i)
    {
        g.setColour(juce::Colour::fromFloatRGBA(0.75f, 0.72f, 0.65f, 0.04f + r.nextFloat() * 0.07f));
        g.fillRect(ox + r.nextFloat() * w, oy + r.nextFloat() * h, 1.0f, 1.0f);
    }
    const auto edgeBias = [&r]
    {
        const float e = r.nextFloat();
        return e < 0.5f ? r.nextFloat() : (r.nextFloat() < 0.5f ? r.nextFloat() * 0.12f : 1.0f - r.nextFloat() * 0.12f);
    };
    for (int i = 0; i < count(14); ++i)
    {
        const float x = ox + r.nextFloat() * w, y = oy + r.nextFloat() * h, rad = 40.0f + r.nextFloat() * 140.0f;
        const bool lite = r.nextFloat() < 0.5f;
        juce::ColourGradient s(lite ? juce::Colour::fromFloatRGBA(0.78f, 0.75f, 0.63f, 0.045f) : juce::Colours::black.withAlpha(0.22f), x, y,
                               juce::Colours::transparentBlack, x + rad, y, true);
        g.setGradientFill(s);
        g.fillEllipse(x - rad, y - rad, 2 * rad, 2 * rad);
    }
    for (int i = 0; i < count(46); ++i)
    {
        const bool nearX = r.nextFloat() < 0.5f;
        const float x = ox + (nearX ? edgeBias() : r.nextFloat()) * w, y = oy + (nearX ? r.nextFloat() : edgeBias()) * h;
        const float rad = 4.0f + r.nextFloat() * 34.0f;
        juce::ColourGradient s(rgba(105 + r.nextInt(30), 48 + r.nextInt(16), 18, 0.12f + r.nextFloat() * 0.2f), x, y, rgba(90, 40, 14, 0.0f),
                               x + rad, y, true);
        s.addColour(0.6, rgba(90, 40, 14, 0.12f));
        g.setGradientFill(s);
        g.fillEllipse(x - rad, y - rad, 2 * rad, 2 * rad);
    }
    for (int i = 0; i < count(30); ++i)
    {
        const int side = r.nextInt(4);
        const float t = r.nextFloat();
        const float x = ox + (side == 0 ? t * w : side == 1 ? w - r.nextFloat() * 8 : side == 2 ? t * w : r.nextFloat() * 8);
        const float y = oy + (side == 0 ? r.nextFloat() * 8 : side == 1 ? t * h : side == 2 ? h - r.nextFloat() * 8 : t * h);
        const float s = 1.5f + r.nextFloat() * 5.0f;
        juce::Path chip;
        for (int k = 0; k < 7; ++k)
        {
            const float a = static_cast<float>(k) / 7.0f * 2.0f * kPi, rr = s * (0.5f + r.nextFloat() * 0.7f);
            if (k == 0)
                chip.startNewSubPath(x + std::cos(a) * rr, y + std::sin(a) * rr * 0.7f);
            else
                chip.lineTo(x + std::cos(a) * rr, y + std::sin(a) * rr * 0.7f);
        }
        chip.closeSubPath();
        g.setColour(r.nextFloat() < 0.55f ? rgba(120, 118, 110, 0.5f) : rgba(95, 46, 18, 0.6f));
        g.fillPath(chip);
    }
    for (int i = 0; i < count(380); ++i)
    {
        const float x = ox + r.nextFloat() * w, y = oy + r.nextFloat() * h, a = r.nextFloat() * 2.0f * kPi;
        const float l = i < 24 ? 120.0f + r.nextFloat() * 380.0f : 6.0f + r.nextFloat() * 70.0f;
        g.setColour(rgba(200 + r.nextInt(40), 195 + r.nextInt(40), 170 + r.nextInt(30), 0.05f + r.nextFloat() * (i < 24 ? 0.16f : 0.26f)));
        juce::Path scratch;
        scratch.startNewSubPath(x, y);
        scratch.quadraticTo(x + std::cos(a) * l * 0.5f + (r.nextFloat() - 0.5f) * 6.0f, y + std::sin(a) * l * 0.5f + (r.nextFloat() - 0.5f) * 6.0f,
                            x + std::cos(a) * l, y + std::sin(a) * l);
        g.strokePath(scratch, juce::PathStrokeType(0.4f + r.nextFloat() * 0.7f));
    }
    for (int i = 0; i < count(900); ++i)
    {
        g.setColour(r.nextFloat() < 0.5f ? juce::Colours::black.withAlpha(0.2f + r.nextFloat() * 0.4f) : rgba(210, 200, 170, 0.05f + r.nextFloat() * 0.2f));
        g.fillRect(ox + r.nextFloat() * w, oy + r.nextFloat() * h, 0.7f + r.nextFloat() * 1.6f, 0.7f + r.nextFloat() * 1.6f);
    }
    juce::ColourGradient vignette(juce::Colours::transparentBlack, ox + w * 0.5f, oy + h * 0.5f, juce::Colours::black.withAlpha(0.38f),
                                  ox + w * 0.5f + w * 0.62f, oy + h * 0.5f, true);
    g.setGradientFill(vignette);
    g.fillRect(inner);
    g.restoreState();

    for (auto c : { juce::Point<float>(39, 39), juce::Point<float>(kW - 39, 39), juce::Point<float>(39, kH - 39), juce::Point<float>(kW - 39, kH - 39) })
    {
        hexagon(g, c, 30.0f, 30.0f, juce::Colour(0xff77776f), juce::Colour(0xff1c1c1a));
        g.setColour(juce::Colour(0xff1a1a19));
        g.fillEllipse(c.x - 5.5f, c.y - 5.5f, 11.0f, 11.0f);
    }
}

void nameplate(juce::Graphics& g, juce::Rectangle<float> plate, const juce::String& title, const juce::String& model, const juce::String& number)
{
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.fillRoundedRectangle(plate.translated(0, 6).expanded(2), 6.0f);
    juce::ColourGradient grad(juce::Colour(0xffd9d7ce), plate.getX(), plate.getY(), juce::Colour(0xffbdbcb2), plate.getRight(), plate.getBottom(), false);
    grad.addColour(0.45, juce::Colour(0xffa6a69d));
    grad.addColour(0.55, juce::Colour(0xff8d8d85));
    g.setColour(juce::Colour(0xff1a1a19));
    g.fillRoundedRectangle(plate.expanded(2), 6.0f);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(plate, 5.0f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    for (float x = plate.getX(); x < plate.getRight(); x += 3.0f)
        g.drawVerticalLine(juce::roundToInt(x), plate.getY(), plate.getBottom());
    for (float rx : { plate.getX() + 21.0f, plate.getRight() - 21.0f })
    {
        juce::ColourGradient rivet(juce::Colour(0xfffafafa), rx - 3, plate.getCentreY() - 4, juce::Colour(0xff3c3c38), rx + 9, plate.getCentreY() + 9, true);
        g.setGradientFill(rivet);
        g.fillEllipse(rx - 9, plate.getCentreY() - 9, 18, 18);
    }
    g.setColour(juce::Colour(0xff2a2926));
    g.setFont(stencilFont(44.0f).withHorizontalScale(0.9f));
    g.drawText(title, plate.withHeight(50.0f).translated(0, 4), juce::Justification::centred);
    g.setFont(stencilFont(13.0f).withKerningFactor(0.12f));
    auto row = plate.withTrimmedTop(54.0f).withHeight(17.0f);
    g.drawText(model, row.withTrimmedRight(row.getWidth() * 0.5f + 16.0f), juce::Justification::centredRight);
    juce::Path star;
    star.addStar({ row.getCentreX(), row.getCentreY() }, 5, 3.0f, 7.5f, 0.0f);
    g.setColour(juce::Colour(0xffb3261e));
    g.fillPath(star);
    const auto no = juce::Rectangle<float>(row.getCentreX() + 16.0f, row.getY(), 70.0f, 17.0f);
    g.setColour(juce::Colour(0xff8d8a7c));
    g.fillRoundedRectangle(no, 2.0f);
    g.setColour(juce::Colour(0xff2a2926));
    g.setFont(monoFont(13.0f));
    g.drawText(number, no, juce::Justification::centred);
    g.setFont(stencilFont(11.5f).withKerningFactor(0.2f));
    g.drawText("MADE IN USSR", plate.withTrimmedTop(72.0f).withHeight(15.0f), juce::Justification::centred);
}

void knobScale(juce::Graphics& g, juce::Point<float> c, float s)
{
    for (int i = 0; i < 21; ++i)
    {
        const float a = (-135.0f + 270.0f / 20.0f * static_cast<float>(i)) * kPi / 180.0f;
        const bool major = i % 2 == 0;
        const float r1 = 62.0f * s, r2 = (major ? 71.0f : 67.0f) * s;
        g.setColour(juce::Colour(0xffefe3bd));
        g.drawLine(c.x + std::sin(a) * r1, c.y - std::cos(a) * r1, c.x + std::sin(a) * r2, c.y - std::cos(a) * r2,
                   (major ? 2.4f : 1.4f) * std::sqrt(s));
    }
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillEllipse(c.x - 50.0f * s, c.y - 40.0f * s, 100.0f * s, 100.0f * s);
    g.setColour(juce::Colours::black);
    g.fillEllipse(c.x - 49.0f * s, c.y - 49.0f * s, 98.0f * s, 98.0f * s);
}

void brassKnob(juce::Graphics& g, juce::Point<float> c, float angle, float s, float alpha, KnobAsset knob, PointerAsset pointer)
{
    const auto& images = hardwareImages();
    g.saveState();
    if (alpha < 1.0f)
        g.beginTransparencyLayer(juce::jlimit(0.0f, 1.0f, alpha));

    const float size = 108.0f * s;
    g.setColour(juce::Colours::black.withAlpha(0.62f));
    g.fillEllipse(c.x - size * 0.5f + 1.5f * s, c.y - size * 0.5f + 3.0f * s, size, size);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    drawCenteredImage(g, knobImage(images, knob), c, size, size);

    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(angle, c.x, c.y));
    drawCenteredImage(g, pointerImage(images, pointer), c, size, size);
    g.restoreState();

    if (alpha < 1.0f)
        g.endTransparencyLayer();
    g.restoreState();
}

void lever(juce::Graphics& g, juce::Point<float> base, float a, float s, LeverPosition position)
{
    const auto& images = hardwareImages();
    auto state = position;
    if (state == LeverPosition::Auto)
        state = a < -0.18f ? LeverPosition::Up : a > 0.18f ? LeverPosition::Down : LeverPosition::Neutral;

    // The source pack uses one shared 285x380 coordinate system for the plate and both lever
    // overlays. Keeping one destination rectangle makes the pivot exact at every UI scale.
    const float width = 44.0f * s, height = width * 380.0f / 285.0f;
    const juce::Rectangle<float> dst(base.x - width * 0.5f, base.y - height * (202.0f / 380.0f), width, height);
    g.saveState();
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    drawCenteredImage(g, images.togglePlate, { dst.getCentreX(), dst.getY() + height * 0.5f }, width, height);
    if (state == LeverPosition::Up)
        drawCenteredImage(g, images.toggleLeverUp, { dst.getCentreX(), dst.getY() + height * 0.5f }, width, height);
    else if (state == LeverPosition::Down)
        drawCenteredImage(g, images.toggleLeverDown, { dst.getCentreX(), dst.getY() + height * 0.5f }, width, height);
    g.restoreState();
}

void lamp(juce::Graphics& g, juce::Point<float> c, bool on, float s, juce::Colour colour)
{
    const auto& images = hardwareImages();

    // FREEZE uses the old cyan lamp treatment; the owner's red lamp pack is used for all normal
    // red pilot lamps and keeps the exact on/off bezel and glow in one cached image.
    if (colour.getRed() >= colour.getBlue() * 1.2f)
    {
        const float size = 68.0f * s;
        if (on)
        {
            const float halo = 62.0f * s;
            juce::ColourGradient glow(colour.withAlpha(0.26f), c.x, c.y, colour.withAlpha(0.0f), c.x + halo, c.y, true);
            g.setGradientFill(glow);
            g.fillEllipse(c.x - halo, c.y - halo, 2.0f * halo, 2.0f * halo);
        }
        g.saveState();
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        drawCenteredImage(g, on ? images.lampOn : images.lampOff, c, size, size);
        g.restoreState();
        return;
    }

    // Preserve the established coloured-lamp rendering for the blue FREEZE indicator.
    g.setColour(juce::Colour(0xff111111));
    g.fillEllipse(c.x - 34 * s, c.y - 34 * s, 68 * s, 68 * s);
    g.setColour(juce::Colour(0xff8d8d85));
    g.fillEllipse(c.x - 32 * s, c.y - 32 * s, 64 * s, 64 * s);
    if (on)
    {
        for (float rr : { 60.0f * s, 44.0f * s })
        {
            juce::ColourGradient glow(colour.withAlpha(rr > 50 * s ? 0.25f : 0.45f), c.x, c.y, colour.withAlpha(0.0f), c.x + rr, c.y, true);
            g.setGradientFill(glow);
            g.fillEllipse(c.x - rr, c.y - rr, 2 * rr, 2 * rr);
        }
        juce::ColourGradient l(juce::Colour(0xfffff3f0), c.x - 26 * s + 52 * s * 0.35f, c.y - 26 * s + 52 * s * 0.30f, colour.darker(1.2f), c.x + 26 * s,
                               c.y + 26 * s, true);
        l.addColour(0.32, colour.brighter(0.15f));
        g.setGradientFill(l);
    }
    else
    {
        juce::ColourGradient l(juce::Colour(0xff555555), c.x - 26 * s + 52 * s * 0.35f, c.y - 26 * s + 52 * s * 0.30f, juce::Colour(0xff0a0a09),
                               c.x + 26 * s, c.y + 26 * s, true);
        l.addColour(0.6, juce::Colour(0xff1c1c1b));
        g.setGradientFill(l);
    }
    g.fillEllipse(c.x - 26 * s, c.y - 26 * s, 52 * s, 52 * s);
}

void lcdWindow(juce::Graphics& g, juce::Rectangle<float> lcd)
{
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(lcd.expanded(2), 4.0f);
    g.setColour(juce::Colour(0xff120b04));
    g.fillRoundedRectangle(lcd, 3.0f);
}

void lcdText(juce::Graphics& g, juce::Rectangle<float> lcd, const juce::String& text, float size, juce::Justification just)
{
    g.setFont(monoFont(size));
    g.setColour(juce::Colour(0x66ff9a1f));
    g.drawText(text, lcd.translated(0, 0.5f).expanded(1.0f), just);
    g.setColour(juce::Colour(0xffffb347));
    g.drawText(text, lcd, just);
}

void meterFace(juce::Graphics& g, const Meter& m)
{
    const auto b = m.bezel;
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.fillRoundedRectangle(b.translated(0, 10).expanded(2), 10.0f);
    juce::ColourGradient brass(juce::Colour(0xfff0d588), b.getX(), b.getY(), juce::Colour(0xffb8923f), b.getRight(), b.getBottom(), false);
    brass.addColour(0.45, juce::Colour(0xffa07a2e));
    brass.addColour(0.55, juce::Colour(0xff66491a));
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(b.expanded(2), 10.0f);
    g.setGradientFill(brass);
    g.fillRoundedRectangle(b, 9.0f);
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    for (float x = b.getX(); x < b.getRight(); x += 3.0f)
        g.drawVerticalLine(juce::roundToInt(x), b.getY() + 2, b.getBottom() - 2);
    for (float sx : { b.getX() + 11.5f, b.getRight() - 11.5f })
    {
        juce::ColourGradient screw(juce::Colour(0xfffff1c4), sx - 2, b.getBottom() - 11, juce::Colour(0xff3a2a0a), sx + 5, b.getBottom() - 3, true);
        g.setGradientFill(screw);
        g.fillEllipse(sx - 5.5f, b.getBottom() - 14.0f, 11.0f, 11.0f);
    }

    const auto face = b.reduced(15.0f);
    g.setColour(juce::Colour(0xff1c1408));
    g.fillRoundedRectangle(face.expanded(3), 4.0f);
    g.saveState();
    g.reduceClipRegion(face.toNearestInt());
    g.addTransform(juce::AffineTransform::scale(face.getWidth() / 300.0f, face.getHeight() / 200.0f).translated(face.getX(), face.getY()));
    constexpr float W = 300.0f, H = 200.0f, PX = W / 2, PY = H + 60.0f, A = 0.9f, R = 190.0f;
    const auto ang = [&](float v) { return -A + (v - m.min) / (m.max - m.min) * 2.0f * A; };
    const auto pt = [&](float v, float r) { return juce::Point<float>(PX + std::sin(ang(v)) * r, PY - std::cos(ang(v)) * r); };
    juce::ColourGradient paper(juce::Colour(0xfff1dfa0), W / 2, H * 0.5f, juce::Colour(0xffa98b4a), W / 2 + 230.0f, H * 0.55f, true);
    paper.addColour(0.55, juce::Colour(0xffdcc57c));
    g.setGradientFill(paper);
    g.fillRect(0.0f, 0.0f, W, H);
    juce::Random r(m.seed);
    for (int i = 0; i < 16; ++i)
    {
        const float x = r.nextFloat() * W, y = r.nextFloat() * H, rad = 14.0f + r.nextFloat() * 50.0f;
        juce::ColourGradient stain(rgba(120, 80, 20, 0.05f + r.nextFloat() * 0.09f), x, y, rgba(120, 80, 20, 0.0f), x + rad, y, true);
        g.setGradientFill(stain);
        g.fillEllipse(x - rad, y - rad, 2 * rad, 2 * rad);
    }
    for (int i = 0; i < 160; ++i)
    {
        g.setColour(rgba(70, 45, 15, 0.12f + r.nextFloat() * 0.3f));
        g.fillRect(r.nextFloat() * W, r.nextFloat() * H, 0.8f + r.nextFloat() * 1.2f, 0.8f + r.nextFloat() * 1.2f);
    }
    const juce::Colour ink(0xff1b1710), redInk(0xffa82820);
    juce::Path arc;
    arc.addCentredArc(PX, PY, R, R, 0.0f, ang(m.min), ang(m.redFrom), true);
    g.setColour(ink);
    g.strokePath(arc, juce::PathStrokeType(3.0f));
    juce::Path redArc;
    redArc.addCentredArc(PX, PY, R - 1, R - 1, 0.0f, ang(m.redFrom), ang(m.max), true);
    g.setColour(redInk);
    g.strokePath(redArc, juce::PathStrokeType(6.0f));
    juce::Path innerArc;
    innerArc.addCentredArc(PX, PY, R - 12, R - 12, 0.0f, ang(m.min), ang(m.max), true);
    g.setColour(ink);
    g.strokePath(innerArc, juce::PathStrokeType(1.6f));
    g.setFont(stencilFont(15.0f, true));
    for (const auto& [v, labelled] : m.ticks)
    {
        const bool red = v >= m.redFrom;
        g.setColour(red ? redInk : ink);
        g.drawLine(juce::Line<float>(pt(v, R - 12), pt(v, R + (labelled ? 10.0f : 5.0f))), labelled ? 2.4f : 1.4f);
        if (labelled)
        {
            const auto p = pt(v, R - 28);
            g.drawText(v > 0 ? "+" + juce::String(static_cast<int>(v)) : juce::String(static_cast<int>(v)), juce::Rectangle<float>(p.x - 18, p.y - 9, 36, 18),
                       juce::Justification::centred);
        }
    }
    g.setColour(ink);
    g.setFont(stencilFont(m.titleSize, true));
    g.drawText(m.title, juce::Rectangle<float>(0, 124 - 14, W, 28), juce::Justification::centred);
    g.setFont(stencilFont(14.0f, false));
    g.drawText(m.unit, juce::Rectangle<float>(0, 144 - 9, W, 18), juce::Justification::centred);
    g.restoreState();
}

void meterNeedle(juce::Graphics& g, const Meter& m)
{
    const auto face = m.bezel.reduced(15.0f);
    g.saveState();
    g.reduceClipRegion(face.toNearestInt());
    g.addTransform(juce::AffineTransform::scale(face.getWidth() / 300.0f, face.getHeight() / 200.0f).translated(face.getX(), face.getY()));
    constexpr float W = 300.0f, H = 200.0f, PX = W / 2, PY = H + 60.0f, A = 0.9f, R = 190.0f;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float tremor = static_cast<float>((std::sin(now / 83.0) + std::sin(now / 31.0) * 0.6) * 0.03 * (m.max - m.min) / 46.0);
    const float v = juce::jlimit(m.min - 2.0f, m.max + 1.5f, m.shown + tremor);
    const float a = -A + (v - m.min) / (m.max - m.min) * 2.0f * A;
    const juce::Point<float> tip(PX + std::sin(a) * (R + 6), PY - std::cos(a) * (R + 6));
    g.setColour(juce::Colour(0x552a1a0a));
    g.drawLine(PX + 4, PY + 4, tip.x + 4, tip.y + 5, 3.0f);
    g.setColour(juce::Colour(0xff0d0b08));
    g.drawLine(PX, PY, tip.x, tip.y, 2.2f);
    juce::Path plate;
    plate.startNewSubPath(W / 2 - 62, H);
    plate.quadraticTo(W / 2, H - 46, W / 2 + 62, H);
    plate.closeSubPath();
    juce::ColourGradient pl(juce::Colour(0xff2d2a22), 0, H - 30, juce::Colour(0xff0c0b09), 0, H, false);
    g.setGradientFill(pl);
    g.fillPath(plate);
    g.setColour(juce::Colour(0xffb79b4a));
    g.fillEllipse(W / 2 - 6, H - 15, 12, 12);
    g.setColour(juce::Colour(0xff6a5520));
    g.fillRect(W / 2 - 4, H - 10, 8.0f, 2.0f);
    juce::Random r(m.seed + 1000);
    for (int i = 0; i < 26; ++i)
    {
        const float x = r.nextFloat() * W, y = r.nextFloat() * H, an = r.nextFloat() * kPi, l = 8.0f + r.nextFloat() * (i < 4 ? 130.0f : 38.0f);
        g.setColour(juce::Colours::white.withAlpha(0.1f + r.nextFloat() * 0.28f));
        g.drawLine(x, y, x + std::cos(an) * l, y + std::sin(an) * l, 0.5f + r.nextFloat() * 0.5f);
    }
    juce::ColourGradient glare(juce::Colour(0x40ffffff), 0, 0, juce::Colour(0x00ffffff), W * 0.41f, H * 0.41f, false);
    glare.addColour(0.97, juce::Colour(0x12ffffff));
    g.setGradientFill(glare);
    g.fillRect(0.0f, 0.0f, W, H);
    juce::ColourGradient edge(juce::Colour(0x00000000), W / 2, H / 2, juce::Colour(0x662a1a0a), W / 2 + 215.0f, H / 2, true);
    edge.addColour(80.0 / 215.0, juce::Colour(0x00000000));
    g.setGradientFill(edge);
    g.fillRect(0.0f, 0.0f, W, H);
    g.restoreState();
}

void crtBezel(juce::Graphics& g, juce::Rectangle<float> b, juce::Rectangle<float> screen, const juce::String& label, float labelY)
{
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.fillRoundedRectangle(b.translated(0, 10).expanded(2), 12.0f);
    juce::ColourGradient metal(juce::Colour(0xff5a5c58), b.getX(), b.getY(), juce::Colour(0xff1d1e1c), b.getRight(), b.getBottom(), false);
    metal.addColour(0.35, juce::Colour(0xff3a3c39));
    metal.addColour(0.6, juce::Colour(0xff2a2b29));
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(b.expanded(2), 12.0f);
    g.setGradientFill(metal);
    g.fillRoundedRectangle(b, 11.0f);
    juce::Random r(77);
    for (int i = 0; i < 260; ++i)
    {
        g.setColour(rgba(220, 220, 210, 0.03f + r.nextFloat() * 0.06f));
        const float x = b.getX() + r.nextFloat() * b.getWidth(), y = b.getY() + r.nextFloat() * b.getHeight();
        g.drawLine(x, y, x + 4.0f + r.nextFloat() * 18.0f, y + (r.nextFloat() - 0.5f) * 2.0f, 0.5f);
    }
    for (auto c : { juce::Point<float>(b.getX() + 10, b.getY() + 10), juce::Point<float>(b.getRight() - 10, b.getY() + 10),
                    juce::Point<float>(b.getX() + 10, b.getBottom() - 10), juce::Point<float>(b.getRight() - 10, b.getBottom() - 10) })
    {
        juce::ColourGradient screw(juce::Colour(0xffd8d8d0), c.x - 2, c.y - 3, juce::Colour(0xff2a2a28), c.x + 5, c.y + 5, true);
        g.setGradientFill(screw);
        g.fillEllipse(c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
        g.setColour(juce::Colour(0xff1a1a19));
        g.drawLine(c.x - 3.5f, c.y + 1.5f, c.x + 3.5f, c.y - 1.5f, 1.2f);
    }
    const auto recess = screen.expanded(5.0f);
    g.setColour(juce::Colour(0xff050505));
    g.fillRoundedRectangle(recess, 14.0f);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(recess.translated(0, 1.5f), 14.0f, 1.2f);
    engraved(g, label, juce::Rectangle<float>(b.getX() + 20.0f, labelY, 130.0f, 18.0f), 12.0f, stencilDim, juce::Justification::centredLeft, 0.2f);
}

void crtBegin(juce::Graphics& g, juce::Rectangle<float> s)
{
    g.saveState();
    juce::Path glass;
    glass.addRoundedRectangle(s, 12.0f);
    g.reduceClipRegion(glass);
    juce::ColourGradient tube(juce::Colour(0xff0a1f12), s.getCentreX(), s.getCentreY(), juce::Colour(0xff010703), s.getRight() + 40.0f,
                              s.getCentreY(), true);
    g.setGradientFill(tube);
    g.fillRect(s);
}

void crtEnd(juce::Graphics& g, juce::Rectangle<float> s)
{
    g.setColour(juce::Colours::black.withAlpha(0.16f));
    for (float y = s.getY(); y < s.getBottom(); y += 3.0f)
        g.fillRect(s.getX(), y, s.getWidth(), 1.0f);
    juce::ColourGradient edge(juce::Colours::transparentBlack, s.getCentreX(), s.getCentreY(), juce::Colours::black.withAlpha(0.65f),
                              s.getRight() + 30.0f, s.getBottom() + 30.0f, true);
    edge.addColour(0.62, juce::Colours::transparentBlack);
    g.setGradientFill(edge);
    g.fillRect(s);
    juce::Path sheen;
    sheen.startNewSubPath(s.getX(), s.getY());
    sheen.lineTo(s.getX() + s.getWidth() * 0.55f, s.getY());
    sheen.quadraticTo(s.getX() + s.getWidth() * 0.25f, s.getY() + s.getHeight() * 0.25f, s.getX(), s.getY() + s.getHeight() * 0.62f);
    sheen.closeSubPath();
    juce::ColourGradient glare(juce::Colours::white.withAlpha(0.09f), s.getX(), s.getY(), juce::Colours::white.withAlpha(0.0f),
                               s.getX() + s.getWidth() * 0.35f, s.getY() + s.getHeight() * 0.35f, false);
    g.setGradientFill(glare);
    g.fillPath(sheen);
    g.restoreState();
    g.setColour(juce::Colours::black.withAlpha(0.9f));
    g.drawRoundedRectangle(s, 12.0f, 2.0f);
}

void crtButton(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool lit)
{
    g.setColour(juce::Colour(0xff0b0c0b));
    g.fillRoundedRectangle(r.expanded(1.5f), 4.0f);
    juce::ColourGradient cap(lit ? juce::Colour(0xff2d6b43) : juce::Colour(0xff2e302d), r.getX(), r.getY(),
                             lit ? juce::Colour(0xff123a22) : juce::Colour(0xff161716), r.getX(), r.getBottom(), false);
    g.setGradientFill(cap);
    g.fillRoundedRectangle(r, 3.0f);
    if (lit)
    {
        juce::ColourGradient halo(phosphor.withAlpha(0.35f), r.getCentreX(), r.getCentreY(), phosphor.withAlpha(0.0f), r.getRight() + 16.0f,
                                  r.getCentreY(), true);
        g.setGradientFill(halo);
        g.fillRoundedRectangle(r.expanded(10.0f, 6.0f), 8.0f);
    }
    g.setFont(stencilFont(12.5f).withKerningFactor(0.15f));
    g.setColour(lit ? phosphor : stencilDim);
    g.drawText(text, r, juce::Justification::centred);
}

void glow(juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float intensity)
{
    for (const auto& [w, a] : { std::pair { 9.0f, 0.05f }, std::pair { 5.0f, 0.10f }, std::pair { 2.6f, 0.24f } })
    {
        g.setColour(c.withAlpha(a * intensity));
        g.strokePath(p, juce::PathStrokeType(width * w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(c.withAlpha(0.95f * intensity));
    g.strokePath(p, juce::PathStrokeType(width * 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.withAlpha(0.55f * intensity));
    g.strokePath(p, juce::PathStrokeType(width * 0.45f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void glowDot(juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float intensity)
{
    juce::ColourGradient halo(col.withAlpha(0.45f * intensity), c.x, c.y, col.withAlpha(0.0f), c.x + r * 3.2f, c.y, true);
    g.setGradientFill(halo);
    g.fillEllipse(c.x - r * 3.2f, c.y - r * 3.2f, r * 6.4f, r * 6.4f);
    g.setColour(col.withAlpha(intensity));
    g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r, 1.6f);
    g.setColour(juce::Colours::white.withAlpha(0.8f * intensity));
    g.fillEllipse(c.x - r * 0.35f, c.y - r * 0.35f, r * 0.7f, r * 0.7f);
}

void crtText(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float size, juce::Justification j, float alpha)
{
    g.setFont(monoFont(size));
    g.setColour(phosphor.withAlpha(alpha * 0.35f));
    g.drawText(t, r.expanded(1.0f), j, false);
    g.setColour(phosphor.withAlpha(alpha));
    g.drawText(t, r, j, false);
}

int numScratchPictures()
{
    return 24;
}

void scratches(juce::Graphics& g, std::initializer_list<Scratch> list, float strength)
{
    for (const auto& s : list)
    {
        const auto name = "ussr_" + juce::String(juce::jlimit(0, numScratchPictures() - 1, s.picture)).paddedLeft('0', 2) + "_png";
        int size = 0;
        const char* data = SovietBinaryData::getNamedResource(name.toRawUTF8(), size);
        if (data == nullptr)
            continue;
        const auto picture = juce::ImageCache::getFromMemory(data, size);
        if (!picture.isValid())
            continue;
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians(s.degrees), s.area.getCentreX(), s.area.getCentreY()));
        // groove: dark, shifted up-left; edge: pale steel catching the light
        g.setColour(juce::Colours::black.withAlpha(0.55f * strength));
        g.drawImage(picture, s.area.translated(-1.0f, -1.0f), juce::RectanglePlacement::centred, true);
        g.setColour(juce::Colour(0xffd8d4c8).withAlpha(0.26f * strength));
        g.drawImage(picture, s.area, juce::RectanglePlacement::centred, true);
    }
}
} // namespace bbg::ui::steel
