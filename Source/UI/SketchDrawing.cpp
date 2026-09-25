#include "SketchDrawing.h"
#include "SketchTheme.h"

namespace bbg::sketch
{
float wobbleFor(int seed, float amount) noexcept
{
    juce::uint32 value = static_cast<juce::uint32>(seed) * 747796405u + 2891336453u;
    value = ((value >> ((value >> 28u) + 4u)) ^ value) * 277803737u;
    value = (value >> 22u) ^ value;
    return (static_cast<float>(value & 1023u) / 511.5f - 1.0f) * amount;
}

void drawLine(juce::Graphics& g, juce::Point<float> from, juce::Point<float> to,
              juce::Colour colour, float thickness, int seed)
{
    g.setColour(colour);
    const auto offset = wobbleFor(seed, 0.55f);
    g.drawLine({ from.x, from.y + offset, to.x, to.y - offset }, thickness);
    g.setColour(colour.withMultipliedAlpha(0.28f));
    g.drawLine({ from.x + wobbleFor(seed + 3, 0.7f), from.y + 0.8f,
                 to.x + wobbleFor(seed + 7, 0.7f), to.y + 0.8f }, juce::jmax(0.45f, thickness * 0.45f));
}

void drawFrame(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour,
               float thickness, int seed, float corner)
{
    bounds = bounds.reduced(thickness * 0.5f + 0.5f);
    g.setColour(colour);
    g.drawRoundedRectangle(bounds.translated(wobbleFor(seed, 0.45f), wobbleFor(seed + 1, 0.35f)), corner, thickness);
    g.setColour(colour.withMultipliedAlpha(0.24f));
    g.drawRoundedRectangle(bounds.reduced(0.8f).translated(wobbleFor(seed + 2, 0.35f), wobbleFor(seed + 3, 0.3f)),
                           corner + 0.5f, juce::jmax(0.45f, thickness * 0.45f));
}

juce::Image makePaperTexture(int width, int height)
{
    juce::Image image(juce::Image::RGB, juce::jmax(1, width), juce::jmax(1, height), true);
    juce::Graphics g(image);
    g.fillAll(Theme::paper());

    for (int y = 0; y < height; y += 3)
    {
        const float alpha = 0.018f + static_cast<float>((y * 17) % 11) * 0.001f;
        g.setColour(Theme::graphite().withAlpha(alpha));
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(width));
    }

    for (int i = 0; i < juce::jmax(80, width * height / 1800); ++i)
    {
        const int x = static_cast<int>((static_cast<juce::uint32>(i) * 2654435761u) % static_cast<juce::uint32>(juce::jmax(1, width)));
        const int y = static_cast<int>((static_cast<juce::uint32>(i + 31) * 2246822519u) % static_cast<juce::uint32>(juce::jmax(1, height)));
        g.setColour(Theme::graphiteSoft().withAlpha(0.035f));
        g.fillEllipse(static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f);
    }
    return image;
}
}
