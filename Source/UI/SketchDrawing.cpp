#include "SketchDrawing.h"
#include "SketchTheme.h"

#include <atomic>
#include <juce_data_structures/juce_data_structures.h>

namespace bbg::sketch
{
namespace
{
using C = juce::Colour;

const Palette& sketchPalette()
{
    static const Palette p {
        C::fromRGB(239, 233, 216), C::fromRGB(248, 244, 232), C::fromRGB(213, 204, 184), C::fromRGB(196, 186, 163), C::fromRGB(168, 156, 132),
        C::fromRGB(38, 39, 37), C::fromRGB(85, 83, 76), C::fromRGB(28, 27, 24), C::fromRGBA(77, 87, 87, 48),
        C::fromRGB(211, 159, 76), C::fromRGB(178, 122, 51), C::fromRGB(247, 202, 128), C::fromRGBA(224, 177, 96, 105),
        C::fromRGB(116, 150, 175), C::fromRGBA(126, 163, 190, 100),
        C::fromRGBA(46, 38, 26, 46), C::fromRGBA(30, 24, 16, 90), C::fromRGBA(255, 252, 240, 190)
    };
    return p;
}

// Blued steel: plates in dark navy, pale-steel engraving, worn amber indicator lamps.
const Palette& steelPalette()
{
    static const Palette p {
        C::fromRGB(30, 41, 55), C::fromRGB(45, 59, 77), C::fromRGB(23, 32, 43), C::fromRGB(17, 24, 33), C::fromRGB(9, 13, 19),
        C::fromRGB(206, 217, 228), C::fromRGB(134, 150, 168), C::fromRGB(236, 241, 246), C::fromRGBA(150, 172, 196, 34),
        C::fromRGB(214, 146, 52), C::fromRGB(156, 98, 30), C::fromRGB(255, 196, 104), C::fromRGBA(214, 146, 52, 80),
        C::fromRGB(98, 142, 182), C::fromRGBA(98, 142, 182, 80),
        C::fromRGBA(0, 0, 0, 90), C::fromRGBA(0, 0, 0, 150), C::fromRGBA(190, 210, 230, 46)
    };
    return p;
}

// A dark room: matte charcoal, bone-coloured text, a dried-blood accent, cold grey second accent.
const Palette& darkPalette()
{
    static const Palette p {
        C::fromRGB(20, 19, 20), C::fromRGB(32, 30, 32), C::fromRGB(14, 13, 14), C::fromRGB(10, 9, 10), C::fromRGB(5, 5, 5),
        C::fromRGB(196, 188, 176), C::fromRGB(118, 111, 104), C::fromRGB(226, 219, 207), C::fromRGBA(200, 188, 176, 24),
        C::fromRGB(150, 44, 38), C::fromRGB(98, 28, 25), C::fromRGB(196, 72, 60), C::fromRGBA(150, 44, 38, 80),
        C::fromRGB(84, 104, 106), C::fromRGBA(84, 104, 106, 80),
        C::fromRGBA(0, 0, 0, 110), C::fromRGBA(0, 0, 0, 170), C::fromRGBA(255, 240, 225, 18)
    };
    return p;
}

std::atomic<int> currentSkin { 0 };

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "HPDG";
    options.folderName = "HamloProd";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    return options;
}
}

Skin activeSkin() noexcept { return static_cast<Skin>(currentSkin.load()); }
void setActiveSkin(Skin skin) noexcept { currentSkin.store(static_cast<int>(skin)); }

const Palette& activePalette() noexcept
{
    switch (activeSkin())
    {
        case Skin::Steel: return steelPalette();
        case Skin::Dark: return darkPalette();
        case Skin::Sketch:
        default: return sketchPalette();
    }
}

const char* skinName(Skin skin) noexcept
{
    switch (skin)
    {
        case Skin::Steel: return "Steel";
        case Skin::Dark: return "Dark";
        case Skin::Sketch:
        default: return "Sketch";
    }
}

std::array<Skin, 3> allSkins() noexcept { return { Skin::Sketch, Skin::Steel, Skin::Dark }; }
bool isHandDrawn() noexcept { return activeSkin() == Skin::Sketch; }

juce::Colour toneForSkin(juce::Colour colour)
{
    if (isHandDrawn() || colour.getSaturation() < 0.25f || colour.isTransparent())
        return colour;
    const float hue = colour.getHue();
    const bool warm = hue < 0.17f || hue > 0.92f;
    const bool cool = hue > 0.45f && hue < 0.75f;
    if (!warm && !cool)
        return colour;
    const auto target = warm ? Theme::ochre() : Theme::blue();
    // the gloomy skin keeps its accents low; steel lets lamps glow
    const float lift = activeSkin() == Skin::Dark ? 0.45f + 0.35f * colour.getBrightness() : 0.55f + 0.6f * colour.getBrightness();
    return target.withBrightness(juce::jlimit(0.18f, 1.0f, target.getBrightness() * lift)).withAlpha(colour.getFloatAlpha());
}

