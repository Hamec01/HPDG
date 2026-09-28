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
    width = juce::jmax(1, width);
    height = juce::jmax(1, height);
    juce::Image image(juce::Image::RGB, width, height, true);
    juce::Graphics g(image);

    // Base sheet: a gentle diagonal light gradient rather than a flat tint, so the
    // page feels lit rather than printed.
    juce::ColourGradient base(Theme::paperLight(), width * 0.18f, height * 0.0f,
                              Theme::paperDeep(), width * 0.85f, static_cast<float>(height), false);
    base.addColour(0.55, Theme::paper());
    g.setGradientFill(base);
    g.fillAll();

    // Fine horizontal fiber lines with irregular spacing/opacity (hand-laid paper grain).
    for (int y = 0; y < height; y += 3)
    {
        const float jitter = wobbleFor(y * 7 + 11, 1.0f);
        const float alpha = 0.014f + std::abs(jitter) * 0.014f;
        g.setColour(Theme::graphite().withAlpha(alpha));
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(width));
    }

    // Faint long fibers: short diagonal hairline strokes scattered across the sheet.
    const int fiberCount = juce::jmax(40, width * height / 9000);
    for (int i = 0; i < fiberCount; ++i)
    {
        const juce::uint32 h1 = static_cast<juce::uint32>(i) * 2654435761u + 12345u;
        const juce::uint32 h2 = static_cast<juce::uint32>(i) * 2246822519u + 6789u;
        const float x = static_cast<float>(h1 % static_cast<juce::uint32>(width));
        const float y = static_cast<float>(h2 % static_cast<juce::uint32>(height));
        const float len = 3.0f + wobbleFor(i * 13, 1.0f) * 3.0f;
        const float angle = wobbleFor(i * 29, 1.0f) * 0.5f;
        g.setColour(Theme::graphiteSoft().withAlpha(0.02f + std::abs(wobbleFor(i * 3, 1.0f)) * 0.018f));
        g.drawLine(x, y, x + len * std::cos(angle), y + len * std::sin(angle), 0.6f);
    }

    // Dust / grain speckle.
    for (int i = 0; i < juce::jmax(80, width * height / 1800); ++i)
    {
        const int x = static_cast<int>((static_cast<juce::uint32>(i) * 2654435761u) % static_cast<juce::uint32>(width));
        const int y = static_cast<int>((static_cast<juce::uint32>(i + 31) * 2246822519u) % static_cast<juce::uint32>(height));
        g.setColour(Theme::graphiteSoft().withAlpha(0.035f));
        g.fillEllipse(static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f);
    }

    // Soft vignette: the page edges settle into a slightly deeper tone.
    const float radius = std::sqrt(static_cast<float>(width) * width + static_cast<float>(height) * height) * 0.62f;
    juce::ColourGradient vignette(juce::Colours::transparentBlack, width * 0.5f, height * 0.5f,
                                  Theme::paperEdge().withAlpha(0.30f), width * 0.5f, height * 0.5f - radius, true);
    g.setGradientFill(vignette);
    g.fillAll();

    return image;
}

void dropShadow(juce::Graphics& g, juce::Rectangle<float> bounds, float corner, float depth, float alphaScale)
{
    const auto shadowBounds = bounds.translated(0.0f, depth * 0.7f).expanded(depth * 0.25f, 0.0f);
    g.setColour(Theme::shadowSoft().withMultipliedAlpha(alphaScale));
    g.fillRoundedRectangle(shadowBounds, corner + depth * 0.3f);
    g.setColour(Theme::shadowStrong().withMultipliedAlpha(alphaScale * 0.5f));
    g.fillRoundedRectangle(bounds.translated(0.0f, depth * 0.35f), corner);
}

juce::ColourGradient raisedPaperGradient(juce::Rectangle<float> bounds, juce::Colour top, juce::Colour bottom)
{
    return juce::ColourGradient(top, bounds.getX(), bounds.getY(),
                                bottom, bounds.getX(), bounds.getBottom(), false);
}
}
