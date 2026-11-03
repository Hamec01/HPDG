// TEMPORARY (not committed): renders the HPDG "Black Steel / СССР" skin mock-up with HPEQ's BlackSteel
// pieces, as the visual reference for the HPDG UI task.

#include <iostream>

#include <juce_gui_basics/juce_gui_basics.h>

#include "UI/BlackSteel.h"

using namespace hpeq::ui::steel;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;

void pushButton(juce::Graphics& g, juce::Point<float> c, float r, juce::Colour cap, const juce::String& label, float labelSize, bool lit = false)
{
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillEllipse(c.x - r - 2, c.y - r + 6, 2 * r + 4, 2 * r + 4);
    juce::ColourGradient bezel(juce::Colour(0xffc9c9c1), c.x - r, c.y - r, juce::Colour(0xff2a2a27), c.x + r, c.y + r, false);
    g.setGradientFill(bezel);
    g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour(juce::Colour(0xff0d0d0c));
    g.fillEllipse(c.x - r * 0.84f, c.y - r * 0.84f, 2 * r * 0.84f, 2 * r * 0.84f);
    if (lit)
    {
        juce::ColourGradient halo(cap.withAlpha(0.45f), c.x, c.y, cap.withAlpha(0.0f), c.x + r * 1.9f, c.y, true);
        g.setGradientFill(halo);
        g.fillEllipse(c.x - r * 1.9f, c.y - r * 1.9f, 3.8f * r, 3.8f * r);
    }
    const float rc = r * 0.74f;
    juce::ColourGradient dome(cap.brighter(0.9f), c.x - rc * 0.4f, c.y - rc * 0.5f, cap.darker(1.1f), c.x + rc, c.y + rc, true);
    dome.addColour(0.35, cap);
    g.setGradientFill(dome);
    g.fillEllipse(c.x - rc, c.y - rc, 2 * rc, 2 * rc);
    g.setColour(juce::Colours::white.withAlpha(0.25f));
    g.fillEllipse(c.x - rc * 0.55f, c.y - rc * 0.7f, rc * 0.7f, rc * 0.35f);
    if (label.isNotEmpty())
        engraved(g, label, { c.x - 90.0f, c.y + r + 6.0f, 180.0f, 20.0f }, labelSize);
}