Skin loadSavedSkinChoice()
{
    juce::PropertiesFile settings(settingsOptions());
    return static_cast<Skin>(juce::jlimit(0, 2, settings.getIntValue("ui_skin", 0)));
}

Skin loadSavedSkin()
{
    setActiveSkin(loadSavedSkinChoice());
    return activeSkin();
}

void saveSkin(Skin skin)
{
    juce::PropertiesFile settings(settingsOptions());
    settings.setValue("ui_skin", static_cast<int>(skin));
    settings.saveIfNeeded();
}

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
    if (!isHandDrawn())
    {
        // machined: one straight stroke, no pencil wobble
        g.setColour(colour);
        g.drawLine({ from, to }, thickness);
        return;
    }
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
    if (!isHandDrawn())
    {
        // machined edge: the outline plus a faint light line inside it (a bevel catching light)
        const float machinedCorner = activeSkin() == Skin::Steel ? juce::jmin(corner, 2.5f) : juce::jmin(corner, 4.0f);
        g.setColour(colour);
        g.drawRoundedRectangle(bounds, machinedCorner, thickness);
        g.setColour(Theme::highlight().withMultipliedAlpha(0.5f));
        g.drawRoundedRectangle(bounds.reduced(thickness + 0.4f), juce::jmax(0.0f, machinedCorner - 1.0f), 0.6f);
        return;
    }
    g.setColour(colour);
    g.drawRoundedRectangle(bounds.translated(wobbleFor(seed, 0.45f), wobbleFor(seed + 1, 0.35f)), corner, thickness);
    g.setColour(colour.withMultipliedAlpha(0.24f));
    g.drawRoundedRectangle(bounds.reduced(0.8f).translated(wobbleFor(seed + 2, 0.35f), wobbleFor(seed + 3, 0.3f)),
                           corner + 0.5f, juce::jmax(0.45f, thickness * 0.45f));
}

