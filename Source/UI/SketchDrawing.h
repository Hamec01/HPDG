#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbg::sketch
{
float wobbleFor(int seed, float amount = 1.0f) noexcept;
void drawLine(juce::Graphics&, juce::Point<float>, juce::Point<float>, juce::Colour, float thickness, int seed);
void drawFrame(juce::Graphics&, juce::Rectangle<float>, juce::Colour, float thickness, int seed, float corner = 3.0f);
juce::Image makePaperTexture(int width, int height);

// Soft ambient drop shadow used to lift raised UI (buttons, cards, knobs) off the page.
void dropShadow(juce::Graphics&, juce::Rectangle<float> bounds, float corner, float depth = 3.0f, float alphaScale = 1.0f);

// Vertical "raised paper" gradient fill for buttons/cards/panels, from a light top edge to a deeper base.
juce::ColourGradient raisedPaperGradient(juce::Rectangle<float> bounds, juce::Colour top, juce::Colour bottom);
}
