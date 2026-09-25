#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbg::sketch
{
float wobbleFor(int seed, float amount = 1.0f) noexcept;
void drawLine(juce::Graphics&, juce::Point<float>, juce::Point<float>, juce::Colour, float thickness, int seed);
void drawFrame(juce::Graphics&, juce::Rectangle<float>, juce::Colour, float thickness, int seed, float corner = 3.0f);
juce::Image makePaperTexture(int width, int height);
}