namespace
{
juce::uint32 hashOf(juce::uint32 value) noexcept
{
    value ^= value >> 16; value *= 0x7feb352du; value ^= value >> 15; value *= 0x846ca68bu; value ^= value >> 16;
    return value;
}
float unit(juce::uint32 seed) noexcept { return static_cast<float>(hashOf(seed) & 0xffffffu) / 16777215.0f; }
float unit(int seed) noexcept { return unit(static_cast<juce::uint32>(seed)); }

// Blued steel sheet: brushed grain (long horizontal strokes of varying tone), uneven bluing,
// scratches in all directions (fine light ones and a few deeper gouges with a dark lip), rub
// marks, and worn edges that catch more light.
juce::Image makeSteelTexture(int width, int height)
{
    juce::Image image(juce::Image::RGB, width, height, true);
    juce::Graphics g(image);
    juce::ColourGradient base(Theme::paperLight(), 0.0f, 0.0f, Theme::paperDeep(), width * 0.9f, static_cast<float>(height), false);
    base.addColour(0.45, Theme::paper());
    g.setGradientFill(base);
    g.fillAll();

    // uneven bluing / oxidation: large soft blotches
    for (int i = 0; i < 18; ++i)
    {
        const float cx = unit(i * 3 + 1) * width, cy = unit(i * 3 + 2) * height;
        const float r = (0.12f + 0.25f * unit(i * 3 + 3)) * static_cast<float>(juce::jmax(width, height));
        const bool dark = (hashOf(static_cast<juce::uint32>(i + 99)) & 1u) != 0;
        juce::ColourGradient blot((dark ? juce::Colours::black : juce::Colour::fromRGB(120, 150, 185)).withAlpha(dark ? 0.16f : 0.06f), cx, cy,
                                  juce::Colours::transparentBlack, cx + r, cy, true);
        g.setGradientFill(blot);
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
    }

    // brushed grain
    for (int y = 0; y < height; ++y)
    {
        const float t = unit(y * 7919);
        g.setColour((t > 0.5f ? juce::Colour::fromRGB(170, 190, 215) : juce::Colours::black).withAlpha(0.025f + 0.05f * std::abs(t - 0.5f)));
        float x = -unit(y + 5) * 200.0f;
        while (x < width)
        {
            const float len = 60.0f + unit(y * 131 + static_cast<int>(x)) * 380.0f;
            g.drawHorizontalLine(y, juce::jmax(0.0f, x), juce::jmin(static_cast<float>(width), x + len));
            x += len + unit(y * 17 + static_cast<int>(x)) * 90.0f;
        }
    }

    // scratches
    const int scratches = juce::jmax(60, width * height / 2600);
    for (int i = 0; i < scratches; ++i)
    {
        const int s = i * 7 + 1000;
        const float x = unit(s + 1) * width, y = unit(s + 2) * height;
        const bool longOne = unit(s + 3) > 0.86f;
        const float len = longOne ? 40.0f + unit(s + 4) * 160.0f : 4.0f + unit(s + 4) * 26.0f;
        const float angle = (unit(s + 5) - 0.5f) * (unit(s + 6) > 0.6f ? 3.1f : 0.7f);
        const float bend = (unit(s + 7) - 0.5f) * len * 0.12f;
        juce::Path path;
        path.startNewSubPath(x, y);
        path.quadraticTo(x + std::cos(angle) * len * 0.5f - std::sin(angle) * bend, y + std::sin(angle) * len * 0.5f + std::cos(angle) * bend,
                         x + std::cos(angle) * len, y + std::sin(angle) * len);
        if (longOne && unit(s + 8) > 0.5f)
        {
            // a gouge: dark lip under a bright edge
            g.setColour(juce::Colours::black.withAlpha(0.30f));
            g.strokePath(path, juce::PathStrokeType(1.3f), juce::AffineTransform::translation(0.0f, 0.8f));
            g.setColour(juce::Colour::fromRGB(205, 220, 236).withAlpha(0.22f));
            g.strokePath(path, juce::PathStrokeType(0.7f));
        }
        else
        {
            g.setColour(juce::Colour::fromRGB(200, 215, 232).withAlpha(0.05f + 0.11f * unit(s + 9)));
            g.strokePath(path, juce::PathStrokeType(0.5f + 0.4f * unit(s + 10)));
        }
    }

    // rub marks: tight clusters of short parallel strokes
    for (int c = 0; c < juce::jmax(4, width * height / 120000); ++c)
    {
        const float cx = unit(c * 11 + 400) * width, cy = unit(c * 11 + 401) * height;
        const float angle = (unit(c * 11 + 402) - 0.5f) * 1.2f;
        for (int k = 0; k < 24; ++k)
        {
            const float ox = (unit(c * 97 + k) - 0.5f) * 40.0f, oy = (unit(c * 89 + k) - 0.5f) * 14.0f;
            const float len = 8.0f + unit(c * 83 + k) * 22.0f;
            g.setColour(juce::Colour::fromRGB(190, 208, 228).withAlpha(0.05f));
            g.drawLine(cx + ox, cy + oy, cx + ox + std::cos(angle) * len, cy + oy + std::sin(angle) * len, 0.6f);
        }
    }

    g.setColour(juce::Colour::fromRGB(150, 175, 205).withAlpha(0.10f));
    g.drawRect(0, 0, width, height, 2);
    return image;
}

// Matte charcoal: fine film grain, a few dull smudges, a heavy vignette.
juce::Image makeDarkTexture(int width, int height)
{
    juce::Image image(juce::Image::RGB, width, height, true);
    {
        juce::Graphics g(image);
        g.fillAll(Theme::paper());
        for (int i = 0; i < 10; ++i)
        {
            const float cx = unit(i * 5 + 700) * width, cy = unit(i * 5 + 701) * height;
            const float r = (0.15f + 0.3f * unit(i * 5 + 702)) * static_cast<float>(juce::jmax(width, height));
            juce::ColourGradient smudge(juce::Colour::fromRGB(60, 52, 48).withAlpha(0.10f), cx, cy, juce::Colours::transparentBlack, cx + r, cy, true);
            g.setGradientFill(smudge);
            g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        }
    }
    {
        juce::Image::BitmapData data(image, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const float n = (unit(y * width + x + 17) - 0.5f) * 9.0f;
                const auto c = data.getPixelColour(x, y);
                auto channel = [n](juce::uint8 v) { return static_cast<juce::uint8>(juce::jlimit(0.0f, 255.0f, v + n)); };
                data.setPixelColour(x, y, juce::Colour::fromRGB(channel(c.getRed()), channel(c.getGreen()), channel(c.getBlue())));
            }
    }
    juce::Graphics g(image);
    const float radius = std::sqrt(static_cast<float>(width) * width + static_cast<float>(height) * height) * 0.55f;
    juce::ColourGradient vignette(juce::Colours::transparentBlack, width * 0.5f, height * 0.45f,
                                  juce::Colours::black.withAlpha(0.55f), width * 0.5f, height * 0.45f - radius, true);
    g.setGradientFill(vignette);
    g.fillAll();
    return image;
}
}

