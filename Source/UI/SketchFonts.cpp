#include "SketchFonts.h"

#include <BinaryData.h>

namespace bbg::sketch
{
juce::Typeface::Ptr notebookTypeface()
{
    static auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::Neucha_ttf,
                                                                BinaryData::Neucha_ttfSize);
    return face;
}

juce::Font notebookFont(float height, bool bold)
{
    juce::Font font { juce::FontOptions(notebookTypeface()) };
    font.setHeight(height);
    juce::ignoreUnused(bold); // Neucha has one native weight; synthetic bold damages its handwritten shapes.
    return font;
}
}
