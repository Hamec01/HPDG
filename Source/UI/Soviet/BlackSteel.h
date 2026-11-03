#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbg::ui::steel
{
// Drawing pieces of the owner's "Black Steel" style (HamloComp, hamlocomp.zip), shared by the module
// pages after the compressor. Sizes are in the page's design coordinates; `s` scales a piece around its
// centre (1 = the HamloComp size).
const juce::Colour stencil { 0xffe6d8a6 }, stencilDim { 0xffa89c76 };

juce::FontOptions stencilFont(float size, bool bold = true);
juce::FontOptions monoFont(float size);
juce::Colour rgba(int r, int g, int b, float a);

void engraved(juce::Graphics&, const juce::String& text, juce::Rectangle<float> r, float size, juce::Colour colour = stencil,
              juce::Justification just = juce::Justification::centred, float spacing = 0.12f);
void hexagon(juce::Graphics&, juce::Point<float> c, float w, float h, juce::Colour light, juce::Colour dark);

// Frame, worn steel plate (mottling, grain, smudges, rust, chips, scratches, pitting), vignette, bolts.
void panel(juce::Graphics&, float width, float height, int seed);
// Brass-grey nameplate with rivets, red star and serial number.
void nameplate(juce::Graphics&, juce::Rectangle<float> plate, const juce::String& title, const juce::String& model,
               const juce::String& number);
// Engraved 21-tick scale (270 degrees) and the recessed ring a knob sits in.
void knobScale(juce::Graphics&, juce::Point<float> c, float s);

enum class KnobAsset
{
    AluminumBrassInsert,
    BakeliteRibbed,
    ChippedIronDark,
    LatheSteelRust
};

enum class PointerAsset
{
    WhiteEnamel,
    RedTipBrassCollar,
    BrassWedge,
    BlackInstrument
};

enum class LeverPosition
{
    Auto,
    Up,
    Neutral,
    Down
};

// Owner-supplied knob body and pointer. `angle` is in radians (0 = up), `s` scales around the
// centre, and `alpha` keeps the old dimmed/bypassed appearance. Defaults retain the old call sites.
void brassKnob(juce::Graphics&, juce::Point<float> c, float angle, float s, float alpha = 1.0f,
               KnobAsset knob = KnobAsset::AluminumBrassInsert, PointerAsset pointer = PointerAsset::WhiteEnamel);
// Owner-supplied TP1-2 plate plus up/neutral/down lever; `angle` in radians for automatic state selection.
void lever(juce::Graphics&, juce::Point<float> base, float angle, float s, LeverPosition position = LeverPosition::Auto);
// Red pilot lamp in a grey bezel.
void lamp(juce::Graphics&, juce::Point<float> c, bool on, float s, juce::Colour colour = juce::Colour(0xffff3b2d));
// Amber LCD window and its text.
void lcdWindow(juce::Graphics&, juce::Rectangle<float>);
void lcdText(juce::Graphics&, juce::Rectangle<float>, const juce::String&, float size, juce::Justification = juce::Justification::centred);

// Scratched pictures (owner's USSR set, Assets/scratches): `picture` 0..numScratchPictures()-1 is cut into
// the steel inside `area` (fitted, kept in proportion), turned by `degrees`: a dark groove and a pale edge.
struct Scratch
{
    int picture;
    juce::Rectangle<float> area;
    float degrees = 0.0f;
};
int numScratchPictures();
void scratches(juce::Graphics&, std::initializer_list<Scratch>, float strength = 1.0f);

// Brass-bezel needle meter with aged dial paper (HamloComp's meters).
struct Meter
{
    juce::Rectangle<float> bezel;
    float min = -40.0f, max = 6.0f, redFrom = 0.0f;
    juce::String title, unit { "dB" };
    float titleSize = 21.0f;
    std::vector<std::pair<float, bool>> ticks; // value, labelled
    int seed = 11;
    float shown = -42.0f;
};
void meterFace(juce::Graphics&, const Meter&);
void meterNeedle(juce::Graphics&, const Meter&);

// Green phosphor CRT in a gunmetal bezel (HamloVerb, HamloEcho).
const juce::Colour phosphor { 0xff6dff9a }, phosphorDim { 0xff2f8a52 };
// Static part: bezel with screws, the recess around `screen`, an engraved label at `labelY`.
void crtBezel(juce::Graphics&, juce::Rectangle<float> bezel, juce::Rectangle<float> screen, const juce::String& label, float labelY);
// crtBegin clips to the glass and fills the tube; draw the picture; crtEnd adds scanlines, edge
// falloff and glare, and restores the clip.
void crtBegin(juce::Graphics&, juce::Rectangle<float> screen);
void crtEnd(juce::Graphics&, juce::Rectangle<float> screen);
// An illuminated push button on the bezel.
void crtButton(juce::Graphics&, juce::Rectangle<float>, const juce::String& text, bool lit);
// Phosphor strokes: bloom passes, a bright core and a white-hot centre line.
void glow(juce::Graphics&, const juce::Path&, juce::Colour, float width, float intensity = 1.0f);
void glowDot(juce::Graphics&, juce::Point<float> c, float r, juce::Colour, float intensity = 1.0f);
void crtText(juce::Graphics&, const juce::String&, juce::Rectangle<float>, float size, juce::Justification, float alpha = 0.8f);
} // namespace bbg::ui::steel
