#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbg::sketch
{
struct Theme final
{
    static juce::Colour paper()       { return juce::Colour::fromRGB(239, 233, 216); }
    static juce::Colour paperLight()  { return juce::Colour::fromRGB(248, 244, 232); }
    static juce::Colour paperShadow() { return juce::Colour::fromRGB(213, 204, 184); }
    static juce::Colour paperDeep()   { return juce::Colour::fromRGB(196, 186, 163); }
    static juce::Colour paperEdge()   { return juce::Colour::fromRGB(168, 156, 132); }
    static juce::Colour graphite()    { return juce::Colour::fromRGB(38, 39, 37); }
    static juce::Colour graphiteSoft(){ return juce::Colour::fromRGB(85, 83, 76); }
    static juce::Colour ink()         { return juce::Colour::fromRGB(28, 27, 24); }
    static juce::Colour gridLine()    { return juce::Colour::fromRGBA(77, 87, 87, 48); }
    static juce::Colour ochre()       { return juce::Colour::fromRGB(211, 159, 76); }
    static juce::Colour ochreDeep()   { return juce::Colour::fromRGB(178, 122, 51); }
    static juce::Colour ochreGlow()   { return juce::Colour::fromRGB(247, 202, 128); }
    static juce::Colour ochreWash()   { return juce::Colour::fromRGBA(224, 177, 96, 105); }
    static juce::Colour blue()        { return juce::Colour::fromRGB(116, 150, 175); }
    static juce::Colour blueWash()    { return juce::Colour::fromRGBA(126, 163, 190, 100); }

    // Layered ambient shadow used beneath cards, buttons and knobs so raised
    // elements read as sitting slightly above the paper rather than printed flat on it.
    static juce::Colour shadowSoft()   { return juce::Colour::fromRGBA(46, 38, 26, 46); }
    static juce::Colour shadowStrong() { return juce::Colour::fromRGBA(30, 24, 16, 90); }
    static juce::Colour highlight()    { return juce::Colour::fromRGBA(255, 252, 240, 190); }
};
}