juce::Image makePaperTexture(int width, int height)
{
    width = juce::jmax(1, width);
    height = juce::jmax(1, height);
    if (activeSkin() == Skin::Steel)
        return makeSteelTexture(width, height);
    if (activeSkin() == Skin::Dark)
        return makeDarkTexture(width, height);
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

namespace
{
// A tile of the skin's texture, kept in JUCE's ImageCache (cleared with JUCE's own shutdown; a
// static juce::Image would be released inside DllMain and can deadlock the host).
juce::Image textureTile()
{
    const juce::int64 key = 0x48504447000000LL + static_cast<juce::int64>(activeSkin());
    auto tile = juce::ImageCache::getFromHashCode(key);
    if (!tile.isValid())
    {
        tile = makePaperTexture(640, 480);
        juce::ImageCache::addImageToCache(tile, key);
    }
    return tile;
}

void drawScrew(juce::Graphics& g, juce::Point<float> centre, float radius, int seed)
{
    // recess, head, a cross slot at a random turn
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillEllipse(centre.x - radius - 1.0f, centre.y - radius - 0.4f, (radius + 1.0f) * 2.0f, (radius + 1.0f) * 2.0f);
    juce::ColourGradient head(juce::Colour::fromRGB(170, 184, 200), centre.x - radius * 0.6f, centre.y - radius * 0.7f,
                              juce::Colour::fromRGB(52, 62, 76), centre.x + radius * 0.7f, centre.y + radius * 0.8f, false);
    g.setGradientFill(head);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    const float turn = wobbleFor(seed, 1.0f) * 1.2f;
    for (const float a : { turn, turn + juce::MathConstants<float>::halfPi })
    {
        const juce::Point<float> d(std::cos(a) * radius * 0.7f, std::sin(a) * radius * 0.7f);
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawLine({ centre - d, centre + d }, 1.3f);
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawLine({ centre - d + juce::Point<float>(0.0f, 0.9f), centre + d + juce::Point<float>(0.0f, 0.9f) }, 0.5f);
    }
}
}

void fillPanel(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour, float corner, bool screws)
{
    if (isHandDrawn())
    {
        g.setColour(colour);
        g.fillRoundedRectangle(bounds, corner);
        return;
    }

    const bool steel = activeSkin() == Skin::Steel;
    corner = juce::jmin(corner, steel ? 2.5f : 4.0f);
    g.setColour(colour);
    g.fillRoundedRectangle(bounds, corner);
    g.setTiledImageFill(textureTile(), static_cast<int>(bounds.getX()), static_cast<int>(bounds.getY()), steel ? 0.8f : 0.45f);
    g.fillRoundedRectangle(bounds, corner);

    if (steel)
    {
        // bevel: light along the top / left, dark along the bottom / right
        g.setColour(juce::Colour::fromRGB(190, 210, 232).withAlpha(0.22f));
        g.drawLine(bounds.getX() + corner, bounds.getY() + 0.7f, bounds.getRight() - corner, bounds.getY() + 0.7f, 1.0f);
        g.drawLine(bounds.getX() + 0.7f, bounds.getY() + corner, bounds.getX() + 0.7f, bounds.getBottom() - corner, 1.0f);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawLine(bounds.getX() + corner, bounds.getBottom() - 0.7f, bounds.getRight() - corner, bounds.getBottom() - 0.7f, 1.2f);
        g.drawLine(bounds.getRight() - 0.7f, bounds.getY() + corner, bounds.getRight() - 0.7f, bounds.getBottom() - corner, 1.2f);
        if (screws && bounds.getWidth() > 160.0f && bounds.getHeight() > 60.0f)
        {
            const float r = 3.6f, inset = 8.0f;
            const int seed = static_cast<int>(bounds.getX() * 13.0f + bounds.getY() * 7.0f);
            drawScrew(g, { bounds.getX() + inset, bounds.getY() + inset }, r, seed);
            drawScrew(g, { bounds.getRight() - inset, bounds.getY() + inset }, r, seed + 1);
            drawScrew(g, { bounds.getX() + inset, bounds.getBottom() - inset }, r, seed + 2);
            drawScrew(g, { bounds.getRight() - inset, bounds.getBottom() - inset }, r, seed + 3);
        }
    }
    else
    {
        // matte: a faint inner shadow along the top edge
        juce::ColourGradient shade(juce::Colours::black.withAlpha(0.35f), 0.0f, bounds.getY(),
                                   juce::Colours::transparentBlack, 0.0f, bounds.getY() + 10.0f, false);
        g.setGradientFill(shade);
        g.fillRoundedRectangle(bounds.withHeight(juce::jmin(10.0f, bounds.getHeight())), corner);
    }
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
