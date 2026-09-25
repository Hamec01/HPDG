#pragma once

#include <juce_graphics/juce_graphics.h>

namespace bbg::sketch
{
juce::Typeface::Ptr notebookTypeface();
juce::Font notebookFont(float height, bool bold = false);
}
