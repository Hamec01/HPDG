#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbg::sketch
{
struct Theme final
{
    static juce::Colour paper()       { return juce::Colour::fromRGB(239, 233, 216); }
    static juce::Colour paperLight()  { return juce::Colour::fromRGB(248, 244, 232); }
    static juce::Colour paperShadow() { return juce::Colour::fromRGB(213, 204, 184); }
    static juce::Colour graphite()    { return juce::Colour::fromRGB(38, 39, 37); }
    static juce::Colour graphiteSoft(){ return juce::Colour::fromRGB(85, 83, 76); }
    static juce::Colour gridLine()    { return juce::Colour::fromRGBA(77, 87, 87, 48); }
    static juce::Colour ochre()       { return juce::Colour::fromRGB(211, 159, 76); }
    static juce::Colour ochreWash()   { return juce::Colour::fromRGBA(224, 177, 96, 105); }
    static juce::Colour blue()        { return juce::Colour::fromRGB(116, 150, 175); }
    static juce::Colour blueWash()    { return juce::Colour::fromRGBA(126, 163, 190, 100); }
};
}