// Rectangular key (piano-style): export / drag
void key(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour stripe)
{
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(r.translated(0, 5), 4.0f);
    juce::ColourGradient body(juce::Colour(0xffe9e4d2), r.getX(), r.getY(), juce::Colour(0xffa39d88), r.getX(), r.getBottom(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(stripe);
    g.fillRect(r.getX() + 6, r.getY() + 5, r.getWidth() - 12, 3.0f);
    g.setColour(juce::Colour(0xff2a2926));
    g.setFont(stencilFont(15.0f).withKerningFactor(0.08f));
    g.drawText(text, r.translated(0, 3), juce::Justification::centred);
    g.setColour(juce::Colours::black.withAlpha(0.7f));
    g.drawRoundedRectangle(r, 4.0f, 1.2f);
}

// A small square push key with a lamp (rack S / M / L)
void miniKey(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on, juce::Colour lampColour)
{
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(r.translated(0, 3), 3.0f);
    juce::ColourGradient body(on ? juce::Colour(0xff6b6b63) : juce::Colour(0xff4a4a45), r.getX(), r.getY(), juce::Colour(0xff1b1b19), r.getX(), r.getBottom(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(on ? juce::Colour(0xfffff6d0) : stencilDim);
    g.setFont(stencilFont(12.0f));
    g.drawText(text, r, juce::Justification::centred);
    lamp(g, { r.getRight() + 6.0f, r.getCentreY() }, on, 0.16f, lampColour);
}

void selector(juce::Graphics& g, juce::Point<float> c, float radius, const juce::StringArray& names, int selected, float startDeg, float stepDeg,
              const juce::String& title)
{
    engraved(g, title, { c.x - 100.0f, c.y - radius - 52.0f, 200.0f, 22.0f }, 17.0f);
    for (int i = 0; i < names.size(); ++i)
    {
        const float a = (startDeg + stepDeg * static_cast<float>(i)) * kPi / 180.0f;
        const juce::Point<float> p(c.x + std::sin(a) * (radius + 26.0f), c.y - std::cos(a) * (radius + 26.0f));
        g.setColour(juce::Colour(0xffefe3bd));
        g.fillEllipse(c.x + std::sin(a) * radius * 0.92f - 2.5f, c.y - std::cos(a) * radius * 0.92f - 2.5f, 5.0f, 5.0f);
        g.setFont(stencilFont(12.5f).withKerningFactor(0.06f));
        g.setColour(i == selected ? juce::Colour(0xfffff6d0) : stencilDim);
        g.drawText(names[i], juce::Rectangle<float>(p.x - 44.0f, p.y - 9.0f, 88.0f, 18.0f), juce::Justification::centred);
    }
    g.setColour(juce::Colours::black);
    g.fillEllipse(c.x - radius * 0.72f, c.y - radius * 0.72f, radius * 1.44f, radius * 1.44f);
    const float a = (startDeg + stepDeg * static_cast<float>(selected)) * kPi / 180.0f;
    brassKnob(g, c, a, radius / 62.0f, 1.0f, KnobAsset::BakeliteRibbed, PointerAsset::WhiteEnamel);
}

void knob(juce::Graphics& g, juce::Point<float> c, float s, float norm, const juce::String& title, const juce::String& value)
{
    engraved(g, title, { c.x - 70.0f, c.y - 74.0f * s - 24.0f, 140.0f, 20.0f }, 15.0f);
    knobScale(g, c, s);
    brassKnob(g, c, (-135.0f + 270.0f * norm) * kPi / 180.0f, s, 1.0f, KnobAsset::AluminumBrassInsert, PointerAsset::WhiteEnamel);
    g.setFont(stencilFont(15.0f));
    g.setColour(stencil);
    g.drawText(value, juce::Rectangle<float>(c.x - 60.0f, c.y + 72.0f * s + 4.0f, 120.0f, 18.0f), juce::Justification::centred);
}

void threeWay(juce::Graphics& g, juce::Point<float> base, const juce::String& title, const juce::StringArray& opts, int selected)
{
    engraved(g, title, { base.x - 70.0f, base.y - 78.0f, 140.0f, 20.0f }, 15.0f);
    for (int i = 0; i < opts.size(); ++i)
    {
        const float x = base.x + (static_cast<float>(i) - (opts.size() - 1) * 0.5f) * 44.0f;
        g.setFont(stencilFont(11.5f));
        g.setColour(i == selected ? juce::Colour(0xfffff6d0) : stencilDim);
        g.drawText(opts[i], juce::Rectangle<float>(x - 24.0f, base.y - 54.0f, 48.0f, 14.0f), juce::Justification::centred);
    }
    const float spread = opts.size() == 2 ? 30.0f : 36.0f;
    lever(g, base, (-spread + 2.0f * spread / static_cast<float>(opts.size() - 1) * static_cast<float>(selected)) * kPi / 180.0f, 0.72f);
}
} // namespace

int runHpdgMockup(const juce::File& out)
{
    juce::ScopedJuceInitialiser_GUI gui;
    constexpr int W = 1600, H = 980;
    juce::Image image(juce::Image::ARGB, W, H, true);
    {
    juce::Graphics g(image);
    panel(g, static_cast<float>(W), static_cast<float>(H), 1987);
    scratches(g, { { 3, { 1440.0f, 600.0f, 90.0f, 130.0f }, 4.0f }, { 11, { 700.0f, 270.0f, 70.0f, 100.0f }, -6.0f } }, 0.8f);

    // ---------------------------------------------------------------- header
    nameplate(g, { 40.0f, 36.0f, 360.0f, 92.0f }, "HPDG", juce::String::fromUTF8("MODEL \xd0\x91\xd0\x94-1"), juce::String::fromUTF8("\xe2\x84\x96 0.1.0"));
    engraved(g, "HAMLOPROD DRUM GENERATOR", { 40.0f, 134.0f, 360.0f, 18.0f }, 12.5f, stencilDim);

    pushButton(g, { 500.0f, 92.0f }, 52.0f, juce::Colour(0xffd0301e), "GENERATE", 16.0f, true);
    pushButton(g, { 625.0f, 92.0f }, 40.0f, juce::Colour(0xff3fb36a), "PLAY", 15.0f, true);
    pushButton(g, { 728.0f, 92.0f }, 34.0f, juce::Colour(0xffe0a020), "MUTATE", 14.0f);
    pushButton(g, { 820.0f, 92.0f }, 30.0f, juce::Colour(0xff8a8a82), "CLEAR", 14.0f);
    for (int i = 0; i < 4; ++i) // transport keys
    {
        const juce::Rectangle<float> r(560.0f + 54.0f * static_cast<float>(i), 160.0f, 46.0f, 26.0f);
        miniKey(g, r.withWidth(40.0f), std::array<const char*, 4> { "|<", "<<", ">>", ">|" }[static_cast<size_t>(i)], false, juce::Colour(0xff57d8ff));
    }
    key(g, { 900.0f, 52.0f, 120.0f, 46.0f }, "EXPORT MIDI", juce::Colour(0xff3a6fb0));
    key(g, { 1030.0f, 52.0f, 120.0f, 46.0f }, "EXPORT WAV", juce::Colour(0xffb3261e));
    key(g, { 1160.0f, 52.0f, 100.0f, 46.0f }, "DRAG FULL", juce::Colour(0xffc79a3a));
    lcdWindow(g, { 900.0f, 118.0f, 200.0f, 58.0f });
    lcdText(g, { 900.0f, 118.0f, 200.0f, 58.0f }, "92.0 BPM", 30.0f);
    threeWay(g, { 1185.0f, 196.0f }, "TEMPO SOURCE", { "FREE", "LOCK", "SYNC" }, 1);
    // output
    Meter vu;
    vu.bezel = { 1300.0f, 30.0f, 260.0f, 170.0f };
    vu.min = -40.0f;
    vu.max = 6.0f;
    vu.title = "OUTPUT";
    vu.titleSize = 20.0f;
    for (float v : { -40.0f, -30.0f, -20.0f, -10.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f })
        vu.ticks.push_back({ v, v == -40.0f || v == -20.0f || v == -10.0f || v == 0.0f || v == 6.0f });
    vu.seed = 7;
    vu.shown = -9.0f;
    meterFace(g, vu);
    meterNeedle(g, vu);
    lamp(g, { 1275.0f, 60.0f }, true, 0.42f);
    engraved(g, "ON", { 1255.0f, 80.0f, 40.0f, 16.0f }, 11.0f);

    // ---------------------------------------------------------------- generator row
    selector(g, { 150.0f, 290.0f }, 50.0f, { "BOOM BAP", "TRAP", "DnB", "TECHNO" }, 0, -90.0f, 60.0f, "GENRE");
    engraved(g, "SUBSTYLE", { 290.0f, 222.0f, 220.0f, 20.0f }, 15.0f);
    lcdWindow(g, { 300.0f, 246.0f, 200.0f, 46.0f });
    lcdText(g, { 300.0f, 246.0f, 200.0f, 46.0f }, juce::String::fromUTF8("\xe2\x97\x80 CLASSIC \xe2\x96\xb6"), 19.0f);
    selector(g, { 590.0f, 300.0f }, 36.0f, { "1", "2", "4", "8", "16" }, 2, -100.0f, 50.0f, "BARS");
    threeWay(g, { 720.0f, 328.0f }, "TEMPO", { "AUTO", "HALF", "DBL" }, 0);
    knob(g, { 850.0f, 296.0f }, 0.62f, 0.57f, "SWING", "57 %");
    knob(g, { 980.0f, 296.0f }, 0.62f, 0.46f, "DENSITY", "46 %");
    threeWay(g, { 1100.0f, 328.0f }, "SNAP", { "1/8", "1/16", "1/32" }, 1);
    threeWay(g, { 1225.0f, 328.0f }, "PLAYBACK", { "FLAG", "LOOP", "ONCE" }, 0);
    // seed: mechanical counter
    engraved(g, "SEED", { 1330.0f, 222.0f, 210.0f, 20.0f }, 15.0f);
    for (int i = 0; i < 4; ++i)
    {
        const juce::Rectangle<float> d(1352.0f + 42.0f * static_cast<float>(i), 248.0f, 36.0f, 52.0f);
        juce::ColourGradient wheel(juce::Colour(0xff15130f), d.getX(), d.getY(), juce::Colour(0xff15130f), d.getX(), d.getBottom(), false);
        wheel.addColour(0.5, juce::Colour(0xff3a352b));
        g.setGradientFill(wheel);
        g.fillRoundedRectangle(d, 3.0f);
        g.setColour(juce::Colour(0xfff0e6c8));
        g.setFont(monoFont(30.0f));
        g.drawText(juce::String(std::array<int, 4> { 0, 3, 7, 4 }[static_cast<size_t>(i)]), d, juce::Justification::centred);
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawRoundedRectangle(d, 3.0f, 1.5f);
    }
    miniKey(g, { 1360.0f, 312.0f, 66.0f, 22.0f }, "NEW", false, juce::Colour(0xffffb020));
    miniKey(g, { 1450.0f, 312.0f, 66.0f, 22.0f }, "LOCK", true, juce::Colour(0xffff3b2d));

    // ---------------------------------------------------------------- sample analysis (CRT)
    const juce::Rectangle<float> bezel { 40.0f, 372.0f, 1520.0f, 190.0f }, screen { 58.0f, 386.0f, 1484.0f, 140.0f };
    crtBezel(g, bezel, screen, juce::String::fromUTF8("SAMPLE ANALYSIS \xc2\xb7 \xd0\x90\xd0\x9d\xd0\x90\xd0\x9b\xd0\x98\xd0\x97"), bezel.getBottom() - 18.0f);
    crtBegin(g, screen);
    crtText(g, "PATTERN 1_209.WAV", { screen.getX() + 14.0f, screen.getY() + 8.0f, 300.0f, 16.0f }, 13.0f, juce::Justification::centredLeft, 0.9f);
    crtText(g, "92.0 BPM   4 BARS   SWING 64%", { screen.getX() + 14.0f, screen.getY() + 30.0f, 300.0f, 16.0f }, 12.0f, juce::Justification::centredLeft, 0.8f);
    crtText(g, "KEY C MAJOR 100%   BASS 15/15", { screen.getX() + 14.0f, screen.getY() + 50.0f, 300.0f, 16.0f }, 12.0f, juce::Justification::centredLeft, 0.8f);
    crtText(g, "K 31   S 44   H 6", { screen.getX() + 14.0f, screen.getY() + 70.0f, 300.0f, 16.0f }, 12.0f, juce::Justification::centredLeft, 0.8f);
    {
        const float x0 = screen.getX() + 330.0f, x1 = screen.getRight() - 16.0f, mid = screen.getCentreY() + 4.0f;
        juce::Random r(9);
        juce::Path wave;
        for (int i = 0; i <= 1100; ++i)
        {
            const float x = x0 + (x1 - x0) * static_cast<float>(i) / 1100.0f;
            const float env = 0.25f + 0.75f * std::pow(std::abs(std::sin(static_cast<float>(i) * 0.0285f)), 6.0f);
            const float y = mid + (r.nextFloat() - 0.5f) * 70.0f * env;
            if (i == 0)
                wave.startNewSubPath(x, y);
            else
                wave.lineTo(x, y);
        }
        glow(g, wave, phosphorDim, 0.6f, 0.7f);
        for (int b = 0; b <= 4; ++b)
        {
            g.setColour(phosphorDim.withAlpha(0.5f));
            g.drawVerticalLine(juce::roundToInt(x0 + (x1 - x0) * b / 4.0f), screen.getY() + 8.0f, screen.getBottom() - 8.0f);
        }
        const std::array<std::pair<const char*, float>, 3> rows { { { "K", screen.getY() + 26.0f }, { "S", mid }, { "H", screen.getBottom() - 22.0f } } };
        juce::Random hits(31);
        for (const auto& [name, y] : rows)
        {
            crtText(g, name, { x0 - 22.0f, y - 7.0f, 16.0f, 14.0f }, 12.0f, juce::Justification::centred, 0.9f);
            for (int i = 0; i < 64; ++i)
                if (hits.nextFloat() < (name[0] == 'S' ? 0.45f : 0.3f))
                    glowDot(g, { x0 + (x1 - x0) * (static_cast<float>(i) + 0.5f) / 64.0f, y }, 3.2f, name[0] == 'K' ? juce::Colour(0xffffb347) : phosphor, 0.9f);
        }
    }
    crtEnd(g, screen);
    float bx = 380.0f;
    for (const auto& [text, lit] : { std::pair { "OPEN", false }, std::pair { "TRIM", false }, std::pair { "COPY BREAK", true }, std::pair { "GUIDE", false },
                                     std::pair { "ANALYZE + COPY", false }, std::pair { "CLEAR", false } })
    {
        const float w = juce::String(text).length() * 9.0f + 34.0f;
        crtButton(g, { bx, bezel.getBottom() - 30.0f, w, 20.0f }, text, lit);
        bx += w + 12.0f;
    }

    // ---------------------------------------------------------------- instrument rack
    const juce::Rectangle<float> rack { 40.0f, 584.0f, 760.0f, 370.0f };
    engraved(g, "INSTRUMENT RACK", { rack.getX(), rack.getY() - 2.0f, 260.0f, 22.0f }, 17.0f, stencil, juce::Justification::centredLeft);
    key(g, { rack.getRight() - 90.0f, rack.getY() - 4.0f, 90.0f, 26.0f }, "+ LANE", juce::Colour(0xff6b6b63));
    const std::array<const char*, 9> lanes { "HIHAT", "HAT ACCENT", "OPEN HAT", "SNARE", "KICK", "KICK GHOST", "RIDE", "CYMBAL", "SUB 808" };
    for (int i = 0; i < 9; ++i)
    {
        const float y = rack.getY() + 30.0f + 37.0f * static_cast<float>(i);
        const bool child = i == 1 || i == 5;
        // engraved brass name strip
        const juce::Rectangle<float> strip(rack.getX() + (child ? 18.0f : 0.0f), y, child ? 132.0f : 150.0f, 28.0f);
        juce::ColourGradient brass(juce::Colour(0xffcbb27a), strip.getX(), strip.getY(), juce::Colour(0xff7d6532), strip.getX(), strip.getBottom(), false);
        g.setGradientFill(brass);
        g.fillRoundedRectangle(strip, 3.0f);
        g.setColour(juce::Colour(0xff2a2010));
        g.setFont(stencilFont(14.0f).withKerningFactor(0.06f));
        g.drawText(lanes[static_cast<size_t>(i)], strip.reduced(8.0f, 0.0f), juce::Justification::centredLeft);
        if (i == 8)
            for (int k = 0; k < 3; ++k)
                miniKey(g, { strip.getRight() - 72.0f + 22.0f * static_cast<float>(k), y + 5.0f, 18.0f, 18.0f }, juce::String(k + 1), k == 0, juce::Colour(0x00000000));
        miniKey(g, { rack.getX() + 160.0f, y + 2.0f, 34.0f, 24.0f }, "RG", false, juce::Colour(0x00000000));
        miniKey(g, { rack.getX() + 206.0f, y + 2.0f, 24.0f, 24.0f }, "S", i == 3, juce::Colour(0xffffb020));
        miniKey(g, { rack.getX() + 246.0f, y + 2.0f, 24.0f, 24.0f }, "M", i == 6, juce::Colour(0xffff3b2d));
        miniKey(g, { rack.getX() + 286.0f, y + 2.0f, 24.0f, 24.0f }, "L", i == 4, juce::Colour(0xff57d8ff));
        lcdWindow(g, { rack.getX() + 330.0f, y + 2.0f, 230.0f, 24.0f });
        lcdText(g, { rack.getX() + 330.0f, y + 2.0f, 230.0f, 24.0f }, juce::String::fromUTF8("\xe2\x97\x80 ") + std::array<const char*, 9> { "HH_DUSTY_03", "HH_ACC_01", "OH_TAPE_2", "SN_CRACK_07", "KK_VINYL_04", "KK_GHOST_1", "RD_BRASS", "CY_WASH", "808_BOOM_C" }[static_cast<size_t>(i)] + juce::String::fromUTF8(" \xe2\x96\xb6"), 13.0f);
        const juce::Point<float> vol(rack.getX() + 596.0f, y + 14.0f);
        brassKnob(g, vol, (-135.0f + 270.0f * (0.6f + 0.04f * static_cast<float>(i % 4))) * kPi / 180.0f, 0.26f, 1.0f, KnobAsset::AluminumBrassInsert, PointerAsset::WhiteEnamel);
        g.setFont(stencilFont(12.0f));
        g.setColour(stencil);
        g.drawText(juce::String(0.6 + 0.04 * (i % 4), 2) + "x", juce::Rectangle<float>(vol.x + 18.0f, y + 6.0f, 46.0f, 16.0f), juce::Justification::centredLeft);
        key(g, { rack.getRight() - 84.0f, y + 1.0f, 84.0f, 26.0f }, "DRAG", juce::Colour(0xffc79a3a));
    }

    // ---------------------------------------------------------------- pattern: punched card
    const juce::Rectangle<float> card { 830.0f, 584.0f, 730.0f, 370.0f };
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(card.translated(4.0f, 8.0f), 6.0f);
    juce::ColourGradient paper(juce::Colour(0xfff0e2b8), card.getX(), card.getY(), juce::Colour(0xffd8c48e), card.getRight(), card.getBottom(), false);
    g.setGradientFill(paper);
    {
        juce::Path shape; // a punched card: one cut corner
        shape.startNewSubPath(card.getX() + 26.0f, card.getY());
        shape.lineTo(card.getRight(), card.getY());
        shape.lineTo(card.getRight(), card.getBottom());
        shape.lineTo(card.getX(), card.getBottom());
        shape.lineTo(card.getX(), card.getY() + 26.0f);
        shape.closeSubPath();
        g.fillPath(shape);
        juce::Random stains(5);
        for (int i = 0; i < 18; ++i)
        {
            const float x = card.getX() + stains.nextFloat() * card.getWidth(), y = card.getY() + stains.nextFloat() * card.getHeight(), rr = 20.0f + stains.nextFloat() * 70.0f;
            juce::ColourGradient s(rgba(120, 80, 20, 0.08f), x, y, rgba(120, 80, 20, 0.0f), x + rr, y, true);
            g.setGradientFill(s);
            g.fillEllipse(x - rr, y - rr, 2 * rr, 2 * rr);
        }
    }
    const juce::Colour ink(0xff6a3b1e);
    g.setColour(ink);
    g.setFont(stencilFont(17.0f).withKerningFactor(0.12f));
    g.drawText(juce::String::fromUTF8("PATTERN \xc2\xb7 \xd0\x9f\xd0\x95\xd0\xa0\xd0\xa4\xd0\x9e\xd0\x9a\xd0\x90\xd0\xa0\xd0\xa2\xd0\x90"), juce::Rectangle<float>(card.getX() + 40.0f, card.getY() + 8.0f, 400.0f, 20.0f),
               juce::Justification::centredLeft);
    const float gx0 = card.getX() + 120.0f, gx1 = card.getRight() - 18.0f, gy0 = card.getY() + 46.0f, rowH = 34.0f;
    constexpr int steps = 32;
    const float stepW = (gx1 - gx0) / steps;
    for (int s = 0; s <= steps; ++s)
    {
        g.setColour(ink.withAlpha(s % 4 == 0 ? 0.55f : 0.18f));
        g.drawVerticalLine(juce::roundToInt(gx0 + stepW * s), gy0 - 6.0f, gy0 + rowH * 9.0f);
        if (s < steps && s % 4 == 0)
        {
            g.setFont(monoFont(12.0f));
            g.drawText(juce::String(s / 16 + 1) + "." + juce::String((s % 16) / 4 + 1), juce::Rectangle<float>(gx0 + stepW * s + 2.0f, gy0 - 22.0f, 40.0f, 14.0f),
                       juce::Justification::centredLeft);
        }
    }
    const std::array<const char*, 9> pattern { "x.x.x.xxx.x.x.x.x.x.x.xxx.x.x.x.", "........x...............x.......", "......x...............x.........",
                                               "....x.......x.......x.......x.x.", "x.....x...x.x...x.....x..x......", "...x...........x...x............",
                                               "................................", "x...............................", "x.....x...x.......x.....x...x..." };
    for (int r = 0; r < 9; ++r)
    {
        const float y = gy0 + rowH * static_cast<float>(r);
        g.setColour(ink.withAlpha(0.25f));
        g.drawHorizontalLine(juce::roundToInt(y + rowH), card.getX() + 12.0f, gx1);
        g.setColour(ink);
        g.setFont(stencilFont(13.0f));
        g.drawText(lanes[static_cast<size_t>(r)], juce::Rectangle<float>(card.getX() + 14.0f, y + 6.0f, 100.0f, 20.0f), juce::Justification::centredLeft);
        for (int s = 0; s < steps; ++s)
        {
            const float cx = gx0 + stepW * (static_cast<float>(s) + 0.5f), cy = y + rowH * 0.5f;
            if (pattern[static_cast<size_t>(r)][s] == 'x')
            {
                // a punched hole: the dark steel behind shows through, with a torn paper rim
                const juce::Rectangle<float> hole(cx - stepW * 0.32f, cy - 9.0f, stepW * 0.64f, 18.0f);
                g.setColour(juce::Colour(0xff15130f));
                g.fillRoundedRectangle(hole, 2.0f);
                g.setColour(juce::Colours::white.withAlpha(0.35f));
                g.drawRoundedRectangle(hole.translated(0.0f, 1.0f), 2.0f, 0.8f);
                if (r == 8)
                {
                    g.setColour(juce::Colour(0xffff9a2f).withAlpha(0.85f));
                    g.fillRect(hole.reduced(2.0f, 6.0f));
                }
            }
            else
            {
                g.setColour(ink.withAlpha(0.22f));
                g.setFont(monoFont(10.0f));
                g.drawText(juce::String(s % 10), juce::Rectangle<float>(cx - 6.0f, cy - 7.0f, 12.0f, 14.0f), juce::Justification::centred);
            }
        }
    }
    // the play head
    const float px = gx0 + stepW * 11.3f;
    g.setColour(juce::Colour(0xffb3261e).withAlpha(0.85f));
    g.fillRect(px - 1.0f, gy0 - 8.0f, 2.0f, rowH * 9.0f + 8.0f);

    } // the Graphics flushes into the image here
    auto file = out.getChildFile("hpdg_soviet_skin_mockup.png");
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat().writeImageToStream(image, stream);
    std::cout << file.getFullPathName() << "\n";
    return 0;
}
