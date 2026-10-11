#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbg::sketch
{
// UI skins. Sketch: the notebook (paper, graphite, ochre). Steel: a street / trap hardware
// module (dark blue scratched steel, engraved labels, amber LEDs). Dark: a matte, gloomy room
// (charcoal, bone text, dried-blood accent). The skin is a user preference shared by every
// plugin instance (saved in the HamloProd settings file), applied when the editor opens.
enum class Skin
{
    Sketch = 0,
    Steel,
    Dark
};

struct Palette
{
    juce::Colour paper, paperLight, paperShadow, paperDeep, paperEdge;
    juce::Colour graphite, graphiteSoft, ink, gridLine;
    juce::Colour ochre, ochreDeep, ochreGlow, ochreWash;
    juce::Colour blue, blueWash;
    juce::Colour shadowSoft, shadowStrong, highlight;
};

Skin activeSkin() noexcept;
void setActiveSkin(Skin skin) noexcept;
const Palette& activePalette() noexcept;
const char* skinName(Skin skin) noexcept;
std::array<Skin, 3> allSkins() noexcept;
bool isHandDrawn() noexcept; // Sketch: wobbly hand-drawn strokes; the others are machined / straight

// Steel / Dark: a saturated colour picked for the paper skin (orange volume, sky-blue pan, the
// blue / copper drag buttons) takes the skin's own colour of the same temperature: warm -> the
// skin accent (amber / ember red), cool -> its second colour (steel blue / cold grey-teal).
// Sketch, greys and other hues are returned as they are.
juce::Colour toneForSkin(juce::Colour colour);

// User preference (HamloProd settings file); loadSavedSkin() also makes it active,
// loadSavedSkinChoice() only reads it.
Skin loadSavedSkin();
Skin loadSavedSkinChoice();
void saveSkin(Skin skin);

struct Theme final
{
    static juce::Colour paper()       { return activePalette().paper; }
    static juce::Colour paperLight()  { return activePalette().paperLight; }
    static juce::Colour paperShadow() { return activePalette().paperShadow; }
    static juce::Colour paperDeep()   { return activePalette().paperDeep; }
    static juce::Colour paperEdge()   { return activePalette().paperEdge; }
    static juce::Colour graphite()    { return activePalette().graphite; }
    static juce::Colour graphiteSoft(){ return activePalette().graphiteSoft; }
    static juce::Colour ink()         { return activePalette().ink; }
    static juce::Colour gridLine()    { return activePalette().gridLine; }
    static juce::Colour ochre()       { return activePalette().ochre; }
    static juce::Colour ochreDeep()   { return activePalette().ochreDeep; }
    static juce::Colour ochreGlow()   { return activePalette().ochreGlow; }
    static juce::Colour ochreWash()   { return activePalette().ochreWash; }
    static juce::Colour blue()        { return activePalette().blue; }
    static juce::Colour blueWash()    { return activePalette().blueWash; }

    // Layered ambient shadow used beneath cards, buttons and knobs so raised
    // elements read as sitting slightly above the paper rather than printed flat on it.
    static juce::Colour shadowSoft()   { return activePalette().shadowSoft; }
    static juce::Colour shadowStrong() { return activePalette().shadowStrong; }
    static juce::Colour highlight()    { return activePalette().highlight; }
};
}
