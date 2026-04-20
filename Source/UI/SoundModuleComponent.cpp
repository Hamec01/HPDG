#include "SoundModuleComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <juce_dsp/juce_dsp.h>

#include "../Core/TrackRegistry.h"

namespace bbg
{
namespace
{
juce::String displayNameForTrackType(TrackType type)
{
    if (const auto* info = TrackRegistry::find(type); info != nullptr)
        return info->displayName;

    return "Track";
}

juce::String displayNameForDescriptor(const SoundTargetDescriptor& descriptor)
{
    if (descriptor.kind == SoundTargetDescriptorKind::BackedRuntimeLane && descriptor.runtimeTrackType.has_value())
        return displayNameForTrackType(*descriptor.runtimeTrackType);

    if (descriptor.kind == SoundTargetDescriptorKind::LegacyTrackTypeAlias && descriptor.legacyTrackTypeAlias.has_value())
        return displayNameForTrackType(*descriptor.legacyTrackTypeAlias);

    return "Global";
}

bool comboContainsDescriptor(const std::vector<SoundTargetDescriptor>& descriptors, const SoundTargetDescriptor& target)
{
    return std::find(descriptors.begin(), descriptors.end(), target) != descriptors.end();
}

juce::String formatDbValue(float value)
{
    if (std::abs(value) < 0.05f)
        return "0.0 dB";

    const juce::String sign = value > 0.0f ? "+" : "";
    return sign + juce::String(value, 1) + " dB";
}

juce::String formatFreqValue(double value)
{
    if (value >= 1000.0)
        return juce::String(value / 1000.0, value >= 10000.0 ? 1 : 2) + " kHz";

    return juce::String(juce::roundToInt(value)) + " Hz";
}

juce::String formatRatioValue(double value)
{
    return juce::String(value, 1) + ":1";
}

juce::String formatMilliseconds(double value)
{
    return juce::String(value, value < 10.0 ? 1 : 0) + " ms";
}

juce::String formatPercentValue(double value)
{
    return juce::String(juce::roundToInt(value)) + "%";
}

juce::String formatPanValue(float value)
{
    if (std::abs(value) < 0.01f)
        return "Center";

    return value < 0.0f ? "L " + juce::String(juce::roundToInt(std::abs(value) * 100.0f))
                        : "R " + juce::String(juce::roundToInt(std::abs(value) * 100.0f));
}

juce::String formatWidthValue(float value)
{
    if (value < 0.05f)
        return "Mono";
    if (std::abs(value - 1.0f) < 0.04f)
        return "Std 100%";
    if (value < 1.0f)
        return "Narrow " + juce::String(juce::roundToInt(value * 100.0f)) + "%";
    return "Wide " + juce::String(juce::roundToInt(value * 100.0f)) + "%";
}

int compressorModeSelectionId(const CompressorState& compressor)
{
    const bool postEq = compressor.order > 1;
    const bool punch = compressor.character == DrumCompressorCharacter::Punch;

    if (!postEq && !punch)
        return 1;
    if (!postEq && punch)
        return 2;
    if (postEq && !punch)
        return 3;
    return 4;
}

void applyCompressorModeSelection(int selectionId, CompressorState& compressor)
{
    switch (selectionId)
    {
        case 2:
            compressor.order = 1;
            compressor.character = DrumCompressorCharacter::Punch;
            compressor.saturationMode = DrumSaturationMode::Punch;
            break;
        case 3:
            compressor.order = 2;
            compressor.character = DrumCompressorCharacter::Glue;
            compressor.saturationMode = DrumSaturationMode::Warm;
            break;
        case 4:
            compressor.order = 2;
            compressor.character = DrumCompressorCharacter::Punch;
            compressor.saturationMode = DrumSaturationMode::Punch;
            break;
        case 1:
        default:
            compressor.order = 1;
            compressor.character = DrumCompressorCharacter::Glue;
            compressor.saturationMode = DrumSaturationMode::Warm;
            break;
    }
}

juce::String compressorDescriptorText(const CompressorState& compressor)
{
    const juce::String character = compressor.character == DrumCompressorCharacter::Punch ? "Punch contour" : "Glue contour";
    const juce::String saturation = compressor.saturationMode == DrumSaturationMode::Punch ? "Punch sat" : "Warm sat";
    const juce::String makeup = compressor.autoMakeup ? "Auto level" : "Manual level";
    return character + " / " + saturation + " / " + makeup;
}

double reverbUiPercent(float normalizedValue)
{
    return juce::jlimit(0.0, 100.0, static_cast<double>(normalizedValue) * 100.0);
}

float reverbNormalized(double percentValue)
{
    return juce::jlimit(0.0f, 1.0f, static_cast<float>(percentValue / 100.0));
}

juce::Colour shellBase() { return juce::Colour::fromRGB(8, 8, 8); }
juce::Colour shellRaised() { return juce::Colour::fromRGB(16, 14, 12); }
juce::Colour panelBase() { return juce::Colour::fromRGB(24, 21, 18); }
juce::Colour panelRaised() { return juce::Colour::fromRGB(34, 29, 25); }
juce::Colour panelInset() { return juce::Colour::fromRGB(12, 11, 10); }
juce::Colour copper() { return juce::Colour::fromRGB(185, 118, 61); }
juce::Colour amber() { return juce::Colour::fromRGB(232, 176, 96); }
juce::Colour amberBright() { return juce::Colour::fromRGB(252, 214, 143); }
juce::Colour steel() { return juce::Colour::fromRGB(179, 184, 190); }
juce::Colour textMain() { return juce::Colour::fromRGB(233, 226, 214); }
juce::Colour textMuted() { return juce::Colour::fromRGB(145, 136, 124); }
juce::Colour danger() { return juce::Colour::fromRGB(197, 102, 86); }
juce::Colour success() { return juce::Colour::fromRGB(150, 191, 133); }

struct HardwareLookAndFeel : juce::LookAndFeel_V4
{
    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions(10.5f, juce::Font::bold));
    }

    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override
    {
        const auto role = button.getProperties().getWithDefault("soundRole", juce::String()).toString();
        const float size = role == "strip" ? 10.0f : juce::jlimit(10.0f, 11.5f, static_cast<float>(buttonHeight) * 0.38f);
        return juce::Font(juce::FontOptions(size, juce::Font::bold));
    }

    void drawComboBox(juce::Graphics& g,
                      int width,
                      int height,
                      bool,
                      int,
                      int,
                      int,
                      int,
                      juce::ComboBox&) override
    {
        auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width) - 1.0f, static_cast<float>(height) - 1.0f);
        juce::ColourGradient fill(panelInset().brighter(0.08f), bounds.getX(), bounds.getY(), panelBase(), bounds.getX(), bounds.getBottom(), false);
        fill.addColour(0.55, panelRaised());
        g.setGradientFill(fill);
        g.fillRoundedRectangle(bounds, 8.0f);

        g.setColour(copper().withAlpha(0.55f));
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

        juce::Path arrow;
        const float cx = bounds.getRight() - 15.0f;
        const float cy = bounds.getCentreY();
        arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
        arrow.lineTo(cx, cy + 3.0f);
        arrow.lineTo(cx + 4.0f, cy - 2.0f);
        g.setColour(amberBright());
        g.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(10, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
        label.setJustificationType(juce::Justification::centredLeft);
    }

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour&,
                              bool isMouseOverButton,
                              bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const auto role = button.getProperties().getWithDefault("soundRole", juce::String()).toString();
        const bool selected = static_cast<bool>(button.getProperties().getWithDefault("selected", false));
        const bool toggled = button.getToggleState();

        juce::Colour base = button.findColour(juce::TextButton::buttonColourId);
        if (role == "strip")
            base = selected ? amber().withAlpha(0.92f) : panelInset().brighter(0.14f);
        else if (toggled)
            base = button.findColour(juce::TextButton::buttonOnColourId);

        if (isMouseOverButton)
            base = base.brighter(0.08f);
        if (isButtonDown)
            base = base.darker(0.14f);

        const float radius = role == "strip" ? 9.0f : 8.0f;
        g.setColour(juce::Colours::black.withAlpha(role == "strip" ? 0.16f : 0.22f));
        g.fillRoundedRectangle(bounds.translated(0.0f, 2.0f), radius);

        juce::ColourGradient fill(base.brighter(role == "strip" ? 0.12f : 0.18f),
                                  bounds.getX(),
                                  bounds.getY(),
                                  base.darker(role == "strip" ? 0.18f : 0.28f),
                                  bounds.getX(),
                                  bounds.getBottom(),
                                  false);
        fill.addColour(0.45, base);
        g.setGradientFill(fill);
        g.fillRoundedRectangle(bounds, radius);

        g.setColour((selected || toggled ? amberBright() : copper()).withAlpha(role == "strip" ? 0.55f : 0.42f));
        g.drawRoundedRectangle(bounds, radius, 1.0f);
    }

    void drawButtonText(juce::Graphics& g,
                        juce::TextButton& button,
                        bool,
                        bool) override
    {
        const auto role = button.getProperties().getWithDefault("soundRole", juce::String()).toString();
        const bool selected = static_cast<bool>(button.getProperties().getWithDefault("selected", false));
        juce::Colour colour = button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                                        : juce::TextButton::textColourOffId);
        if (role == "strip")
            colour = selected ? panelInset().brighter(0.9f) : textMain();

        g.setColour(colour);
        g.setFont(getTextButtonFont(button, button.getHeight()));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 2), juce::Justification::centred, 1);
    }

    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)).reduced(8.0f);
        const float diameter = std::min(bounds.getWidth(), bounds.getHeight());
        bounds.setSize(diameter, diameter);
        bounds.setCentre(static_cast<float>(x) + static_cast<float>(width) * 0.5f,
                         static_cast<float>(y) + static_cast<float>(height) * 0.5f);

        const auto outline = slider.findColour(juce::Slider::rotarySliderOutlineColourId);
        const auto fill = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const auto thumb = slider.findColour(juce::Slider::thumbColourId);
        const auto track = slider.findColour(juce::Slider::trackColourId);
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const auto centre = bounds.getCentre();

        g.setColour(juce::Colours::black.withAlpha(0.34f));
        g.fillEllipse(bounds.translated(0.0f, 3.0f));

        juce::ColourGradient rim(shellRaised().brighter(0.20f), bounds.getX(), bounds.getY(), shellBase(), bounds.getX(), bounds.getBottom(), false);
        g.setGradientFill(rim);
        g.fillEllipse(bounds);
        g.setColour(outline);
        g.drawEllipse(bounds, 1.2f);

        auto inner = bounds.reduced(diameter * 0.10f);
        juce::ColourGradient body(panelRaised().brighter(0.18f), inner.getX(), inner.getY(), panelInset(), inner.getX(), inner.getBottom(), false);
        body.addColour(0.42, panelBase());
        g.setGradientFill(body);
        g.fillEllipse(inner);

        auto highlight = inner.reduced(inner.getWidth() * 0.14f);
        highlight.setHeight(highlight.getHeight() * 0.44f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillEllipse(highlight);

        juce::Path trackPath;
        trackPath.addCentredArc(centre.x,
                                centre.y,
                                inner.getWidth() * 0.52f,
                                inner.getHeight() * 0.52f,
                                0.0f,
                                rotaryStartAngle,
                                rotaryEndAngle,
                                true);
        g.setColour(track.withAlpha(0.18f));
        g.strokePath(trackPath, juce::PathStrokeType(4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path valuePath;
        valuePath.addCentredArc(centre.x,
                                centre.y,
                                inner.getWidth() * 0.52f,
                                inner.getHeight() * 0.52f,
                                0.0f,
                                rotaryStartAngle,
                                angle,
                                true);
        g.setColour(fill.withAlpha(0.95f));
        g.strokePath(valuePath, juce::PathStrokeType(4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path pointer;
        auto pointerLength = inner.getHeight() * 0.36f;
        auto pointerThickness = 3.8f;
        pointer.addRoundedRectangle(-pointerThickness * 0.5f, -inner.getHeight() * 0.08f - pointerLength, pointerThickness, pointerLength, 1.3f);
        g.setColour(thumb);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));

        g.setColour(copper().withAlpha(0.85f));
        g.fillEllipse(centre.x - 3.0f, centre.y - 3.0f, 6.0f, 6.0f);
    }
};

HardwareLookAndFeel hardwareLookAndFeel;

void styleHeaderLabel(juce::Label& label, const juce::String& text, float size = 12.5f)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, textMain());
    label.setFont(juce::Font(juce::FontOptions(size, juce::Font::bold)));
    label.setInterceptsMouseClicks(false, false);
}

void styleMicroLabel(juce::Label& label, const juce::String& text, float size = 10.0f)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, textMuted());
    label.setFont(juce::Font(juce::FontOptions(size, juce::Font::bold)));
    label.setInterceptsMouseClicks(false, false);
}

void styleValueLabel(juce::Label& label)
{
    label.setJustificationType(juce::Justification::centredRight);
    label.setColour(juce::Label::textColourId, amberBright());
    label.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    label.setInterceptsMouseClicks(false, false);
}

void drawScrew(juce::Graphics& g, juce::Point<float> centre)
{
    g.setColour(juce::Colour::fromRGB(62, 58, 54));
    g.fillEllipse(centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 28));
    g.drawEllipse(centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f, 1.0f);
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 110));
    g.drawLine(centre.x - 2.5f, centre.y, centre.x + 2.5f, centre.y, 1.0f);
}

void drawStereoPlaceholder(juce::Graphics& g, const juce::Rectangle<int>& bounds, float pan, float width)
{
    if (bounds.isEmpty())
        return;

    auto r = bounds.toFloat();
    juce::ColourGradient fill(juce::Colour::fromRGB(17, 14, 12), r.getX(), r.getY(), juce::Colour::fromRGB(8, 8, 8), r.getX(), r.getBottom(), false);
    g.setGradientFill(fill);
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(amber().withAlpha(0.32f));
    g.drawRoundedRectangle(r, 10.0f, 1.0f);

    const float centreX = r.getCentreX();
    const float centreY = r.getCentreY();
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
    g.drawLine(r.getX() + 12.0f, centreY, r.getRight() - 12.0f, centreY, 1.0f);
    g.drawLine(centreX, r.getY() + 10.0f, centreX, r.getBottom() - 10.0f, 1.0f);

    const float span = (r.getWidth() - 36.0f) * juce::jlimit(0.0f, 1.0f, width * 0.5f);
    const float markerX = centreX + pan * (r.getWidth() * 0.32f);
    juce::Rectangle<float> widthBox(markerX - span * 0.5f, centreY - 9.0f, juce::jmax(18.0f, span), 18.0f);
    g.setColour(amber().withAlpha(0.16f));
    g.fillRoundedRectangle(widthBox, 9.0f);
    g.setColour(amberBright().withAlpha(0.75f));
    g.drawRoundedRectangle(widthBox, 9.0f, 1.0f);
    g.fillRoundedRectangle(juce::Rectangle<float>(markerX - 1.2f, centreY - 15.0f, 2.4f, 30.0f), 1.1f);
}

void drawTransientPlaceholder(juce::Graphics& g, const juce::Rectangle<int>& bounds, float attack, float sustain, float gain)
{
    if (bounds.isEmpty())
        return;

    auto r = bounds.toFloat();
    juce::ColourGradient fill(juce::Colour::fromRGB(16, 13, 12), r.getX(), r.getY(), juce::Colour::fromRGB(7, 7, 7), r.getX(), r.getBottom(), false);
    g.setGradientFill(fill);
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(copper().withAlpha(0.38f));
    g.drawRoundedRectangle(r, 10.0f, 1.0f);

    auto graph = r.reduced(14.0f, 10.0f);
    const float baselineY = graph.getBottom() - 3.0f;
    const float attackAmount = juce::jlimit(0.0f, 1.0f, attack / 100.0f);
    const float sustainAmount = juce::jlimit(0.0f, 1.0f, sustain / 100.0f);
    const float gainOffset = juce::jmap(juce::jlimit(-6.0f, 12.0f, gain), -6.0f, 12.0f, -4.0f, 7.0f);
    const float attackPeakY = baselineY - (graph.getHeight() * juce::jmap(attackAmount, 0.0f, 1.0f, 0.28f, 0.88f)) - gainOffset * 0.28f;
    const float sustainY = baselineY - (graph.getHeight() * juce::jmap(sustainAmount, 0.0f, 1.0f, 0.14f, 0.46f)) - gainOffset * 0.22f;

    g.setColour(textMuted().withAlpha(0.16f));
    g.drawLine(graph.getX(), baselineY, graph.getRight(), baselineY, 1.0f);
    g.drawLine(graph.getX(), graph.getY() + graph.getHeight() * 0.42f, graph.getRight(), graph.getY() + graph.getHeight() * 0.42f, 1.0f);

    juce::Path envelope;
    const float startX = graph.getX() + 2.0f;
    const float attackX = graph.getX() + graph.getWidth() * 0.14f;
    const float settleX = graph.getX() + graph.getWidth() * 0.42f;
    const float tailX = graph.getRight() - 2.0f;
    envelope.startNewSubPath(startX, baselineY);
    envelope.lineTo(graph.getX() + graph.getWidth() * 0.05f, attackPeakY);
    envelope.cubicTo(attackX,
                     attackPeakY,
                     graph.getX() + graph.getWidth() * 0.28f,
                     sustainY,
                     settleX,
                     sustainY);
    envelope.cubicTo(graph.getX() + graph.getWidth() * 0.62f,
                     sustainY,
                     graph.getX() + graph.getWidth() * 0.82f,
                     baselineY - (baselineY - sustainY) * 0.36f,
                     tailX,
                     baselineY - gainOffset * 0.12f);

    juce::Path envelopeFill(envelope);
    envelopeFill.lineTo(tailX, baselineY);
    envelopeFill.closeSubPath();

    g.setColour(amber().withAlpha(0.16f));
    g.fillPath(envelopeFill);
    g.setColour(amberBright().withAlpha(0.86f));
    g.strokePath(envelope, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto trimLine = juce::Rectangle<float>(graph.getRight() - 24.0f,
                                           juce::jlimit(graph.getY() + 4.0f, baselineY - 2.0f, baselineY - gainOffset - 8.0f),
                                           18.0f,
                                           2.4f);
    g.setColour(copper().withAlpha(0.60f));
    g.fillRoundedRectangle(trimLine, 1.2f);
}

constexpr double kEqDisplayMinFrequencyHz = 20.0;
constexpr double kEqDisplayMaxFrequencyHz = 20000.0;
constexpr double kEqDisplaySampleRate = 48000.0;
constexpr float kEqDisplayDbRange = 18.0f;

double normalizeEqDisplayFrequency(double frequencyHz)
{
    const double clamped = juce::jlimit(kEqDisplayMinFrequencyHz, kEqDisplayMaxFrequencyHz, frequencyHz);
    const double minLog = std::log10(kEqDisplayMinFrequencyHz);
    const double maxLog = std::log10(kEqDisplayMaxFrequencyHz);
    return (std::log10(clamped) - minLog) / (maxLog - minLog);
}

double eqDisplayFrequencyFromNormalized(double normalized)
{
    const double minLog = std::log10(kEqDisplayMinFrequencyHz);
    const double maxLog = std::log10(kEqDisplayMaxFrequencyHz);
    return std::pow(10.0, minLog + juce::jlimit(0.0, 1.0, normalized) * (maxLog - minLog));
}

float mapEqDisplayDbToY(const juce::Rectangle<float>& bounds, float gainDb)
{
    const float clampedDb = juce::jlimit(-kEqDisplayDbRange, kEqDisplayDbRange, gainDb);
    const float normalized = (clampedDb + kEqDisplayDbRange) / (kEqDisplayDbRange * 2.0f);
    return bounds.getBottom() - normalized * bounds.getHeight();
}

float mapEqAnalyzerMagnitudeToY(const juce::Rectangle<float>& bounds, float magnitude)
{
    const float clamped = juce::jlimit(0.0f, 1.0f, magnitude);
    const float headroom = bounds.getHeight() * 0.92f;
    return bounds.getBottom() - clamped * headroom;
}

juce::dsp::IIR::Coefficients<float>::Ptr makeEqDisplayCoefficients(const EqBandState& band)
{
    const float frequencyHz = juce::jlimit(static_cast<float>(kEqDisplayMinFrequencyHz),
                                           static_cast<float>(kEqDisplayMaxFrequencyHz),
                                           band.freqHz);
    const float q = juce::jmax(0.1f, band.q);

    switch (band.shape)
    {
        case EqBandShape::LowCut:
            return juce::dsp::IIR::Coefficients<float>::makeHighPass(kEqDisplaySampleRate, frequencyHz, q);
        case EqBandShape::HighCut:
            return juce::dsp::IIR::Coefficients<float>::makeLowPass(kEqDisplaySampleRate, frequencyHz, q);
        case EqBandShape::Bell:
        default:
            return juce::dsp::IIR::Coefficients<float>::makePeakFilter(kEqDisplaySampleRate,
                                                                       frequencyHz,
                                                                       q,
                                                                       juce::Decibels::decibelsToGain(band.gainDb));
    }
}

float computeEqDisplayResponseDb(const EqState& eq, double frequencyHz)
{
    double totalMagnitude = 1.0;
    for (const auto& band : eq.bands)
    {
        if (!band.enabled)
            continue;

        if (auto coefficients = makeEqDisplayCoefficients(band); coefficients != nullptr)
            totalMagnitude *= coefficients->getMagnitudeForFrequency(frequencyHz, kEqDisplaySampleRate);
    }

    totalMagnitude = juce::jmax(1.0e-6, totalMagnitude);
    return juce::jlimit(-kEqDisplayDbRange,
                        kEqDisplayDbRange,
                        juce::Decibels::gainToDecibels(static_cast<float>(totalMagnitude), -kEqDisplayDbRange * 2.0f));
}

juce::Rectangle<float> getEqDisplayResponseArea(const juce::Rectangle<int>& bounds)
{
    auto responseArea = bounds.toFloat().reduced(16.0f, 12.0f);
    responseArea.removeFromLeft(26.0f);
    responseArea.removeFromBottom(18.0f);
    return responseArea;
}

juce::String eqBandShortName(int bandIndex)
{
    switch (bandIndex)
    {
        case 0:  return "LC";
        case 1:  return "WT";
        case 2:  return "BD";
        case 3:  return "BX";
        case 4:  return "AT";
        case 5:  return "AIR";
        case 6:  return "HC";
        default: return "EQ";
    }
}

juce::String eqBandShapeShortName(EqBandShape shape)
{
    switch (shape)
    {
        case EqBandShape::LowCut:  return "HP";
        case EqBandShape::HighCut: return "LP";
        case EqBandShape::Bell:
        default:
            return "BEL";
    }
}

juce::Point<float> getEqBandMarkerPosition(const juce::Rectangle<float>& responseArea, const EqState& eq, int bandIndex)
{
    const auto& band = eq.bands[static_cast<size_t>(clampEqBandIndex(bandIndex))];
    const double frequencyHz = juce::jlimit(kEqDisplayMinFrequencyHz,
                                            kEqDisplayMaxFrequencyHz,
                                            static_cast<double>(band.freqHz));
    const float x = responseArea.getX() + static_cast<float>(normalizeEqDisplayFrequency(frequencyHz)) * responseArea.getWidth();
    const float y = mapEqDisplayDbToY(responseArea, computeEqDisplayResponseDb(eq, frequencyHz));
    return { x, y };
}

juce::String formatEqBandBadgeText(int bandIndex, const EqBandState& band)
{
    juce::String text = eqBandShortName(bandIndex) + " " + eqBandShapeShortName(band.shape);
    text << "  " << formatFreqValue(static_cast<double>(band.freqHz));
    if (band.shape == EqBandShape::Bell)
        text << "  " << formatDbValue(band.gainDb);
    text << "  Q " << juce::String(band.q, 2);
    if (!band.enabled)
        text << "  OFF";
    return text;
}

int countEnabledEqBands(const EqState& eq)
{
    int count = 0;
    for (const auto& band : eq.bands)
    {
        if (band.enabled)
            ++count;
    }

    return count;
}

juce::String buildEqCollapsedCharacter(const EqState& eq)
{
    const int enabledBands = countEnabledEqBands(eq);
    if (enabledBands <= 0)
        return "Contour ready / neutral shell";

    return juce::String(enabledBands) + (enabledBands == 1 ? " band / selected focus" : " bands / selected focus");
}

juce::String buildEqCollapsedSummary(const EqState& eq)
{
    const int bandIndex = clampEqBandIndex(eq.selectedBand);
    const auto& band = eq.bands[static_cast<size_t>(bandIndex)];

    juce::String text = eqBandShortName(bandIndex) + " " + eqBandShapeShortName(band.shape);
    text << "  |  " << formatFreqValue(static_cast<double>(band.freqHz));
    if (band.shape == EqBandShape::Bell)
        text << "  |  " << formatDbValue(band.gainDb);
    return text;
}

juce::String buildCompressorCollapsedCharacter(const CompressorState& compressor)
{
    if (!compressor.enabled)
        return "Bypassed / drum glue ready";

    return compressor.character == DrumCompressorCharacter::Punch
        ? "Punch clamp / level hold"
        : "Glue clamp / level hold";
}

juce::String buildCompressorCollapsedSummary(const CompressorState& compressor)
{
    return formatRatioValue(compressor.ratio)
        + "  |  MIX " + formatPercentValue(static_cast<double>(compressor.mix) * 100.0);
}

juce::String buildStereoCollapsedCharacter(bool monoSafe, float lowCenterProtect, float airSpread)
{
    if (monoSafe)
        return "Mono safe / centered lows";
    if (airSpread > 0.35f)
        return "Wide image / air lift";
    if (lowCenterProtect > 0.35f)
        return "Open image / tight lows";
    return "Open field / drum spread";
}

juce::String buildStereoCollapsedSummary(float pan, float width)
{
    return formatPanValue(pan)
        + "  |  " + formatWidthValue(width);
}

juce::String buildReverbCollapsedCharacter(double size, double tail)
{
    if (size < 28.0)
        return "Tight room / front depth";
    if (tail > 60.0)
        return "Wide room / tail bloom";
    return "Room glue / back depth";
}

juce::String buildReverbCollapsedSummary(double size, double mix, double predelay)
{
    juce::ignoreUnused(predelay);
    return "MIX " + formatPercentValue(mix)
        + "  |  SIZE " + formatPercentValue(size);
}

juce::String buildMonstaCollapsedCharacter(const MonstaFxState& monstaFx)
{
    if (monstaFx.wet <= 0.001f)
        return "Reverse / glitch / VHS chaos ready";

    return monstaFxFlavorCharacter(resolveMonstaFxFlavor(monstaFx));
}

juce::String buildMonstaCollapsedSummary(const MonstaFxState& monstaFx)
{
    return juce::String(monstaFxFlavorTitle(resolveMonstaFxFlavor(monstaFx))) + "  |  Dry "
        + formatPercentValue(static_cast<double>(monstaFx.dry) * 100.0)
        + "  |  Wet " + formatPercentValue(static_cast<double>(monstaFx.wet) * 100.0);
}

juce::String buildTransientCollapsedCharacter(bool smoothEnabled, bool limitEnabled)
{
    if (smoothEnabled && limitEnabled)
        return "Smoothed hit / capped peak";
    if (smoothEnabled)
        return "Smoothed hit / body hold";
    if (limitEnabled)
        return "Fast hit / capped peak";
    return "Fast hit / body hold";
}

juce::String buildTransientCollapsedSummary(double attack, double sustain, double gain)
{
    if (std::abs(gain) >= 0.6)
        return "ATK " + formatPercentValue(attack)
            + "  |  " + formatDbValue(static_cast<float>(gain));

    return "ATK " + formatPercentValue(attack)
        + "  |  SUS " + formatPercentValue(sustain);
}

void drawCardSummaryText(juce::Graphics& g,
                         const juce::Rectangle<int>& bounds,
                         const juce::String& character,
                         const juce::String& summary,
                         juce::Colour accent)
{
    if (bounds.isEmpty())
        return;

    auto area = bounds.reduced(6, 4);
    auto indicatorBounds = juce::Rectangle<float>(static_cast<float>(area.getRight() - 34), static_cast<float>(area.getY()), 26.0f, 10.0f);
    g.setColour(accent.withAlpha(0.12f));
    g.fillRoundedRectangle(indicatorBounds, 5.0f);
    auto litBounds = indicatorBounds.reduced(4.0f, 3.0f);
    litBounds.setWidth(litBounds.getWidth() * 0.58f);
    g.setColour(accent.withAlpha(0.55f));
    g.fillRoundedRectangle(litBounds, 2.0f);

    auto characterBounds = area.removeFromTop(12);
    area.removeFromTop(6);
    auto summaryBounds = area.removeFromTop(18);

    g.setColour(textMuted().withAlpha(0.92f));
    g.setFont(juce::Font(juce::FontOptions(8.4f, juce::Font::bold)));
    g.drawFittedText(character.toUpperCase(), characterBounds, juce::Justification::centredLeft, 1);

    g.setColour(accent.withAlpha(0.96f));
    g.setFont(juce::Font(juce::FontOptions(10.2f, juce::Font::bold)));
    g.drawFittedText(summary, summaryBounds, juce::Justification::centredLeft, 1);

    auto underline = juce::Rectangle<float>(static_cast<float>(summaryBounds.getX()),
                                            static_cast<float>(bounds.getBottom() - 10),
                                            static_cast<float>(juce::jmin(summaryBounds.getWidth(), 74)),
                                            2.0f);
    g.setColour(accent.withAlpha(0.18f));
    g.fillRoundedRectangle(underline, 1.0f);
    underline.setWidth(underline.getWidth() * 0.62f);
    g.setColour(accent.withAlpha(0.62f));
    g.fillRoundedRectangle(underline, 1.0f);
}

void drawEqPreviewStrip(juce::Graphics& g, const juce::Rectangle<int>& bounds, const EqState& eq)
{
    if (bounds.isEmpty())
        return;

    auto display = bounds.toFloat();
    juce::ColourGradient fill(juce::Colour::fromRGB(19, 15, 12), display.getX(), display.getY(), juce::Colour::fromRGB(8, 8, 8), display.getX(), display.getBottom(), false);
    g.setGradientFill(fill);
    g.fillRoundedRectangle(display, 8.0f);
    g.setColour(amber().withAlpha(0.26f));
    g.drawRoundedRectangle(display, 8.0f, 1.0f);

    auto responseArea = display.reduced(10.0f, 6.0f);
    const float zeroDbY = mapEqDisplayDbToY(responseArea, 0.0f);
    g.setColour(amberBright().withAlpha(0.16f));
    g.drawLine(responseArea.getX(), zeroDbY, responseArea.getRight(), zeroDbY, 1.0f);

    juce::Path responseCurve;
    const int pointCount = juce::jmax(32, static_cast<int>(std::round(responseArea.getWidth())));
    for (int index = 0; index < pointCount; ++index)
    {
        const float normalized = pointCount > 1 ? static_cast<float>(index) / static_cast<float>(pointCount - 1) : 0.0f;
        const float x = responseArea.getX() + normalized * responseArea.getWidth();
        const float gainDb = computeEqDisplayResponseDb(eq, eqDisplayFrequencyFromNormalized(normalized));
        const float y = mapEqDisplayDbToY(responseArea, gainDb);

        if (index == 0)
            responseCurve.startNewSubPath(x, y);
        else
            responseCurve.lineTo(x, y);
    }

    g.setColour(amberBright().withAlpha(0.90f));
    g.strokePath(responseCurve, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawCompressorPreviewBars(juce::Graphics& g, const juce::Rectangle<int>& bounds, const CompressorState& compressor)
{
    if (bounds.isEmpty())
        return;

    auto area = bounds.toFloat();
    g.setColour(panelInset().brighter(0.08f));
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(copper().withAlpha(0.26f));
    g.drawRoundedRectangle(area, 8.0f, 1.0f);

    const std::array<float, 3> values {
        juce::jlimit(0.0f, 1.0f, (compressor.ratio - 1.5f) / 6.0f),
        juce::jlimit(0.0f, 1.0f, (-compressor.thresholdDb - 6.0f) / 30.0f),
        juce::jlimit(0.0f, 1.0f, compressor.mix)
    };
    const std::array<juce::String, 3> labels { "RATIO", "THRESH", "MIX" };

    auto slots = area.reduced(10.0f, 8.0f);
    const float gap = 8.0f;
    const float slotWidth = (slots.getWidth() - gap * 2.0f) / 3.0f;
    for (size_t index = 0; index < values.size(); ++index)
    {
        auto slot = juce::Rectangle<float>(slots.getX() + static_cast<float>(index) * (slotWidth + gap), slots.getY(), slotWidth, slots.getHeight());
        auto meter = slot.removeFromBottom(10.0f);
        g.setColour(textMuted().withAlpha(0.88f));
        g.setFont(juce::Font(juce::FontOptions(7.8f, juce::Font::bold)));
        g.drawText(labels[index], slot.toNearestInt(), juce::Justification::centredLeft, false);
        g.setColour(amber().withAlpha(0.16f));
        g.fillRoundedRectangle(meter, 4.0f);
        auto fillMeter = meter.withWidth(juce::jmax(10.0f, meter.getWidth() * values[index]));
        g.setColour((index == 1 ? copper() : amberBright()).withAlpha(0.90f));
        g.fillRoundedRectangle(fillMeter, 4.0f);
    }
}

void drawEqResponseDisplay(juce::Graphics& g,
                           const juce::Rectangle<int>& bounds,
                           const EqState& eq,
                           const EqDisplayAnalyzerState& analyzerState,
                           int hoveredBandIndex)
{
    if (bounds.isEmpty())
        return;

    auto display = bounds.toFloat();
    juce::ColourGradient displayFill(juce::Colour::fromRGB(20, 16, 13),
                                     display.getX(),
                                     display.getY(),
                                     juce::Colour::fromRGB(8, 8, 8),
                                     display.getX(),
                                     display.getBottom(),
                                     false);
    g.setGradientFill(displayFill);
    g.fillRoundedRectangle(display, 12.0f);
    g.setColour(amber().withAlpha(0.42f));
    g.drawRoundedRectangle(display, 12.0f, 1.0f);

    const auto responseArea = getEqDisplayResponseArea(bounds);

    const std::array<float, 5> dbGuides { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f };
    for (const auto db : dbGuides)
    {
        const float y = mapEqDisplayDbToY(responseArea, db);
        g.setColour(db == 0.0f ? amberBright().withAlpha(0.28f) : amber().withAlpha(0.10f));
        g.drawHorizontalLine(static_cast<int>(std::round(y)), responseArea.getX(), responseArea.getRight());

        g.setColour(db == 0.0f ? amberBright().withAlpha(0.82f) : textMuted().withAlpha(0.75f));
        g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        g.drawText(db > 0.0f ? "+" + juce::String(db, 0) : juce::String(db, 0),
                   juce::Rectangle<float>(display.getX() + 4.0f, y - 7.0f, 18.0f, 14.0f).toNearestInt(),
                   juce::Justification::centredRight,
                   false);
    }

    const std::array<double, 10> frequencyGuides { 30.0, 60.0, 120.0, 250.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 16000.0 };
    for (const auto frequencyHz : frequencyGuides)
    {
        const float x = responseArea.getX() + static_cast<float>(normalizeEqDisplayFrequency(frequencyHz)) * responseArea.getWidth();
        g.setColour(amber().withAlpha(0.09f));
        g.drawVerticalLine(static_cast<int>(std::round(x)), responseArea.getY(), responseArea.getBottom());
    }

    const std::array<std::pair<double, juce::String>, 6> frequencyLabels {
        std::pair<double, juce::String> { 30.0, "30" },
        std::pair<double, juce::String> { 100.0, "100" },
        std::pair<double, juce::String> { 500.0, "500" },
        std::pair<double, juce::String> { 2000.0, "2k" },
        std::pair<double, juce::String> { 10000.0, "10k" },
        std::pair<double, juce::String> { 20000.0, "20k" }
    };
    g.setColour(textMuted());
    g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    for (const auto& [frequencyHz, label] : frequencyLabels)
    {
        const float x = responseArea.getX() + static_cast<float>(normalizeEqDisplayFrequency(frequencyHz)) * responseArea.getWidth();
        g.drawText(label,
                   juce::Rectangle<float>(x - 16.0f, responseArea.getBottom() + 2.0f, 32.0f, 12.0f).toNearestInt(),
                   juce::Justification::centred,
                   false);
    }

    if (analyzerState.active)
    {
        juce::Path analyzerPath;
        juce::Path analyzerFill;
        for (int index = 0; index < kEqDisplayAnalyzerBinCount; ++index)
        {
            const float normalized = kEqDisplayAnalyzerBinCount > 1
                ? static_cast<float>(index) / static_cast<float>(kEqDisplayAnalyzerBinCount - 1)
                : 0.0f;
            const float x = responseArea.getX() + normalized * responseArea.getWidth();
            const float y = mapEqAnalyzerMagnitudeToY(responseArea, analyzerState.magnitudes[static_cast<size_t>(index)]);

            if (index == 0)
            {
                analyzerPath.startNewSubPath(x, y);
                analyzerFill.startNewSubPath(x, responseArea.getBottom());
                analyzerFill.lineTo(x, y);
            }
            else
            {
                analyzerPath.lineTo(x, y);
                analyzerFill.lineTo(x, y);
            }
        }

        analyzerFill.lineTo(responseArea.getRight(), responseArea.getBottom());
        analyzerFill.closeSubPath();

        const float analyzerAlpha = juce::jmap(juce::jlimit(0.0f, 1.0f, analyzerState.rms), 0.0f, 1.0f, 0.10f, 0.22f);
        g.setColour(amber().withAlpha(analyzerAlpha));
        g.fillPath(analyzerFill);
        g.setColour(amberBright().withAlpha(analyzerAlpha * 1.15f));
        g.strokePath(analyzerPath, juce::PathStrokeType(1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Path responseCurve;
    juce::Path responseFill;
    const float zeroDbY = mapEqDisplayDbToY(responseArea, 0.0f);
    const int pointCount = juce::jmax(64, static_cast<int>(std::round(responseArea.getWidth())));
    for (int index = 0; index < pointCount; ++index)
    {
        const float normalized = pointCount > 1 ? static_cast<float>(index) / static_cast<float>(pointCount - 1) : 0.0f;
        const float x = responseArea.getX() + normalized * responseArea.getWidth();
        const float gainDb = computeEqDisplayResponseDb(eq, eqDisplayFrequencyFromNormalized(normalized));
        const float y = mapEqDisplayDbToY(responseArea, gainDb);

        if (index == 0)
        {
            responseCurve.startNewSubPath(x, y);
            responseFill.startNewSubPath(x, zeroDbY);
            responseFill.lineTo(x, y);
        }
        else
        {
            responseCurve.lineTo(x, y);
            responseFill.lineTo(x, y);
        }
    }

    responseFill.lineTo(responseArea.getRight(), zeroDbY);
    responseFill.closeSubPath();
    g.setColour(amber().withAlpha(0.12f));
    g.fillPath(responseFill);

    g.setColour(amberBright().withAlpha(0.92f));
    g.strokePath(responseCurve, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const int selectedBandIndex = clampEqBandIndex(eq.selectedBand);
    const int focusBandIndex = hoveredBandIndex >= 0 ? clampEqBandIndex(hoveredBandIndex) : selectedBandIndex;
    for (int bandIndex = 0; bandIndex < kEqBandCount; ++bandIndex)
    {
        const auto& band = eq.bands[static_cast<size_t>(bandIndex)];
        const auto markerPosition = getEqBandMarkerPosition(responseArea, eq, bandIndex);
        const float x = markerPosition.x;
        const float y = markerPosition.y;
        const bool isSelected = bandIndex == selectedBandIndex;
        const bool isHovered = bandIndex == hoveredBandIndex;
        const bool isFocus = bandIndex == focusBandIndex;
        const float radius = isSelected ? 4.8f : isHovered ? 4.2f : band.enabled ? 3.4f : 2.8f;
        const auto markerColour = isSelected ? amberBright()
                                             : isHovered ? amberBright().interpolatedWith(amber(), 0.35f)
                                                         : band.enabled ? amber()
                                                                        : steel().withAlpha(0.70f);

        if (band.enabled || isFocus)
        {
            g.setColour(markerColour.withAlpha(isSelected ? 0.24f : isHovered ? 0.18f : 0.12f));
            g.drawVerticalLine(static_cast<int>(std::round(x)), responseArea.getY(), responseArea.getBottom());
        }

        g.setColour(markerColour.withAlpha(isFocus ? 0.12f : 0.06f));
        g.fillEllipse(x - 9.0f, y - 9.0f, 18.0f, 18.0f);
        g.setColour(panelInset().brighter(0.20f));
        g.fillEllipse(x - radius - 1.2f, y - radius - 1.2f, (radius + 1.2f) * 2.0f, (radius + 1.2f) * 2.0f);
        g.setColour(markerColour.withAlpha(band.enabled ? 0.95f : 0.55f));
        g.fillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f);

        if (isFocus)
        {
            g.setColour(markerColour.withAlpha(0.95f));
            g.drawEllipse(x - (radius + 3.0f), y - (radius + 3.0f), (radius + 3.0f) * 2.0f, (radius + 3.0f) * 2.0f, 1.2f);
        }
    }

    if (focusBandIndex >= 0)
    {
        const auto& focusBand = eq.bands[static_cast<size_t>(focusBandIndex)];
        const auto markerPosition = getEqBandMarkerPosition(responseArea, eq, focusBandIndex);
        const auto badgeText = formatEqBandBadgeText(focusBandIndex, focusBand);
        const auto badgeColour = focusBandIndex == selectedBandIndex ? amberBright() : amber();
        const auto badgeFont = juce::Font(juce::FontOptions(9.2f, juce::Font::bold));
        juce::GlyphArrangement badgeGlyphs;
        badgeGlyphs.addLineOfText(badgeFont, badgeText, 0.0f, 0.0f);
        const float badgeWidth = badgeGlyphs.getBoundingBox(0, badgeText.length(), true).getWidth() + 16.0f;
        const float badgeHeight = 18.0f;
        const float preferredY = juce::jmax(display.getY() + 8.0f, markerPosition.y - 28.0f);
        juce::Rectangle<float> badge(markerPosition.x - badgeWidth * 0.5f, preferredY, badgeWidth, badgeHeight);

        badge.setX(juce::jlimit(display.getX() + 8.0f, display.getRight() - badgeWidth - 8.0f, badge.getX()));
        badge.setY(juce::jlimit(display.getY() + 8.0f, responseArea.getBottom() - badgeHeight - 6.0f, badge.getY()));

        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.fillRoundedRectangle(badge.translated(0.0f, 2.0f), 8.0f);
        g.setColour(panelInset().brighter(0.18f));
        g.fillRoundedRectangle(badge, 8.0f);
        g.setColour(badgeColour.withAlpha(0.88f));
        g.drawRoundedRectangle(badge, 8.0f, 1.0f);
        g.setColour(badgeColour.withAlpha(0.26f));
        g.drawLine(markerPosition.x,
                   badge.getBottom(),
                   markerPosition.x,
                   markerPosition.y - 6.0f,
                   1.0f);
        g.setColour(badgeColour.withAlpha(0.96f));
        g.setFont(badgeFont);
        g.drawText(badgeText, badge.toNearestInt(), juce::Justification::centred, false);
    }

    g.setColour(textMuted().withAlpha(0.82f));
    g.setFont(juce::Font(juce::FontOptions(8.8f, juce::Font::bold)));
    g.drawText("CLICK DOT TO FOCUS BAND", juce::Rectangle<float>(display.getRight() - 152.0f, display.getY() + 6.0f, 144.0f, 12.0f).toNearestInt(), juce::Justification::centredRight, false);
}
} // namespace

SoundModuleComponent::RotaryDial::RotaryDial()
{
    setWantsKeyboardFocus(false);
}

void SoundModuleComponent::RotaryDial::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!isPointOverKnob(event.position))
        return;

    juce::Slider::mouseWheelMove(event, wheel);
}

bool SoundModuleComponent::RotaryDial::isPointOverActiveZone(juce::Point<float> position) const
{
    return isPointOverKnob(position);
}

juce::Rectangle<float> SoundModuleComponent::RotaryDial::getInteractiveKnobBounds() const
{
    auto bounds = getLocalBounds().toFloat().reduced(8.0f);
    const float diameter = std::min(bounds.getWidth(), bounds.getHeight());
    bounds.setSize(diameter, diameter);
    bounds.setCentre(getLocalBounds().toFloat().getCentre());
    return bounds;
}

bool SoundModuleComponent::RotaryDial::isPointOverKnob(juce::Point<float> position) const
{
    const auto knobBounds = getInteractiveKnobBounds();
    const auto radius = knobBounds.getWidth() * 0.5f;
    const auto centre = knobBounds.getCentre();
    const auto dx = position.x - centre.x;
    const auto dy = position.y - centre.y;
    return (dx * dx) + (dy * dy) <= radius * radius;
}

SoundModuleComponent::SoundModuleComponent()
{
    currentTarget = SoundTargetDescriptor::makeGlobal();
    setLookAndFeel(&hardwareLookAndFeel);
    verticalScrollBar.addListener(this);
    verticalScrollBar.setSingleStepSize(44.0);
    addAndMakeVisible(verticalScrollBar);
    contentViewport.setInterceptsMouseClicks(false, true);
    contentCanvas.setInterceptsMouseClicks(false, true);
    contentCanvas.addMouseListener(this, true);
    addAndMakeVisible(contentViewport);
    contentViewport.addAndMakeVisible(contentCanvas);

    styleHeaderLabel(titleLabel, "HPDG SOUND", 16.0f);
    addAndMakeVisible(titleLabel);

    styleMicroLabel(brandLabel, "MODULE RACK", 10.5f);
    brandLabel.setColour(juce::Label::textColourId, amber());
    addAndMakeVisible(brandLabel);

    styleMicroLabel(targetLabel, "TARGET", 10.0f);
    addAndMakeVisible(targetLabel);

    styleHeaderLabel(targetSummaryLabel, "Editing: Global sound layer", 11.5f);
    addAndMakeVisible(targetSummaryLabel);

    targetModeLabel.setText("GLOBAL", juce::dontSendNotification);
    targetModeLabel.setJustificationType(juce::Justification::centred);
    targetModeLabel.setColour(juce::Label::textColourId, panelInset().brighter(0.8f));
    targetModeLabel.setColour(juce::Label::backgroundColourId, amberBright());
    targetModeLabel.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    targetModeLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    addAndMakeVisible(targetModeLabel);

    styleMicroLabel(targetStatusLabel, "Routing: shared sound shaping for the full kit.", 10.2f);
    targetStatusLabel.setColour(juce::Label::textColourId, steel());
    addAndMakeVisible(targetStatusLabel);

    styleMicroLabel(chainSummaryLabel, "FLOW: PRE EQ  |  STEREO 100%  |  COMP OFF  |  MONSTA 0%  |  REV 0%  |  TRANS 0%", 10.0f);
    chainSummaryLabel.setColour(juce::Label::textColourId, amber());
    chainSummaryLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(chainSummaryLabel);

    setupCombo(targetCombo);
    targetCombo.setTooltip("Choose whether the Sound Module edits the global layer or a specific lane target.");
    addAndMakeVisible(targetCombo);

    setupActionButton(bypassButton, false);
    bypassButton.setClickingTogglesState(true);
    bypassButton.onClick = [this] { updateChainSummary(); };
    addAndMakeVisible(bypassButton);

    setupActionButton(resetButton, true);
    resetButton.onClick = [this] { resetCurrentState(); };
    addAndMakeVisible(resetButton);

    styleHeaderLabel(eqSectionLabel, "EQ", 14.5f);
    addAndMakeVisible(eqSectionLabel);
    styleMicroLabel(eqDescriptorLabel, "Drum contour stage", 10.0f);
    addAndMakeVisible(eqDescriptorLabel);
    styleMicroLabel(eqBandMeaningLabel, "FOCUS: BODY", 10.0f);
    eqBandMeaningLabel.setColour(juce::Label::textColourId, amberBright());
    addAndMakeVisible(eqBandMeaningLabel);
    styleMicroLabel(eqBandRangeLabel, "120-250 Hz  |  weight / body", 9.5f);
    eqBandRangeLabel.setJustificationType(juce::Justification::centredRight);
    eqBandRangeLabel.setColour(juce::Label::textColourId, steel());
    addAndMakeVisible(eqBandRangeLabel);
    styleMicroLabel(eqOrderLabel, "ORDER");
    addAndMakeVisible(eqOrderLabel);
    styleMicroLabel(eqBandLabel, "BAND");
    addAndMakeVisible(eqBandLabel);
    styleMicroLabel(eqShapeLabel, "SHAPE");
    addAndMakeVisible(eqShapeLabel);
    styleMicroLabel(eqFreqLabel, "FREQ");
    addAndMakeVisible(eqFreqLabel);
    styleMicroLabel(eqGainLabel, "GAIN");
    addAndMakeVisible(eqGainLabel);
    styleMicroLabel(eqQLabel, "Q");
    addAndMakeVisible(eqQLabel);
    styleValueLabel(eqFreqValueLabel);
    addAndMakeVisible(eqFreqValueLabel);
    styleValueLabel(eqGainValueLabel);
    addAndMakeVisible(eqGainValueLabel);
    styleValueLabel(eqQValueLabel);
    addAndMakeVisible(eqQValueLabel);
    setupCombo(eqOrderCombo);
    eqOrderCombo.addItem("PRE", 1);
    eqOrderCombo.addItem("POST", 2);
    eqOrderCombo.setSelectedId(1, juce::dontSendNotification);
    eqOrderCombo.onChange = [this] { updateChainSummary(); };
    addAndMakeVisible(eqOrderCombo);
    setupCombo(eqBandCombo);
    eqBandCombo.addItem("LOW CUT", 1);
    eqBandCombo.addItem("WEIGHT", 2);
    eqBandCombo.addItem("BODY", 3);
    eqBandCombo.addItem("BOX", 4);
    eqBandCombo.addItem("ATTACK", 5);
    eqBandCombo.addItem("AIR", 6);
    eqBandCombo.addItem("HIGH CUT", 7);
    eqBandCombo.setSelectedId(3, juce::dontSendNotification);
    eqBandCombo.onChange = [this]
    {
        if (isSyncingUi)
            return;

        currentSoundState.eq.selectedBand = currentEqBandIndex();
        updateEqBandUiFromSelection();
        emitSoundLayerChange();
    };
    addAndMakeVisible(eqBandCombo);
    setupCombo(eqShapeCombo);
    eqShapeCombo.addItem("LOW CUT", 1);
    eqShapeCombo.addItem("BELL", 2);
    eqShapeCombo.addItem("HIGH CUT", 3);
    eqShapeCombo.setSelectedId(2, juce::dontSendNotification);
    eqShapeCombo.onChange = [this]
    {
        if (isSyncingUi)
            return;

        if (!eqEnableButton.getToggleState())
            eqEnableButton.setToggleState(true, juce::dontSendNotification);
        commitEqBandControlsToState();
        emitSoundLayerChange();
    };
    addAndMakeVisible(eqShapeCombo);
    setupActionButton(eqEnableButton, false);
    eqEnableButton.setClickingTogglesState(true);
    eqEnableButton.setToggleState(true, juce::dontSendNotification);
    eqEnableButton.onClick = [this]
    {
        if (isSyncingUi)
            return;

        commitEqBandControlsToState();
        emitSoundLayerChange();
    };
    addAndMakeVisible(eqEnableButton);

    setupDial(eqFreqSlider, 20.0, 20000.0, 1.0, amber(), 850.0);
    setupDial(eqGainSlider, -12.0, 12.0, 0.1, amberBright());
    setupDial(eqQSlider, 0.2, 8.0, 0.01, copper(), 1.0);
    eqFreqSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        if (!eqEnableButton.getToggleState())
            eqEnableButton.setToggleState(true, juce::dontSendNotification);
        commitEqBandControlsToState();
        emitSoundLayerChange();
    };
    eqGainSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        if (!eqEnableButton.getToggleState())
            eqEnableButton.setToggleState(true, juce::dontSendNotification);
        commitEqBandControlsToState();
        emitSoundLayerChange();
    };
    eqQSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        if (!eqEnableButton.getToggleState())
            eqEnableButton.setToggleState(true, juce::dontSendNotification);
        commitEqBandControlsToState();
        emitSoundLayerChange();
    };
    addAndMakeVisible(eqFreqSlider);
    addAndMakeVisible(eqGainSlider);
    addAndMakeVisible(eqQSlider);

    styleHeaderLabel(stereoSectionLabel, "STEREO FIELD", 13.0f);
    addAndMakeVisible(stereoSectionLabel);
    styleMicroLabel(stereoDescriptorLabel, "Center hold and side bite", 10.0f);
    addAndMakeVisible(stereoDescriptorLabel);
    styleMicroLabel(stereoInputLabel, "INPUT L/R", 9.5f);
    stereoInputLabel.setColour(juce::Label::textColourId, steel());
    addAndMakeVisible(stereoInputLabel);
    styleMicroLabel(stereoOutputLabel, "OUTPUT BUS", 9.5f);
    stereoOutputLabel.setColour(juce::Label::textColourId, amber());
    stereoOutputLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(stereoOutputLabel);
    styleMicroLabel(panLabel, "PAN");
    addAndMakeVisible(panLabel);
    styleMicroLabel(widthLabel, "WIDTH");
    addAndMakeVisible(widthLabel);
    styleMicroLabel(stereoFocusLabel, "FOCUS");
    addAndMakeVisible(stereoFocusLabel);
    styleMicroLabel(stereoEdgeLabel, "EDGE");
    addAndMakeVisible(stereoEdgeLabel);
    styleMicroLabel(stereoLowCenterProtectLabel, "LOW CENTER");
    addAndMakeVisible(stereoLowCenterProtectLabel);
    styleMicroLabel(stereoAirSpreadLabel, "AIR SPREAD");
    addAndMakeVisible(stereoAirSpreadLabel);
    styleValueLabel(panValueLabel);
    addAndMakeVisible(panValueLabel);
    styleValueLabel(widthValueLabel);
    addAndMakeVisible(widthValueLabel);
    styleValueLabel(stereoFocusValueLabel);
    addAndMakeVisible(stereoFocusValueLabel);
    styleValueLabel(stereoEdgeValueLabel);
    addAndMakeVisible(stereoEdgeValueLabel);
    styleValueLabel(stereoLowCenterProtectValueLabel);
    addAndMakeVisible(stereoLowCenterProtectValueLabel);
    styleValueLabel(stereoAirSpreadValueLabel);
    addAndMakeVisible(stereoAirSpreadValueLabel);
    setupDial(panSlider, -1.0, 1.0, 0.01, amberBright());
    setupDial(widthSlider, 0.0, 2.0, 0.01, amber());
    setupDial(stereoFocusSlider, 0.0, 100.0, 1.0, amberBright());
    setupDial(stereoEdgeSlider, 0.0, 100.0, 1.0, copper());
    setupDial(stereoLowCenterProtectSlider, 0.0, 100.0, 1.0, amber());
    setupDial(stereoAirSpreadSlider, 0.0, 100.0, 1.0, amberBright());
    panSlider.onValueChange = [this] { emitSoundLayerChange(); };
    widthSlider.onValueChange = [this] { emitSoundLayerChange(); };
    stereoFocusSlider.onValueChange = [this] { emitSoundLayerChange(); };
    stereoEdgeSlider.onValueChange = [this] { emitSoundLayerChange(); };
    stereoLowCenterProtectSlider.onValueChange = [this] { emitSoundLayerChange(); };
    stereoAirSpreadSlider.onValueChange = [this] { emitSoundLayerChange(); };
    addAndMakeVisible(panSlider);
    addAndMakeVisible(widthSlider);
    addAndMakeVisible(stereoFocusSlider);
    addAndMakeVisible(stereoEdgeSlider);
    addAndMakeVisible(stereoLowCenterProtectSlider);
    addAndMakeVisible(stereoAirSpreadSlider);
    setupActionButton(stereoMonoSafeButton, false);
    stereoMonoSafeButton.setClickingTogglesState(true);
    stereoMonoSafeButton.onClick = [this]
    {
        if (isSyncingUi)
            return;

        emitSoundLayerChange();
    };
    addAndMakeVisible(stereoMonoSafeButton);

    styleHeaderLabel(compSectionLabel, "COMPRESSOR", 13.5f);
    addAndMakeVisible(compSectionLabel);
    styleMicroLabel(compDescriptorLabel, "Dynamics and density", 10.0f);
    addAndMakeVisible(compDescriptorLabel);
    styleMicroLabel(compOrderLabel, "ORDER");
    addAndMakeVisible(compOrderLabel);
    styleMicroLabel(compRatioLabel, "RATIO");
    addAndMakeVisible(compRatioLabel);
    styleMicroLabel(compThresholdLabel, "THRESHOLD");
    addAndMakeVisible(compThresholdLabel);
    styleMicroLabel(compMixLabel, "MIX");
    addAndMakeVisible(compMixLabel);
    styleMicroLabel(compAttackLabel, "ATTACK");
    addAndMakeVisible(compAttackLabel);
    styleMicroLabel(compReleaseLabel, "RELEASE");
    addAndMakeVisible(compReleaseLabel);
    styleMicroLabel(compSaturationLabel, "SATURATION");
    addAndMakeVisible(compSaturationLabel);
    styleValueLabel(compRatioValueLabel);
    addAndMakeVisible(compRatioValueLabel);
    styleValueLabel(compThresholdValueLabel);
    addAndMakeVisible(compThresholdValueLabel);
    styleValueLabel(compMixValueLabel);
    addAndMakeVisible(compMixValueLabel);
    styleValueLabel(compAttackValueLabel);
    addAndMakeVisible(compAttackValueLabel);
    styleValueLabel(compReleaseValueLabel);
    addAndMakeVisible(compReleaseValueLabel);
    styleValueLabel(compSaturationValueLabel);
    addAndMakeVisible(compSaturationValueLabel);
    setupCombo(compOrderCombo);
    compOrderCombo.addItem("PRE GLUE", 1);
    compOrderCombo.addItem("PRE PUNCH", 2);
    compOrderCombo.addItem("POST GLUE", 3);
    compOrderCombo.addItem("POST PUNCH", 4);
    compOrderCombo.setSelectedId(3, juce::dontSendNotification);
    compOrderCombo.onChange = [this]
    {
        if (isSyncingUi)
            return;

        applyCompressorUiToState();
        emitSoundLayerChange();
    };
    addAndMakeVisible(compOrderCombo);
    setupActionButton(compPowerButton, false);
    compPowerButton.setClickingTogglesState(true);
    compPowerButton.onClick = [this]
    {
        if (isSyncingUi)
            return;

        if (compPowerButton.getToggleState())
            ensureCompressorDefaultsForActivation();

        applyCompressorUiToState();
        emitSoundLayerChange();
    };
    addAndMakeVisible(compPowerButton);
    setupDial(compRatioSlider, 1.5, 20.0, 0.1, amberBright());
    setupDial(compThresholdSlider, -36.0, -6.0, 0.1, copper());
    setupDial(compMixSlider, 0.0, 100.0, 1.0, amber());
    setupDial(compAttackSlider, 2.0, 30.0, 0.1, amberBright());
    setupDial(compReleaseSlider, 40.0, 300.0, 1.0, amber());
    setupDial(compSaturationSlider, 0.0, 100.0, 1.0, copper());
    compRatioSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        compressorUiState.ratio = compRatioSlider.getValue();
        applyCompressorUiToState();
        emitSoundLayerChange();
    };
    compThresholdSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        compressorUiState.threshold = compThresholdSlider.getValue();
        handleGateAmountChange(static_cast<float>((-6.0 - compThresholdSlider.getValue()) / 30.0));
    };
    compMixSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        compressorUiState.mix = compMixSlider.getValue();
        handleCompressionAmountChange(compPowerButton.getToggleState() ? static_cast<float>(compMixSlider.getValue() / 100.0) : 0.0f);
    };
    compAttackSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        compressorUiState.attack = compAttackSlider.getValue();
        applyCompressorUiToState();
        emitSoundLayerChange();
    };
    compReleaseSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        compressorUiState.release = compReleaseSlider.getValue();
        applyCompressorUiToState();
        emitSoundLayerChange();
    };
    compSaturationSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        compressorUiState.saturation = compSaturationSlider.getValue();
        handleDriveAmountChange(static_cast<float>(compSaturationSlider.getValue() / 100.0));
    };
    addAndMakeVisible(compRatioSlider);
    addAndMakeVisible(compThresholdSlider);
    addAndMakeVisible(compMixSlider);
    addAndMakeVisible(compAttackSlider);
    addAndMakeVisible(compReleaseSlider);
    addAndMakeVisible(compSaturationSlider);

    styleHeaderLabel(reverbSectionLabel, "REVERB", 13.0f);
    addAndMakeVisible(reverbSectionLabel);
    styleMicroLabel(reverbDescriptorLabel, "Tight room and depth", 10.0f);
    addAndMakeVisible(reverbDescriptorLabel);
    styleMicroLabel(reverbSizeLabel, "SIZE");
    addAndMakeVisible(reverbSizeLabel);
    styleMicroLabel(reverbMixLabel, "MIX");
    addAndMakeVisible(reverbMixLabel);
    styleMicroLabel(reverbPredelayLabel, "PREDELAY");
    addAndMakeVisible(reverbPredelayLabel);
    styleMicroLabel(reverbTailLabel, "ER / TAIL");
    addAndMakeVisible(reverbTailLabel);
    styleValueLabel(reverbSizeValueLabel);
    addAndMakeVisible(reverbSizeValueLabel);
    styleValueLabel(reverbMixValueLabel);
    addAndMakeVisible(reverbMixValueLabel);
    styleValueLabel(reverbPredelayValueLabel);
    addAndMakeVisible(reverbPredelayValueLabel);
    styleValueLabel(reverbTailValueLabel);
    addAndMakeVisible(reverbTailValueLabel);
    setupDial(reverbSizeSlider, 0.0, 100.0, 1.0, amber());
    setupDial(reverbMixSlider, 0.0, 100.0, 1.0, amberBright());
    setupDial(reverbPredelaySlider, 0.0, 60.0, 1.0, copper());
    setupDial(reverbTailSlider, 0.0, 100.0, 1.0, amber());
    reverbSizeSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        reverbUiState.size = reverbSizeSlider.getValue();
        handleReverbAmountChange(static_cast<float>(reverbMixSlider.getValue() / 100.0));
    };
    reverbMixSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        reverbUiState.mix = reverbMixSlider.getValue();
        handleReverbAmountChange(static_cast<float>(reverbMixSlider.getValue() / 100.0));
    };
    reverbPredelaySlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        reverbUiState.predelay = reverbPredelaySlider.getValue();
        handleReverbAmountChange(static_cast<float>(reverbMixSlider.getValue() / 100.0));
    };
    reverbTailSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        reverbUiState.tail = reverbTailSlider.getValue();
        handleReverbAmountChange(static_cast<float>(reverbMixSlider.getValue() / 100.0));
    };
    addAndMakeVisible(reverbSizeSlider);
    addAndMakeVisible(reverbMixSlider);
    addAndMakeVisible(reverbPredelaySlider);
    addAndMakeVisible(reverbTailSlider);

    styleHeaderLabel(monstaSectionLabel, "MONSTAFX", 13.0f);
    addAndMakeVisible(monstaSectionLabel);
    styleMicroLabel(monstaDescriptorLabel, "Reverse burn / glitch chaos / VHS melt", 10.0f);
    addAndMakeVisible(monstaDescriptorLabel);
    styleMicroLabel(monstaDryLabel, "DRY");
    addAndMakeVisible(monstaDryLabel);
    styleMicroLabel(monstaWetLabel, "WET");
    addAndMakeVisible(monstaWetLabel);
    styleValueLabel(monstaDryValueLabel);
    addAndMakeVisible(monstaDryValueLabel);
    styleValueLabel(monstaWetValueLabel);
    addAndMakeVisible(monstaWetValueLabel);
    setupDial(monstaDrySlider, 0.0, 100.0, 1.0, steel());
    setupDial(monstaWetSlider, 0.0, 100.0, 1.0, danger());
    monstaDrySlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        monstaUiState.dry = monstaDrySlider.getValue();
        emitSoundLayerChange();
    };
    monstaWetSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        monstaUiState.wet = monstaWetSlider.getValue();
        emitSoundLayerChange();
    };
    addAndMakeVisible(monstaDrySlider);
    addAndMakeVisible(monstaWetSlider);

    setupActionButton(monstaChaosButton, true);
    monstaChaosButton.setButtonText("REROLL");
    monstaChaosButton.onClick = [this]
    {
        if (isSyncingUi)
            return;

        if (monstaWetSlider.getValue() < 1.0)
        {
            monstaUiState.dry = juce::jmax(monstaUiState.dry, 100.0);
            monstaUiState.wet = 38.0;
            syncMonstaFxVisuals();
        }

        auto nextSeed = static_cast<std::uint32_t>(juce::Random::getSystemRandom().nextInt())
            ^ static_cast<std::uint32_t>(juce::Time::getMillisecondCounter());
        if (nextSeed == 0)
            nextSeed = 1u;

        currentSoundState.monstaFx.chaosSeed = nextSeed;
        currentSoundState.monstaFx.pendingChaosReseed = true;
        emitSoundLayerChange();
        currentSoundState.monstaFx.pendingChaosReseed = false;
    };
    addAndMakeVisible(monstaChaosButton);

    styleHeaderLabel(transientSectionLabel, "TRANSIENT", 13.5f);
    addAndMakeVisible(transientSectionLabel);
    styleMicroLabel(transientDescriptorLabel, "Fast hit / body hold / trim", 10.0f);
    addAndMakeVisible(transientDescriptorLabel);
    styleMicroLabel(transientAttackLabel, "ATTACK");
    addAndMakeVisible(transientAttackLabel);
    styleMicroLabel(transientSustainLabel, "SUSTAIN");
    addAndMakeVisible(transientSustainLabel);
    styleMicroLabel(transientGainLabel, "GAIN");
    addAndMakeVisible(transientGainLabel);
    styleValueLabel(transientAttackValueLabel);
    addAndMakeVisible(transientAttackValueLabel);
    styleValueLabel(transientSustainValueLabel);
    addAndMakeVisible(transientSustainValueLabel);
    styleValueLabel(transientGainValueLabel);
    addAndMakeVisible(transientGainValueLabel);
    setupDial(transientAttackSlider, 0.0, 100.0, 1.0, amberBright());
    setupDial(transientSustainSlider, 0.0, 100.0, 1.0, amber());
    setupDial(transientGainSlider, -6.0, 12.0, 0.1, copper());
    transientAttackSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        transientUiState.attack = transientAttackSlider.getValue();
        handleTransientAmountChange(static_cast<float>(transientAttackSlider.getValue() / 100.0));
    };
    transientSustainSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        transientUiState.sustain = transientSustainSlider.getValue();
        currentSoundState.drumTransient.sustain = juce::jlimit(0.0f, 1.0f, static_cast<float>(transientUiState.sustain / 100.0));
        emitSoundLayerChange();
    };
    transientGainSlider.onValueChange = [this]
    {
        if (isSyncingUi)
            return;

        transientUiState.gain = transientGainSlider.getValue();
        currentSoundState.drumTransient.gainDb = static_cast<float>(transientUiState.gain);
        emitSoundLayerChange();
    };
    addAndMakeVisible(transientAttackSlider);
    addAndMakeVisible(transientSustainSlider);
    addAndMakeVisible(transientGainSlider);

    setupActionButton(smoothButton, false);
    smoothButton.setClickingTogglesState(true);
    smoothButton.onClick = [this]
    {
        if (isSyncingUi)
            return;

        currentSoundState.drumTransient.smooth = smoothButton.getToggleState();
        syncTransientVisuals();
        emitSoundLayerChange();
    };
    addAndMakeVisible(smoothButton);

    setupActionButton(limitButton, false);
    limitButton.setClickingTogglesState(true);
    limitButton.onClick = [this]
    {
        if (isSyncingUi)
            return;

        currentSoundState.drumTransient.limit = limitButton.getToggleState();
        syncTransientVisuals();
        emitSoundLayerChange();
    };
    addAndMakeVisible(limitButton);

    setupStripButton(compStripButton, false);
    compStripButton.onClick = [this] { toggleLayoutMode(LayoutMode::FocusComp); };
    addAndMakeVisible(compStripButton);
    setupStripButton(reverbStripButton, false);
    reverbStripButton.onClick = [this] { toggleLayoutMode(LayoutMode::FocusReverb); };
    addAndMakeVisible(reverbStripButton);
    setupStripButton(monstaStripButton, false);
    monstaStripButton.onClick = [this] { toggleLayoutMode(LayoutMode::FocusMonsta); };
    addAndMakeVisible(monstaStripButton);
    setupStripButton(transientStripButton, false);
    transientStripButton.onClick = [this] { toggleLayoutMode(LayoutMode::FocusTransient); };
    addAndMakeVisible(transientStripButton);
    setupStripButton(eqStripButton, false);
    eqStripButton.onClick = [this] { toggleLayoutMode(LayoutMode::FocusEq); };
    addAndMakeVisible(eqStripButton);
    setupStripButton(stereoStripButton, false);
    stereoStripButton.onClick = [this] { toggleLayoutMode(LayoutMode::FocusStereo); };
    addAndMakeVisible(stereoStripButton);

    targetCombo.onChange = [this]
    {
        const int id = targetCombo.getSelectedId();
        SoundTargetDescriptor target = SoundTargetDescriptor::makeGlobal();
        if (id >= 1)
        {
            const int index = id - 1;
            if (index >= 0 && index < static_cast<int>(targetDescriptors.size()))
                target = targetDescriptors[static_cast<size_t>(index)];
        }

        currentTarget = target;
        if (onSoundTargetChanged)
            onSoundTargetChanged(currentTarget);
    };

    auto moveToContentCanvas = [this](std::initializer_list<juce::Component*> components)
    {
        for (auto* component : components)
            contentCanvas.addAndMakeVisible(component);
    };

    moveToContentCanvas({ &eqSectionLabel, &eqDescriptorLabel, &eqBandMeaningLabel, &eqBandRangeLabel, &eqOrderLabel, &eqBandLabel,
                          &eqShapeLabel, &eqFreqLabel, &eqFreqValueLabel, &eqGainLabel, &eqGainValueLabel, &eqQLabel, &eqQValueLabel,
                          &eqOrderCombo, &eqBandCombo, &eqShapeCombo, &eqEnableButton, &eqFreqSlider, &eqGainSlider, &eqQSlider,
                          &stereoSectionLabel, &stereoDescriptorLabel, &stereoInputLabel, &stereoOutputLabel, &panLabel,
                          &panValueLabel, &widthLabel, &widthValueLabel, &stereoFocusLabel, &stereoFocusValueLabel,
                          &stereoEdgeLabel, &stereoEdgeValueLabel, &stereoLowCenterProtectLabel, &stereoLowCenterProtectValueLabel,
                          &stereoAirSpreadLabel, &stereoAirSpreadValueLabel, &panSlider, &widthSlider, &stereoFocusSlider,
                          &stereoEdgeSlider, &stereoLowCenterProtectSlider, &stereoAirSpreadSlider, &stereoMonoSafeButton,
                          &compSectionLabel, &compDescriptorLabel, &compOrderLabel, &compRatioLabel, &compRatioValueLabel,
                          &compThresholdLabel, &compThresholdValueLabel, &compMixLabel, &compMixValueLabel, &compAttackLabel,
                          &compAttackValueLabel, &compReleaseLabel, &compReleaseValueLabel, &compSaturationLabel,
                          &compSaturationValueLabel, &compOrderCombo, &compPowerButton, &compRatioSlider, &compThresholdSlider,
                          &compMixSlider, &compAttackSlider, &compReleaseSlider, &compSaturationSlider,
                          &reverbSectionLabel, &reverbDescriptorLabel, &reverbSizeLabel, &reverbSizeValueLabel, &reverbMixLabel,
                          &reverbMixValueLabel, &reverbPredelayLabel, &reverbPredelayValueLabel, &reverbTailLabel,
                          &reverbTailValueLabel, &reverbSizeSlider, &reverbMixSlider, &reverbPredelaySlider, &reverbTailSlider,
                          &monstaSectionLabel, &monstaDescriptorLabel, &monstaDryLabel, &monstaDryValueLabel,
                          &monstaWetLabel, &monstaWetValueLabel, &monstaDrySlider, &monstaWetSlider, &monstaChaosButton,
                          &transientSectionLabel, &transientDescriptorLabel, &transientAttackLabel, &transientAttackValueLabel,
                          &transientSustainLabel, &transientSustainValueLabel, &transientGainLabel, &transientGainValueLabel,
                          &transientAttackSlider, &transientSustainSlider, &transientGainSlider, &smoothButton, &limitButton });

    syncVisualStateFromCurrentSoundState();
    updateStripSelection();
    updateValueLabels();
    updateChainSummary();
}

SoundModuleComponent::~SoundModuleComponent()
{
    contentCanvas.removeMouseListener(this);
    verticalScrollBar.removeListener(this);
    setLookAndFeel(nullptr);
}

void SoundModuleComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient shell(shellBase(), 0.0f, 0.0f, shellRaised(), 0.0f, bounds.getBottom(), false);
    shell.addColour(0.45, panelInset());
    shell.addColour(0.78, shellBase().brighter(0.06f));
    g.setGradientFill(shell);
    g.fillRoundedRectangle(bounds.reduced(1.0f), 20.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 16));
    g.drawRoundedRectangle(bounds.reduced(1.5f), 20.0f, 1.0f);
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 90));
    g.drawRoundedRectangle(bounds.reduced(3.0f), 18.0f, 1.0f);

    auto drawModule = [&](const juce::Rectangle<int>& rect, juce::Colour accent, bool strong)
    {
        if (rect.isEmpty())
            return;

        auto r = rect.toFloat();
        g.setColour(juce::Colours::black.withAlpha(strong ? 0.34f : 0.24f));
        g.fillRoundedRectangle(r.translated(0.0f, 3.0f), strong ? 18.0f : 14.0f);

        juce::ColourGradient fill(panelRaised().brighter(strong ? 0.24f : 0.14f), r.getX(), r.getY(), panelInset(), r.getX(), r.getBottom(), false);
        fill.addColour(0.50, panelBase());
        fill.addColour(0.82, shellBase());
        g.setGradientFill(fill);
        g.fillRoundedRectangle(r, strong ? 18.0f : 14.0f);

        auto topStrip = r.removeFromTop(strong ? 12.0f : 9.0f);
        juce::ColourGradient accentGlow(accent.withAlpha(strong ? 0.55f : 0.34f), topStrip.getX(), topStrip.getY(), juce::Colours::transparentBlack, topStrip.getX(), topStrip.getBottom(), false);
        g.setGradientFill(accentGlow);
        g.fillRoundedRectangle(topStrip, 10.0f);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, strong ? 24 : 14));
        g.drawRoundedRectangle(rect.toFloat(), strong ? 18.0f : 14.0f, 1.0f);

        drawScrew(g, rect.getTopLeft().toFloat() + juce::Point<float>(12.0f, 12.0f));
        drawScrew(g, juce::Point<float>(static_cast<float>(rect.getRight() - 12), static_cast<float>(rect.getY() + 12)));
        drawScrew(g, juce::Point<float>(static_cast<float>(rect.getX() + 12), static_cast<float>(rect.getBottom() - 12)));
        drawScrew(g, juce::Point<float>(static_cast<float>(rect.getRight() - 12), static_cast<float>(rect.getBottom() - 12)));
    };

    const bool overviewMode = layoutMode == LayoutMode::Overview;
    drawModule(topBarBounds, amber(), false);
    drawModule(moduleStripBounds, copper(), false);

    auto drawModuleCard = [&](const ModuleCardLayout& layout, juce::Colour accent, bool strong)
    {
        if (layout.bounds.isEmpty())
            return;

        const auto boundsRect = toDisplaySpace(layout.bounds).toFloat();
        auto headerRect = toDisplaySpace(layout.headerBounds).toFloat();
        const auto contentRect = toDisplaySpace(layout.contentBounds).toFloat();
        const float radius = strong ? 16.0f : 14.0f;

        g.setColour(juce::Colours::black.withAlpha(strong ? 0.30f : 0.22f));
        g.fillRoundedRectangle(boundsRect.translated(0.0f, 3.0f), radius);

        juce::ColourGradient fill(panelRaised().brighter(strong ? 0.18f : 0.12f), boundsRect.getX(), boundsRect.getY(), panelInset(), boundsRect.getX(), boundsRect.getBottom(), false);
        fill.addColour(0.52, panelBase());
        fill.addColour(0.84, shellBase());
        g.setGradientFill(fill);
        g.fillRoundedRectangle(boundsRect, radius);

        g.setColour(accent.withAlpha(strong ? 0.22f : 0.16f));
        g.fillRoundedRectangle(headerRect, 10.0f);

        g.setColour(accent.withAlpha(strong ? 0.58f : 0.40f));
        g.fillRoundedRectangle(headerRect.removeFromTop(3.0f), 3.0f);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, strong ? 22 : 14));
        g.drawRoundedRectangle(boundsRect, radius, 1.0f);
        g.setColour(accent.withAlpha(0.24f));
        g.drawLine(contentRect.getX(), contentRect.getY() - 6.0f, contentRect.getRight(), contentRect.getY() - 6.0f, 1.0f);

        drawScrew(g, boundsRect.getTopLeft() + juce::Point<float>(12.0f, 12.0f));
        drawScrew(g, juce::Point<float>(boundsRect.getRight() - 12.0f, boundsRect.getY() + 12.0f));
        drawScrew(g, juce::Point<float>(boundsRect.getX() + 12.0f, boundsRect.getBottom() - 12.0f));
        drawScrew(g, juce::Point<float>(boundsRect.getRight() - 12.0f, boundsRect.getBottom() - 12.0f));
    };

    const bool eqExpanded = layoutMode == LayoutMode::FocusEq || isOverviewCardExpanded(OverviewCard::Eq);
    const bool compExpanded = layoutMode == LayoutMode::FocusComp || isOverviewCardExpanded(OverviewCard::Comp);
    const bool stereoExpanded = layoutMode == LayoutMode::FocusStereo || isOverviewCardExpanded(OverviewCard::Stereo);
    const bool reverbExpanded = layoutMode == LayoutMode::FocusReverb || isOverviewCardExpanded(OverviewCard::Reverb);
    const bool monstaExpanded = layoutMode == LayoutMode::FocusMonsta || isOverviewCardExpanded(OverviewCard::MonstaFx);
    const bool transientExpanded = layoutMode == LayoutMode::FocusTransient || isOverviewCardExpanded(OverviewCard::Transient);

    g.saveState();
    g.reduceClipRegion(contentViewportBounds);
    drawModuleCard(eqCardLayout, amberBright(), eqExpanded);
    drawModuleCard(compCardLayout, copper(), compExpanded);
    drawModuleCard(stereoCardLayout, amberBright(), stereoExpanded);
    drawModuleCard(reverbCardLayout, amber(), reverbExpanded);
    drawModuleCard(monstaCardLayout, danger(), monstaExpanded);
    drawModuleCard(transientCardLayout, copper(), transientExpanded);

    auto clipToCardContent = [&](const ModuleCardLayout& layout)
    {
        g.saveState();
        g.reduceClipRegion(toDisplaySpace(layout.contentBounds));
    };

    if (!eqCardLayout.contentBounds.isEmpty())
    {
        clipToCardContent(eqCardLayout);
        if (overviewMode && !eqExpanded)
        {
            auto summaryArea = toDisplaySpace(eqCardLayout.contentBounds).reduced(0, 2);
            drawCardSummaryText(g,
                                summaryArea,
                                buildEqCollapsedCharacter(currentSoundState.eq),
                                buildEqCollapsedSummary(currentSoundState.eq),
                                amberBright());
        }
        else if (!eqDisplayBounds.isEmpty())
        {
            drawEqResponseDisplay(g, toDisplaySpace(eqDisplayBounds), currentSoundState.eq, eqDisplayAnalyzerState, hoveredEqBandIndex);
        }
        g.restoreState();
    }

    if (!stereoCardLayout.contentBounds.isEmpty())
    {
        clipToCardContent(stereoCardLayout);
        if (overviewMode && !stereoExpanded)
        {
            auto summaryArea = toDisplaySpace(stereoCardLayout.contentBounds).reduced(0, 2);
            drawCardSummaryText(g,
                                summaryArea,
                                buildStereoCollapsedCharacter(currentSoundState.stereoFieldMonoSafe,
                                                             currentSoundState.stereoFieldLowCenterProtect,
                                                             currentSoundState.stereoFieldAirSpread),
                                buildStereoCollapsedSummary(static_cast<float>(panSlider.getValue()),
                                                           static_cast<float>(widthSlider.getValue())),
                                amberBright());
        }
        else if (!stereoDisplayBounds.isEmpty())
        {
            drawStereoPlaceholder(g,
                                  toDisplaySpace(stereoDisplayBounds),
                                  static_cast<float>(panSlider.getValue()),
                                  static_cast<float>(widthSlider.getValue()));
        }
        g.restoreState();
    }

    if (!compCardLayout.contentBounds.isEmpty())
    {
        clipToCardContent(compCardLayout);
        if (overviewMode && !compExpanded)
        {
            auto summaryArea = toDisplaySpace(compCardLayout.contentBounds).reduced(0, 2);
            drawCardSummaryText(g,
                                summaryArea,
                                buildCompressorCollapsedCharacter(currentSoundState.compressor),
                                buildCompressorCollapsedSummary(currentSoundState.compressor),
                                copper());
        }
        g.restoreState();
    }

    if (!reverbCardLayout.contentBounds.isEmpty())
    {
        clipToCardContent(reverbCardLayout);
        auto content = toDisplaySpace(reverbCardLayout.contentBounds);
        if (overviewMode && !reverbExpanded)
        {
            drawCardSummaryText(g,
                                content,
                                buildReverbCollapsedCharacter(reverbUiState.size, reverbUiState.tail),
                                buildReverbCollapsedSummary(reverbUiState.size, reverbUiState.mix, reverbUiState.predelay),
                                amber());
        }
        else
        {
            auto halo = content.removeFromTop(56).removeFromRight(92).toFloat().reduced(4.0f, 2.0f);
            g.setColour(amber().withAlpha(0.12f));
            g.drawEllipse(halo, 1.0f);
            g.drawEllipse(halo.reduced(8.0f), 1.0f);
            g.drawEllipse(halo.reduced(16.0f), 1.0f);
            g.setColour(amberBright().withAlpha(0.42f));
            g.fillEllipse(halo.getCentreX() - 2.0f, halo.getCentreY() - 2.0f, 4.0f, 4.0f);
        }
        g.restoreState();
    }

    if (!monstaCardLayout.contentBounds.isEmpty())
    {
        clipToCardContent(monstaCardLayout);
        if (overviewMode && !monstaExpanded)
        {
            auto summaryArea = toDisplaySpace(monstaCardLayout.contentBounds).reduced(0, 2);
            drawCardSummaryText(g,
                                summaryArea,
                                buildMonstaCollapsedCharacter(currentSoundState.monstaFx),
                                buildMonstaCollapsedSummary(currentSoundState.monstaFx),
                                danger());
        }
        g.restoreState();
    }

    if (!transientCardLayout.contentBounds.isEmpty())
    {
        clipToCardContent(transientCardLayout);
        if (overviewMode && !transientExpanded)
        {
            auto summaryArea = toDisplaySpace(transientCardLayout.contentBounds).reduced(0, 2);
            drawCardSummaryText(g,
                                summaryArea,
                                buildTransientCollapsedCharacter(smoothButton.getToggleState(), limitButton.getToggleState()),
                                buildTransientCollapsedSummary(transientUiState.attack, transientUiState.sustain, transientUiState.gain),
                                copper());
        }
        else if (!transientDisplayBounds.isEmpty())
        {
            drawTransientPlaceholder(g,
                                     toDisplaySpace(transientDisplayBounds),
                                     static_cast<float>(transientUiState.attack),
                                     static_cast<float>(transientUiState.sustain),
                                     static_cast<float>(transientUiState.gain));
        }
        g.restoreState();
    }
    g.restoreState();
}

void SoundModuleComponent::resized()
{
    eqDisplayBounds = {};
    stereoDisplayBounds = {};
    transientDisplayBounds = {};
    contentViewportBounds = {};
    eqBounds = {};
    stereoBounds = {};
    compBounds = {};
    reverbBounds = {};
    monstaBounds = {};
    transientBounds = {};
    eqCardLayout = {};
    stereoCardLayout = {};
    compCardLayout = {};
    reverbCardLayout = {};
    monstaCardLayout = {};
    transientCardLayout = {};

    auto area = getLocalBounds().reduced(14);
    topBarBounds = area.removeFromTop(92);
    area.removeFromTop(12);

    moduleStripBounds = area.removeFromBottom(42);
    area.removeFromBottom(10);
    const int scrollBarWidth = 10;
    auto scrollArea = area;
    auto scrollBarBounds = scrollArea.removeFromRight(scrollBarWidth);
    scrollArea.removeFromRight(6);
    contentViewportBounds = scrollArea;
    contentViewport.setBounds(contentViewportBounds);

    const bool overviewMode = layoutMode == LayoutMode::Overview;
    contentHeight = contentViewportBounds.getHeight();
    if (overviewMode)
    {
        const int eqHeight = isOverviewCardExpanded(OverviewCard::Eq) ? 244 : 104;
        const int compHeight = isOverviewCardExpanded(OverviewCard::Comp) ? 250 : 104;
        const int stereoHeight = isOverviewCardExpanded(OverviewCard::Stereo) ? 232 : 104;
        const int reverbHeight = isOverviewCardExpanded(OverviewCard::Reverb) ? 192 : 104;
        const int monstaHeight = isOverviewCardExpanded(OverviewCard::MonstaFx) ? 178 : 104;
        const int transientHeight = isOverviewCardExpanded(OverviewCard::Transient) ? 192 : 104;
        contentHeight = juce::jmax(contentViewportBounds.getHeight(),
                                   juce::jmax(eqHeight, compHeight)
                                       + juce::jmax(stereoHeight, reverbHeight)
                                       + juce::jmax(monstaHeight, transientHeight)
                                       + 34);
    }
    else if (layoutMode == LayoutMode::FocusEq)
    {
        contentHeight = juce::jmax(contentViewportBounds.getHeight() + 120,
                                   juce::roundToInt(contentViewportBounds.getHeight() * 1.18f));
    }

    const int maxScrollOffset = juce::jmax(0, contentHeight - contentViewportBounds.getHeight());
    scrollOffsetY = juce::jlimit(0, maxScrollOffset, scrollOffsetY);
    contentCanvas.setBounds(0, -scrollOffsetY, contentViewportBounds.getWidth(), contentHeight);

    if (maxScrollOffset > 0)
    {
        verticalScrollBar.setVisible(true);
        verticalScrollBar.setBounds(scrollBarBounds);
        verticalScrollBar.setRangeLimits(0.0, static_cast<double>(contentHeight));
        verticalScrollBar.setCurrentRange(static_cast<double>(scrollOffsetY), static_cast<double>(contentViewportBounds.getHeight()));
    }
    else
    {
        verticalScrollBar.setVisible(false);
        verticalScrollBar.setBounds({});
    }

    auto assignCardLayout = [](ModuleCardLayout& layout, juce::Rectangle<int> bounds, int headerHeight)
    {
        layout.bounds = bounds;
        if (bounds.isEmpty())
        {
            layout.headerBounds = {};
            layout.contentBounds = {};
            return;
        }

        const bool compactOverviewCard = headerHeight <= 24;
        auto inner = compactOverviewCard ? bounds.reduced(14, 10)
                                        : bounds.reduced(16, 14);
        layout.headerBounds = inner.removeFromTop(headerHeight);
        inner.removeFromTop(compactOverviewCard ? 6 : 8);
        layout.contentBounds = inner;
    };

    auto contentArea = juce::Rectangle<int>(0, 0, contentViewportBounds.getWidth(), contentHeight);

    if (overviewMode)
    {
        const int gap = 10;
        const int leftWidth = (contentArea.getWidth() - gap) / 2;
        const int rightWidth = contentArea.getWidth() - leftWidth - gap;
        const int eqHeight = isOverviewCardExpanded(OverviewCard::Eq) ? 224 : 92;
        const int compHeight = isOverviewCardExpanded(OverviewCard::Comp) ? 228 : 92;
        const int stereoHeight = isOverviewCardExpanded(OverviewCard::Stereo) ? 210 : 92;
        const int reverbHeight = isOverviewCardExpanded(OverviewCard::Reverb) ? 174 : 92;
        const int monstaHeight = isOverviewCardExpanded(OverviewCard::MonstaFx) ? 162 : 92;
        const int transientHeight = isOverviewCardExpanded(OverviewCard::Transient) ? 170 : 92;
        const int row1Height = juce::jmax(eqHeight, compHeight);
        const int row2Height = juce::jmax(stereoHeight, reverbHeight);
        const int row3Height = juce::jmax(monstaHeight, transientHeight);
        int y = 0;
        eqBounds = { 0, y, leftWidth, eqHeight };
        compBounds = { leftWidth + gap, y, rightWidth, compHeight };
        y += row1Height + gap;
        stereoBounds = { 0, y, leftWidth, stereoHeight };
        reverbBounds = { leftWidth + gap, y, rightWidth, reverbHeight };
        y += row2Height + gap;
        monstaBounds = { 0, y, leftWidth, monstaHeight };
        transientBounds = { leftWidth + gap, y, rightWidth, transientHeight };
        juce::ignoreUnused(row3Height);
    }
    else
    {
        switch (layoutMode)
        {
            case LayoutMode::FocusEq:
                eqBounds = contentArea;
                break;
            case LayoutMode::FocusStereo:
                stereoBounds = contentArea;
                break;
            case LayoutMode::FocusComp:
                compBounds = contentArea;
                break;
            case LayoutMode::FocusReverb:
                reverbBounds = contentArea;
                break;
            case LayoutMode::FocusMonsta:
                monstaBounds = contentArea;
                break;
            case LayoutMode::FocusTransient:
                transientBounds = contentArea;
                break;
            case LayoutMode::Overview:
            default:
                break;
        }
    }

    const int overviewHeaderHeight = overviewMode ? 24 : 28;
    assignCardLayout(eqCardLayout, eqBounds, overviewHeaderHeight);
    assignCardLayout(stereoCardLayout, stereoBounds, overviewHeaderHeight);
    assignCardLayout(compCardLayout, compBounds, overviewHeaderHeight);
    assignCardLayout(reverbCardLayout, reverbBounds, overviewHeaderHeight);
    assignCardLayout(monstaCardLayout, monstaBounds, overviewHeaderHeight);
    assignCardLayout(transientCardLayout, transientBounds, overviewHeaderHeight);

    const bool showEq = overviewMode || layoutMode == LayoutMode::FocusEq;
    const bool showStereo = overviewMode || layoutMode == LayoutMode::FocusStereo;
    const bool showComp = overviewMode || layoutMode == LayoutMode::FocusComp;
    const bool showReverb = overviewMode || layoutMode == LayoutMode::FocusReverb;
    const bool showMonsta = overviewMode || layoutMode == LayoutMode::FocusMonsta;
    const bool showTransient = overviewMode || layoutMode == LayoutMode::FocusTransient;
    const bool showCompDescriptor = showComp && !isOverviewCardExpanded(OverviewCard::Comp);
    const bool showEqExpandedControls = layoutMode == LayoutMode::FocusEq || isOverviewCardExpanded(OverviewCard::Eq);
    const bool showStereoExpandedControls = layoutMode == LayoutMode::FocusStereo || isOverviewCardExpanded(OverviewCard::Stereo);
    const bool showStereoAdvancedControls = layoutMode == LayoutMode::FocusStereo;
    const bool showCompExpandedControls = layoutMode == LayoutMode::FocusComp || isOverviewCardExpanded(OverviewCard::Comp);
    const bool showReverbExpandedControls = layoutMode == LayoutMode::FocusReverb || isOverviewCardExpanded(OverviewCard::Reverb);
    const bool showMonstaExpandedControls = layoutMode == LayoutMode::FocusMonsta || isOverviewCardExpanded(OverviewCard::MonstaFx);
    const bool showTransientExpandedControls = layoutMode == LayoutMode::FocusTransient || isOverviewCardExpanded(OverviewCard::Transient);

    auto setModuleVisibility = [](bool shouldShow, std::initializer_list<juce::Component*> components)
    {
        for (auto* component : components)
        {
            component->setVisible(shouldShow);
            if (!shouldShow)
                component->setBounds({});
        }
    };

        setModuleVisibility(showEq, { &eqSectionLabel, &eqDescriptorLabel, &eqEnableButton });
        setModuleVisibility(showEqExpandedControls,
                                                { &eqBandMeaningLabel, &eqBandRangeLabel, &eqOrderLabel, &eqBandLabel, &eqShapeLabel, &eqFreqLabel,
                                                    &eqFreqValueLabel, &eqGainLabel, &eqGainValueLabel, &eqQLabel, &eqQValueLabel, &eqOrderCombo,
                                                    &eqBandCombo, &eqShapeCombo, &eqFreqSlider, &eqGainSlider, &eqQSlider });
        setModuleVisibility(showStereo, { &stereoSectionLabel, &stereoDescriptorLabel });
        setModuleVisibility(showStereoExpandedControls,
                                                { &stereoInputLabel, &stereoOutputLabel, &panLabel, &panValueLabel, &widthLabel, &widthValueLabel,
                                                    &stereoFocusLabel, &stereoFocusValueLabel, &stereoEdgeLabel, &stereoEdgeValueLabel,
                                                    &panSlider, &widthSlider, &stereoFocusSlider, &stereoEdgeSlider, &stereoMonoSafeButton });
        setModuleVisibility(showStereoAdvancedControls,
                                                { &stereoLowCenterProtectLabel, &stereoLowCenterProtectValueLabel, &stereoAirSpreadLabel,
                                                    &stereoAirSpreadValueLabel, &stereoLowCenterProtectSlider, &stereoAirSpreadSlider });
        setModuleVisibility(showComp, { &compSectionLabel, &compPowerButton });
        setModuleVisibility(showCompDescriptor, { &compDescriptorLabel });
        setModuleVisibility(showCompExpandedControls,
                                                { &compOrderLabel, &compRatioLabel, &compRatioValueLabel, &compThresholdLabel, &compThresholdValueLabel,
                                                    &compMixLabel, &compMixValueLabel, &compAttackLabel, &compAttackValueLabel, &compReleaseLabel,
                                                    &compReleaseValueLabel, &compSaturationLabel, &compSaturationValueLabel, &compOrderCombo,
                                                    &compRatioSlider, &compThresholdSlider, &compMixSlider, &compAttackSlider, &compReleaseSlider,
                                                    &compSaturationSlider });
        setModuleVisibility(showReverb, { &reverbSectionLabel, &reverbDescriptorLabel });
        setModuleVisibility(showReverbExpandedControls,
                                                { &reverbSizeLabel, &reverbSizeValueLabel, &reverbMixLabel, &reverbMixValueLabel, &reverbPredelayLabel,
                                                    &reverbPredelayValueLabel, &reverbTailLabel, &reverbTailValueLabel, &reverbSizeSlider, &reverbMixSlider,
                                                    &reverbPredelaySlider, &reverbTailSlider });
        setModuleVisibility(showMonsta, { &monstaSectionLabel, &monstaDescriptorLabel });
        setModuleVisibility(showMonstaExpandedControls,
                                                { &monstaDryLabel, &monstaDryValueLabel, &monstaWetLabel, &monstaWetValueLabel,
                                                    &monstaDrySlider, &monstaWetSlider, &monstaChaosButton });
        setModuleVisibility(showTransient, { &transientSectionLabel, &transientDescriptorLabel });
        setModuleVisibility(showTransientExpandedControls,
                                                { &transientAttackLabel, &transientAttackValueLabel, &transientSustainLabel, &transientSustainValueLabel,
                                                    &transientGainLabel, &transientGainValueLabel, &transientAttackSlider, &transientSustainSlider,
                                                    &transientGainSlider, &smoothButton, &limitButton });

    auto layoutKnob = [](juce::Rectangle<int> area, juce::Label& name, juce::Label& value, juce::Slider& slider)
    {
        auto top = area.removeFromTop(18);
        value.setBounds(top.removeFromRight(76));
        name.setBounds(top);
        area.removeFromTop(2);
        slider.setBounds(area);
    };

    {
        auto top = topBarBounds.reduced(16, 14);
        auto header = top.removeFromTop(24);
        titleLabel.setBounds(header.removeFromLeft(148));
        brandLabel.setBounds(header.removeFromLeft(104));
        auto controls = header;
        resetButton.setBounds(controls.removeFromRight(82));
        controls.removeFromRight(8);
        bypassButton.setBounds(controls.removeFromRight(82));
        controls.removeFromRight(10);
        targetModeLabel.setBounds(controls.removeFromRight(76));
        controls.removeFromRight(10);
        targetCombo.setBounds(controls.removeFromRight(240));
        controls.removeFromRight(10);
        targetLabel.setBounds(controls.removeFromRight(58));

        top.removeFromTop(8);
        targetSummaryLabel.setBounds(top.removeFromTop(18));
        top.removeFromTop(4);
        auto footer = top.removeFromTop(18);
        targetStatusLabel.setBounds(footer.removeFromLeft(juce::roundToInt(footer.getWidth() * 0.56f)));
        chainSummaryLabel.setBounds(footer);
    }

    if (showEq)
    {
        auto header = eqCardLayout.headerBounds;
        const bool focusEq = layoutMode == LayoutMode::FocusEq;
        eqSectionLabel.setBounds(header.removeFromLeft(focusEq ? 68 : 56));
        eqEnableButton.setBounds(header.removeFromRight(showEqExpandedControls ? 92 : 84));
        header.removeFromRight(8);
        eqDescriptorLabel.setBounds(header);

        auto eq = eqCardLayout.contentBounds;
        if (showEqExpandedControls)
        {
            auto controlRow = eq.removeFromTop(focusEq ? 24 : 22);
            auto orderZone = controlRow.removeFromLeft(focusEq ? 124 : 116);
            eqOrderLabel.setBounds(orderZone.removeFromLeft(48));
            eqOrderCombo.setBounds(orderZone);
            controlRow.removeFromLeft(6);
            auto bandZone = controlRow.removeFromLeft(focusEq ? 156 : 144);
            eqBandLabel.setBounds(bandZone.removeFromLeft(42));
            eqBandCombo.setBounds(bandZone);
            controlRow.removeFromLeft(6);
            auto shapeZone = controlRow.removeFromLeft(focusEq ? 156 : 144);
            eqShapeLabel.setBounds(shapeZone.removeFromLeft(48));
            eqShapeCombo.setBounds(shapeZone);

            eq.removeFromTop(6);
            auto focusRow = eq.removeFromTop(16);
            eqBandMeaningLabel.setBounds(focusRow.removeFromLeft(126));
            eqBandRangeLabel.setBounds(focusRow);

            eq.removeFromTop(6);
            eqDisplayBounds = eq.removeFromTop(focusEq ? 150 : 92);
            eq.removeFromTop(8);
            auto knobs = eq.removeFromTop(juce::jmax(focusEq ? 180 : 110, eq.getHeight()));
            auto slotA = knobs.removeFromLeft(knobs.getWidth() / 3).reduced(4, 0);
            auto slotB = knobs.removeFromLeft(knobs.getWidth() / 2).reduced(4, 0);
            auto slotC = knobs.reduced(4, 0);
            layoutKnob(slotA, eqFreqLabel, eqFreqValueLabel, eqFreqSlider);
            layoutKnob(slotB, eqGainLabel, eqGainValueLabel, eqGainSlider);
            layoutKnob(slotC, eqQLabel, eqQValueLabel, eqQSlider);
        }
        else
        {
            eqDisplayBounds = {};
        }
    }

    if (showStereo)
    {
        auto header = stereoCardLayout.headerBounds;
        const bool focusStereo = layoutMode == LayoutMode::FocusStereo;
        stereoSectionLabel.setBounds(header.removeFromLeft(118));
        if (showStereoExpandedControls)
        {
            stereoMonoSafeButton.setBounds(header.removeFromRight(104));
            header.removeFromRight(8);
        }
        stereoDescriptorLabel.setBounds(header);

        auto stereo = stereoCardLayout.contentBounds;
        if (showStereoExpandedControls)
        {
            stereoDisplayBounds = stereo.removeFromTop(focusStereo ? 72 : 40);
            stereo.removeFromTop(6);
            auto ioRow = stereo.removeFromTop(14);
            stereoInputLabel.setBounds(ioRow.removeFromLeft(88));
            stereoOutputLabel.setBounds(ioRow);
            stereo.removeFromTop(8);
            auto knobRowA = stereo.removeFromTop(focusStereo ? 92 : 68);
            auto leftA = knobRowA.removeFromLeft(knobRowA.getWidth() / 2).reduced(4, 0);
            auto rightA = knobRowA.reduced(4, 0);
            layoutKnob(leftA, panLabel, panValueLabel, panSlider);
            layoutKnob(rightA, widthLabel, widthValueLabel, widthSlider);
            stereo.removeFromTop(6);
            auto knobRowB = stereo.removeFromTop(focusStereo ? 92 : juce::jmax(68, stereo.getHeight()));
            auto leftB = knobRowB.removeFromLeft(knobRowB.getWidth() / 2).reduced(4, 0);
            auto rightB = knobRowB.reduced(4, 0);
            layoutKnob(leftB, stereoFocusLabel, stereoFocusValueLabel, stereoFocusSlider);
            layoutKnob(rightB, stereoEdgeLabel, stereoEdgeValueLabel, stereoEdgeSlider);

            if (showStereoAdvancedControls)
            {
                stereo.removeFromTop(8);
                auto advancedRow = stereo.removeFromTop(juce::jmax(92, stereo.getHeight()));
                auto protectArea = advancedRow.removeFromLeft(advancedRow.getWidth() / 2).reduced(4, 0);
                auto airArea = advancedRow.reduced(4, 0);
                layoutKnob(protectArea,
                           stereoLowCenterProtectLabel,
                           stereoLowCenterProtectValueLabel,
                           stereoLowCenterProtectSlider);
                layoutKnob(airArea,
                           stereoAirSpreadLabel,
                           stereoAirSpreadValueLabel,
                           stereoAirSpreadSlider);
            }
        }
        else
        {
            stereoDisplayBounds = {};
        }
    }

    if (showComp)
    {
        auto header = compCardLayout.headerBounds;
        const bool focusComp = layoutMode == LayoutMode::FocusComp;
        const bool overviewExpandedComp = isOverviewCardExpanded(OverviewCard::Comp);
        compSectionLabel.setBounds(header.removeFromLeft(118));
        compPowerButton.setBounds(header.removeFromRight(84));
        if (showCompDescriptor)
        {
            header.removeFromRight(8);
            compDescriptorLabel.setBounds(header);
        }
        else
        {
            compDescriptorLabel.setBounds({});
        }

        auto comp = compCardLayout.contentBounds;
        if (showCompExpandedControls)
        {
            if (overviewExpandedComp)
                comp.removeFromTop(12);

            auto orderRow = comp.removeFromTop(overviewExpandedComp ? 24 : 22);
            compOrderLabel.setBounds(orderRow.removeFromLeft(overviewExpandedComp ? 52 : 48));
            compOrderCombo.setBounds(orderRow.removeFromLeft(overviewExpandedComp ? 136 : 126));
            comp.removeFromTop(overviewExpandedComp ? 10 : 8);
            auto rowA = comp.removeFromTop(focusComp ? 128 : 90);
            auto ratioArea = rowA.removeFromLeft(rowA.getWidth() / 3).reduced(4, 0);
            auto thresholdArea = rowA.removeFromLeft(rowA.getWidth() / 2).reduced(4, 0);
            auto mixArea = rowA.reduced(4, 0);
            layoutKnob(ratioArea, compRatioLabel, compRatioValueLabel, compRatioSlider);
            layoutKnob(thresholdArea, compThresholdLabel, compThresholdValueLabel, compThresholdSlider);
            layoutKnob(mixArea, compMixLabel, compMixValueLabel, compMixSlider);
            comp.removeFromTop(6);
            auto rowB = comp.removeFromTop(juce::jmax(focusComp ? 128 : 90, comp.getHeight()));
            auto attackArea = rowB.removeFromLeft(rowB.getWidth() / 3).reduced(4, 0);
            auto releaseArea = rowB.removeFromLeft(rowB.getWidth() / 2).reduced(4, 0);
            auto satArea = rowB.reduced(4, 0);
            layoutKnob(attackArea, compAttackLabel, compAttackValueLabel, compAttackSlider);
            layoutKnob(releaseArea, compReleaseLabel, compReleaseValueLabel, compReleaseSlider);
            layoutKnob(satArea, compSaturationLabel, compSaturationValueLabel, compSaturationSlider);
        }
    }

    if (showReverb)
    {
        auto header = reverbCardLayout.headerBounds;
        const bool focusReverb = layoutMode == LayoutMode::FocusReverb;
        reverbSectionLabel.setBounds(header.removeFromLeft(92));
        reverbDescriptorLabel.setBounds(header);

        if (showReverbExpandedControls)
        {
            auto reverb = reverbCardLayout.contentBounds;
            auto rowA = reverb.removeFromTop(focusReverb ? 128 : 88);
            auto sizeArea = rowA.removeFromLeft(rowA.getWidth() / 2).reduced(4, 0);
            auto mixArea = rowA.reduced(4, 0);
            layoutKnob(sizeArea, reverbSizeLabel, reverbSizeValueLabel, reverbSizeSlider);
            layoutKnob(mixArea, reverbMixLabel, reverbMixValueLabel, reverbMixSlider);
            reverb.removeFromTop(6);
            auto rowB = reverb.removeFromTop(juce::jmax(focusReverb ? 128 : 88, reverb.getHeight()));
            auto predelayArea = rowB.removeFromLeft(rowB.getWidth() / 2).reduced(4, 0);
            auto tailArea = rowB.reduced(4, 0);
            layoutKnob(predelayArea, reverbPredelayLabel, reverbPredelayValueLabel, reverbPredelaySlider);
            layoutKnob(tailArea, reverbTailLabel, reverbTailValueLabel, reverbTailSlider);
        }
    }

    if (showMonsta)
    {
        auto header = monstaCardLayout.headerBounds;
        const bool focusMonsta = layoutMode == LayoutMode::FocusMonsta;
        monstaSectionLabel.setBounds(header.removeFromLeft(104));
        if (showMonstaExpandedControls)
        {
            monstaChaosButton.setBounds(header.removeFromRight(86));
            header.removeFromRight(8);
        }
        monstaDescriptorLabel.setBounds(header);

        if (showMonstaExpandedControls)
        {
            auto monsta = monstaCardLayout.contentBounds;
            auto row = monsta.removeFromTop(juce::jmax(focusMonsta ? 128 : 94, monsta.getHeight()));
            auto dryArea = row.removeFromLeft(row.getWidth() / 2).reduced(4, 0);
            auto wetArea = row.reduced(4, 0);
            layoutKnob(dryArea, monstaDryLabel, monstaDryValueLabel, monstaDrySlider);
            layoutKnob(wetArea, monstaWetLabel, monstaWetValueLabel, monstaWetSlider);
        }
    }

    if (showTransient)
    {
        auto header = transientCardLayout.headerBounds;
        const bool focusTransient = layoutMode == LayoutMode::FocusTransient;
        transientSectionLabel.setBounds(header.removeFromLeft(110));
        if (showTransientExpandedControls)
        {
            limitButton.setBounds(header.removeFromRight(82));
            header.removeFromRight(8);
            smoothButton.setBounds(header.removeFromRight(82));
            header.removeFromRight(8);
        }
        transientDescriptorLabel.setBounds(header);

        auto trans = transientCardLayout.contentBounds;
        if (showTransientExpandedControls)
        {
            transientDisplayBounds = trans.removeFromTop(focusTransient ? 70 : 44);
            trans.removeFromTop(8);
            auto row = trans.removeFromTop(juce::jmax(focusTransient ? 148 : 92, trans.getHeight()));
            auto attackArea = row.removeFromLeft(row.getWidth() / 3).reduced(4, 0);
            auto sustainArea = row.removeFromLeft(row.getWidth() / 2).reduced(4, 0);
            auto gainArea = row.reduced(4, 0);
            layoutKnob(attackArea, transientAttackLabel, transientAttackValueLabel, transientAttackSlider);
            layoutKnob(sustainArea, transientSustainLabel, transientSustainValueLabel, transientSustainSlider);
            layoutKnob(gainArea, transientGainLabel, transientGainValueLabel, transientGainSlider);
        }
        else
        {
            transientDisplayBounds = {};
        }
    }

    {
        auto strip = moduleStripBounds.reduced(12, 8);
        const int gap = 8;
        const int buttonWidth = (strip.getWidth() - gap * 5) / 6;
        compStripButton.setBounds(strip.removeFromLeft(buttonWidth));
        strip.removeFromLeft(gap);
        reverbStripButton.setBounds(strip.removeFromLeft(buttonWidth));
        strip.removeFromLeft(gap);
        monstaStripButton.setBounds(strip.removeFromLeft(buttonWidth));
        strip.removeFromLeft(gap);
        transientStripButton.setBounds(strip.removeFromLeft(buttonWidth));
        strip.removeFromLeft(gap);
        eqStripButton.setBounds(strip.removeFromLeft(buttonWidth));
        strip.removeFromLeft(gap);
        stereoStripButton.setBounds(strip);
    }
}

void SoundModuleComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (contentHeight <= contentViewportBounds.getHeight())
        return;

    const int delta = juce::roundToInt(-(wheel.deltaY * (wheel.isSmooth ? 90.0f : 160.0f)));
    if (delta != 0)
        setScrollOffset(scrollOffsetY + delta);
}

void SoundModuleComponent::mouseMove(const juce::MouseEvent& event)
{
    if (draggedEqBandIndex >= 0)
        return;

    const auto localEvent = event.getEventRelativeTo(this);
    const auto canvasPosition = toContentCanvasSpace(localEvent.position);
    const bool interactiveChild = isOverviewInteractiveChildHit(event);
    if (layoutMode == LayoutMode::Overview && interactiveChild)
    {
        setHoveredEqBandIndex(-1);
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    if (layoutMode == LayoutMode::Overview)
    {
        if (const auto headerCard = findOverviewHeaderAt(localEvent.getPosition()); headerCard != OverviewCard::None)
        {
            setHoveredEqBandIndex(-1);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            return;
        }

        if (const auto card = findOverviewCardAt(localEvent.getPosition()); card != OverviewCard::None)
        {
            const bool keepEqInteractive = card == OverviewCard::Eq
                                           && isOverviewCardExpanded(OverviewCard::Eq)
                                           && eqDisplayBounds.contains(canvasPosition.toInt());
            if (!keepEqInteractive)
            {
                setHoveredEqBandIndex(-1);
                setMouseCursor(isOverviewCardExpanded(card)
                                   ? juce::MouseCursor::NormalCursor
                                   : juce::MouseCursor::PointingHandCursor);
                return;
            }
        }
    }

    setMouseCursor(juce::MouseCursor::NormalCursor);
    if (eqDisplayBounds.contains(canvasPosition.toInt()))
        setHoveredEqBandIndex(findEqDisplayBandAt(canvasPosition));
    else
        setHoveredEqBandIndex(-1);
}

void SoundModuleComponent::mouseExit(const juce::MouseEvent&)
{
    if (draggedEqBandIndex >= 0)
        return;

    setHoveredEqBandIndex(-1);
}

void SoundModuleComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto localEvent = event.getEventRelativeTo(this);
    const auto canvasPosition = toContentCanvasSpace(localEvent.position);
    const bool interactiveChild = isOverviewInteractiveChildHit(event);

    if (layoutMode == LayoutMode::Overview)
    {
        if (!interactiveChild)
        {
            if (const auto card = findOverviewHeaderAt(localEvent.getPosition()); card != OverviewCard::None)
            {
                toggleOverviewCard(card);
                return;
            }

            if (const auto card = findOverviewCardAt(localEvent.getPosition()); card != OverviewCard::None)
            {
                const bool keepEqInteractive = card == OverviewCard::Eq
                                               && isOverviewCardExpanded(OverviewCard::Eq)
                                               && eqDisplayBounds.contains(canvasPosition.toInt());

                if (!keepEqInteractive)
                {
                    toggleOverviewCard(card);
                    return;
                }
            }
        }
    }

    if (!targetAvailable)
        return;

    const int bandIndex = findEqDisplayBandAt(canvasPosition);
    if (bandIndex < 0)
        return;

    draggedEqBandIndex = bandIndex;
    eqBandDragGestureActive = true;
    if (onSoundLayerGestureStarted)
        onSoundLayerGestureStarted();

    setHoveredEqBandIndex(bandIndex);
    eqBandCombo.setSelectedId(bandIndex + 1, juce::sendNotificationSync);
}

void SoundModuleComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedEqBandIndex < 0 || !targetAvailable)
        return;

    updateDraggedEqBandFromPosition(toContentCanvasSpace(event.getEventRelativeTo(this).position));
}

void SoundModuleComponent::mouseUp(const juce::MouseEvent&)
{
    if (eqBandDragGestureActive && onSoundLayerGestureEnded)
        onSoundLayerGestureEnded();

    eqBandDragGestureActive = false;
    draggedEqBandIndex = -1;
}

void SoundModuleComponent::setState(const SoundModuleViewState& state)
{
    const auto preservedExpandedOverviewCard = expandedOverviewCard;
    targetDescriptors.clear();
    targetCombo.clear(juce::dontSendNotification);

    int selectedId = 0;
    int itemId = 1;
    bool targetMatchedInCombo = false;
    juce::String matchedTargetName;
    for (const auto& option : state.targetOptions)
    {
        targetDescriptors.push_back(option.descriptor);
        targetCombo.addItem(option.displayName, itemId);
        if (state.selectedTarget == option.descriptor)
        {
            selectedId = itemId;
            targetMatchedInCombo = true;
            matchedTargetName = option.displayName;
        }
        ++itemId;
    }

    currentTarget = state.selectedTarget;
    currentSoundState = state.soundState;
    if (layoutMode == LayoutMode::Overview)
        expandedOverviewCard = preservedExpandedOverviewCard;
    draggedEqBandIndex = -1;
    eqBandDragGestureActive = false;
    targetAvailable = comboContainsDescriptor(targetDescriptors, state.selectedTarget);
    if (targetAvailable && selectedId > 0)
    {
        targetCombo.setSelectedId(selectedId, juce::dontSendNotification);
    }
    else
    {
        targetCombo.setSelectedId(0, juce::dontSendNotification);
        targetCombo.setText("Unavailable: " + displayNameForDescriptor(state.selectedTarget), juce::dontSendNotification);
    }

    bypassButton.setToggleState(false, juce::dontSendNotification);
    eqOrderCombo.setSelectedId(1, juce::dontSendNotification);
    compOrderCombo.setSelectedId(3, juce::dontSendNotification);

    {
        const juce::ScopedValueSetter<bool> scope(isSyncingUi, true);
        eqBandCombo.setSelectedId(currentSoundState.eq.selectedBand + 1, juce::dontSendNotification);
    }
    syncVisualStateFromCurrentSoundState();
    updateStripSelection();
    updateTargetPresentation(state.selectedTarget, targetMatchedInCombo, matchedTargetName);
    setControlsEnabled(targetAvailable);
    updateValueLabels();
    updateChainSummary();
}

void SoundModuleComponent::setEqDisplayAnalyzerState(const EqDisplayAnalyzerState& state)
{
    eqDisplayAnalyzerState = state;
    repaint(toDisplaySpace(eqDisplayBounds));
}

void SoundModuleComponent::setupDial(juce::Slider& slider,
                                     double min,
                                     double max,
                                     double step,
                                     juce::Colour accent,
                                     double skewMidPoint)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange(min, max, step);
    if (skewMidPoint > 0.0)
        slider.setSkewFactorFromMidPoint(skewMidPoint);
    slider.setColour(juce::Slider::rotarySliderFillColourId, accent.withAlpha(0.95f));
    slider.setColour(juce::Slider::thumbColourId, amberBright());
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(72, 58, 44));
    slider.setColour(juce::Slider::trackColourId, accent.withAlpha(0.55f));
}

void SoundModuleComponent::setupCombo(juce::ComboBox& combo)
{
    combo.setJustificationType(juce::Justification::centredLeft);
    combo.setColour(juce::ComboBox::backgroundColourId, panelInset());
    combo.setColour(juce::ComboBox::outlineColourId, copper().withAlpha(0.55f));
    combo.setColour(juce::ComboBox::textColourId, textMain());
    combo.setColour(juce::ComboBox::arrowColourId, amberBright());
}

void SoundModuleComponent::setupActionButton(juce::TextButton& button, bool prominent)
{
    button.getProperties().set("soundRole", "action");
    button.setColour(juce::TextButton::buttonColourId, prominent ? copper().withAlpha(0.90f) : panelInset().brighter(0.14f));
    button.setColour(juce::TextButton::buttonOnColourId, prominent ? amberBright() : amber().withAlpha(0.92f));
    button.setColour(juce::TextButton::textColourOffId, prominent ? panelInset().brighter(0.95f) : textMain());
    button.setColour(juce::TextButton::textColourOnId, panelInset().brighter(0.95f));
}

void SoundModuleComponent::setupStripButton(juce::TextButton& button, bool selected)
{
    button.getProperties().set("soundRole", "strip");
    button.getProperties().set("selected", selected);
    button.setColour(juce::TextButton::buttonColourId, panelInset().brighter(0.12f));
    button.setColour(juce::TextButton::buttonOnColourId, amber().withAlpha(0.92f));
    button.setColour(juce::TextButton::textColourOffId, textMain());
    button.setColour(juce::TextButton::textColourOnId, panelInset().brighter(0.95f));
}

void SoundModuleComponent::toggleLayoutMode(LayoutMode requestedMode)
{
    layoutMode = layoutMode == requestedMode ? LayoutMode::Overview : requestedMode;
    if (layoutMode == LayoutMode::Overview)
        expandedOverviewCard = OverviewCard::None;
    setScrollOffset(0);
    updateStripSelection();
    resized();
    repaint();
}

bool SoundModuleComponent::isOverviewCardExpanded(OverviewCard card) const
{
    return layoutMode == LayoutMode::Overview && expandedOverviewCard == card;
}

void SoundModuleComponent::toggleOverviewCard(OverviewCard card)
{
    if (layoutMode != LayoutMode::Overview || card == OverviewCard::None)
        return;

    expandedOverviewCard = expandedOverviewCard == card ? OverviewCard::None : card;
    setScrollOffset(0);
    resized();
    repaint();
}

bool SoundModuleComponent::isOverviewInteractiveChildHit(const juce::MouseEvent& event) const
{
    auto* owner = const_cast<SoundModuleComponent*>(this);
    auto* current = owner->getComponentAt(event.getEventRelativeTo(owner).getPosition());
    while (current != nullptr && current != this)
    {
        if (auto* dial = dynamic_cast<RotaryDial*>(current); dial != nullptr)
        {
            const auto localPosition = event.getEventRelativeTo(dial).position;
            return dial->isPointOverActiveZone(localPosition);
        }

        if (dynamic_cast<juce::Button*>(current) != nullptr
            || dynamic_cast<juce::ComboBox*>(current) != nullptr
            || dynamic_cast<juce::ScrollBar*>(current) != nullptr)
        {
            return true;
        }

        if (dynamic_cast<juce::Slider*>(current) != nullptr)
            return true;

        current = current->getParentComponent();
    }

    return false;
}

SoundModuleComponent::OverviewCard SoundModuleComponent::findOverviewHeaderAt(juce::Point<int> position) const
{
    if (layoutMode != LayoutMode::Overview)
        return OverviewCard::None;

    if (!contentViewportBounds.contains(position))
        return OverviewCard::None;

    const auto canvasPosition = toContentCanvasSpace(position.toFloat()).toInt();

    const auto hit = [&](const ModuleCardLayout& layout, OverviewCard card)
    {
        return layout.headerBounds.contains(canvasPosition) ? card : OverviewCard::None;
    };

    if (auto card = hit(eqCardLayout, OverviewCard::Eq); card != OverviewCard::None)
        return card;
    if (auto card = hit(compCardLayout, OverviewCard::Comp); card != OverviewCard::None)
        return card;
    if (auto card = hit(stereoCardLayout, OverviewCard::Stereo); card != OverviewCard::None)
        return card;
    if (auto card = hit(reverbCardLayout, OverviewCard::Reverb); card != OverviewCard::None)
        return card;
    if (auto card = hit(monstaCardLayout, OverviewCard::MonstaFx); card != OverviewCard::None)
        return card;
    if (auto card = hit(transientCardLayout, OverviewCard::Transient); card != OverviewCard::None)
        return card;

    return OverviewCard::None;
}

SoundModuleComponent::OverviewCard SoundModuleComponent::findOverviewCardAt(juce::Point<int> position) const
{
    if (layoutMode != LayoutMode::Overview)
        return OverviewCard::None;

    if (!contentViewportBounds.contains(position))
        return OverviewCard::None;

    const auto canvasPosition = toContentCanvasSpace(position.toFloat()).toInt();

    const auto hit = [&](const ModuleCardLayout& layout, OverviewCard card)
    {
        return layout.bounds.contains(canvasPosition) ? card : OverviewCard::None;
    };

    if (auto card = hit(eqCardLayout, OverviewCard::Eq); card != OverviewCard::None)
        return card;
    if (auto card = hit(compCardLayout, OverviewCard::Comp); card != OverviewCard::None)
        return card;
    if (auto card = hit(stereoCardLayout, OverviewCard::Stereo); card != OverviewCard::None)
        return card;
    if (auto card = hit(reverbCardLayout, OverviewCard::Reverb); card != OverviewCard::None)
        return card;
    if (auto card = hit(monstaCardLayout, OverviewCard::MonstaFx); card != OverviewCard::None)
        return card;
    if (auto card = hit(transientCardLayout, OverviewCard::Transient); card != OverviewCard::None)
        return card;

    return OverviewCard::None;
}

juce::Point<float> SoundModuleComponent::toContentCanvasSpace(juce::Point<float> position) const
{
    return position.translated(-static_cast<float>(contentViewportBounds.getX()),
                               static_cast<float>(scrollOffsetY - contentViewportBounds.getY()));
}

juce::Rectangle<int> SoundModuleComponent::toDisplaySpace(const juce::Rectangle<int>& canvasBounds) const
{
    if (canvasBounds.isEmpty())
        return {};

    return canvasBounds.translated(contentViewportBounds.getX(), contentViewportBounds.getY() - scrollOffsetY);
}

void SoundModuleComponent::updateStripSelection()
{
    compStripButton.getProperties().set("selected", layoutMode == LayoutMode::FocusComp);
    reverbStripButton.getProperties().set("selected", layoutMode == LayoutMode::FocusReverb);
    monstaStripButton.getProperties().set("selected", layoutMode == LayoutMode::FocusMonsta);
    transientStripButton.getProperties().set("selected", layoutMode == LayoutMode::FocusTransient);
    eqStripButton.getProperties().set("selected", layoutMode == LayoutMode::FocusEq);
    stereoStripButton.getProperties().set("selected", layoutMode == LayoutMode::FocusStereo);

    compStripButton.repaint();
    reverbStripButton.repaint();
    monstaStripButton.repaint();
    transientStripButton.repaint();
    eqStripButton.repaint();
    stereoStripButton.repaint();
}

void SoundModuleComponent::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (scrollBarThatHasMoved != &verticalScrollBar)
        return;

    setScrollOffset(juce::roundToInt(newRangeStart));
}

void SoundModuleComponent::setScrollOffset(int newOffset)
{
    const int maxScrollOffset = juce::jmax(0, contentHeight - contentViewportBounds.getHeight());
    const int clamped = juce::jlimit(0, maxScrollOffset, newOffset);
    if (clamped == scrollOffsetY)
        return;

    scrollOffsetY = clamped;
    resized();
    repaint();
}

void SoundModuleComponent::setControlsEnabled(bool shouldEnable)
{
    targetCombo.setEnabled(true);
    bypassButton.setEnabled(shouldEnable);
    resetButton.setEnabled(shouldEnable);
    eqEnableButton.setEnabled(shouldEnable);
    eqOrderCombo.setEnabled(shouldEnable);
    eqBandCombo.setEnabled(shouldEnable);
    eqShapeCombo.setEnabled(shouldEnable);
    eqFreqSlider.setEnabled(shouldEnable);
    eqGainSlider.setEnabled(shouldEnable);
    eqQSlider.setEnabled(shouldEnable);
    panSlider.setEnabled(shouldEnable);
    widthSlider.setEnabled(shouldEnable);
    stereoFocusSlider.setEnabled(shouldEnable);
    stereoEdgeSlider.setEnabled(shouldEnable);
    stereoLowCenterProtectSlider.setEnabled(shouldEnable);
    stereoAirSpreadSlider.setEnabled(shouldEnable);
    stereoMonoSafeButton.setEnabled(shouldEnable);
    compOrderCombo.setEnabled(shouldEnable);
    compPowerButton.setEnabled(shouldEnable);
    compRatioSlider.setEnabled(shouldEnable);
    compThresholdSlider.setEnabled(shouldEnable);
    compMixSlider.setEnabled(shouldEnable);
    compAttackSlider.setEnabled(shouldEnable);
    compReleaseSlider.setEnabled(shouldEnable);
    compSaturationSlider.setEnabled(shouldEnable);
    reverbSizeSlider.setEnabled(shouldEnable);
    reverbMixSlider.setEnabled(shouldEnable);
    reverbPredelaySlider.setEnabled(shouldEnable);
    reverbTailSlider.setEnabled(shouldEnable);
    monstaDrySlider.setEnabled(shouldEnable);
    monstaWetSlider.setEnabled(shouldEnable);
    monstaChaosButton.setEnabled(shouldEnable);
    transientAttackSlider.setEnabled(shouldEnable);
    transientSustainSlider.setEnabled(shouldEnable);
    transientGainSlider.setEnabled(shouldEnable);
    smoothButton.setEnabled(shouldEnable);
    limitButton.setEnabled(shouldEnable);
}

void SoundModuleComponent::updateTargetPresentation(const SoundTargetDescriptor& selectedTarget,
                                                    bool targetMatchedInCombo,
                                                    const juce::String& matchedTargetName)
{
    if (!targetAvailable)
    {
        targetSummaryLabel.setText("Editing: Target unavailable", juce::dontSendNotification);
        targetModeLabel.setText("INVALID", juce::dontSendNotification);
        targetModeLabel.setColour(juce::Label::backgroundColourId, danger());
        targetModeLabel.setColour(juce::Label::textColourId, panelInset().brighter(0.95f));
        targetStatusLabel.setText("Target is no longer valid. Choose Global or a visible lane before editing.",
                                  juce::dontSendNotification);
        targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(255, 176, 166));
        return;
    }

    if (selectedTarget.isGlobal())
    {
        targetSummaryLabel.setText("Editing: Global sound layer", juce::dontSendNotification);
        targetModeLabel.setText("GLOBAL", juce::dontSendNotification);
        targetModeLabel.setColour(juce::Label::backgroundColourId, amberBright());
        targetModeLabel.setColour(juce::Label::textColourId, panelInset().brighter(0.95f));
        targetStatusLabel.setText("Routing: shared sound shaping for the full kit.", juce::dontSendNotification);
        targetStatusLabel.setColour(juce::Label::textColourId, steel());
        return;
    }

    const auto targetName = targetMatchedInCombo ? matchedTargetName : displayNameForDescriptor(selectedTarget);
    if (selectedTarget.kind == SoundTargetDescriptorKind::BackedRuntimeLane)
    {
        targetSummaryLabel.setText("Editing: " + targetName + " lane", juce::dontSendNotification);
        targetModeLabel.setText("LANE", juce::dontSendNotification);
        targetModeLabel.setColour(juce::Label::backgroundColourId, success());
        targetModeLabel.setColour(juce::Label::textColourId, panelInset().brighter(0.95f));
        targetStatusLabel.setText("Routing: edits the current backed runtime lane only.", juce::dontSendNotification);
        targetStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(174, 214, 160));
        return;
    }

    targetSummaryLabel.setText("Editing: " + targetName + " track", juce::dontSendNotification);
    targetModeLabel.setText("TRACK", juce::dontSendNotification);
    targetModeLabel.setColour(juce::Label::backgroundColourId, copper());
    targetModeLabel.setColour(juce::Label::textColourId, panelInset().brighter(0.95f));
    targetStatusLabel.setText("Routing: edits the selected track target.", juce::dontSendNotification);
    targetStatusLabel.setColour(juce::Label::textColourId, amberBright());
}

void SoundModuleComponent::updateChainSummary()
{
    if (bypassButton.getToggleState())
    {
        chainSummaryLabel.setText("FLOW: preview bypass active", juce::dontSendNotification);
        return;
    }

    juce::String chain = "FLOW: ";
    chain << eqOrderCombo.getText() << " EQ";
    chain << "  |  STEREO " << juce::String(juce::roundToInt(widthSlider.getValue() * 100.0)) << "%"
          << " / FOC " << juce::String(juce::roundToInt(stereoFocusSlider.getValue())) << "%";
    chain << "  |  " << (compPowerButton.getToggleState() ? compOrderCombo.getText() + " COMP" : juce::String("COMP OFF"));
    chain << "  |  MONSTA " << juce::String(monstaFxFlavorTitle(resolveMonstaFxFlavor(currentSoundState.monstaFx)))
          << " " << juce::String(juce::roundToInt(monstaWetSlider.getValue())) << "%";
    chain << "  |  REV " << juce::String(juce::roundToInt(reverbMixSlider.getValue())) << "%";
    chain << "  |  TRANS " << juce::String(juce::roundToInt(transientAttackSlider.getValue())) << "%";
    chainSummaryLabel.setText(chain, juce::dontSendNotification);
}

void SoundModuleComponent::updateValueLabels()
{
    eqFreqValueLabel.setText(formatFreqValue(eqFreqSlider.getValue()), juce::dontSendNotification);
    eqGainValueLabel.setText(formatDbValue(static_cast<float>(eqGainSlider.getValue())), juce::dontSendNotification);
    eqQValueLabel.setText(juce::String(eqQSlider.getValue(), 2), juce::dontSendNotification);

    panValueLabel.setText(formatPanValue(static_cast<float>(panSlider.getValue())), juce::dontSendNotification);
    widthValueLabel.setText(formatWidthValue(static_cast<float>(widthSlider.getValue())), juce::dontSendNotification);
    stereoFocusValueLabel.setText(formatPercentValue(stereoFocusSlider.getValue()), juce::dontSendNotification);
    stereoEdgeValueLabel.setText(formatPercentValue(stereoEdgeSlider.getValue()), juce::dontSendNotification);
    stereoLowCenterProtectValueLabel.setText(formatPercentValue(stereoLowCenterProtectSlider.getValue()), juce::dontSendNotification);
    stereoAirSpreadValueLabel.setText(formatPercentValue(stereoAirSpreadSlider.getValue()), juce::dontSendNotification);

    compRatioValueLabel.setText(formatRatioValue(compRatioSlider.getValue()), juce::dontSendNotification);
    compThresholdValueLabel.setText(juce::String(compThresholdSlider.getValue(), 1) + " dB", juce::dontSendNotification);
    compMixValueLabel.setText(formatPercentValue(compMixSlider.getValue()), juce::dontSendNotification);
    compAttackValueLabel.setText(formatMilliseconds(compAttackSlider.getValue()), juce::dontSendNotification);
    compReleaseValueLabel.setText(formatMilliseconds(compReleaseSlider.getValue()), juce::dontSendNotification);
    compSaturationValueLabel.setText(formatPercentValue(compSaturationSlider.getValue()), juce::dontSendNotification);

    reverbSizeValueLabel.setText(formatPercentValue(reverbSizeSlider.getValue()), juce::dontSendNotification);
    reverbMixValueLabel.setText(formatPercentValue(reverbMixSlider.getValue()), juce::dontSendNotification);
    reverbPredelayValueLabel.setText(formatMilliseconds(reverbPredelaySlider.getValue()), juce::dontSendNotification);
    reverbTailValueLabel.setText(formatPercentValue(reverbTailSlider.getValue()), juce::dontSendNotification);

    monstaDryValueLabel.setText(formatPercentValue(monstaDrySlider.getValue()), juce::dontSendNotification);
    monstaWetValueLabel.setText(formatPercentValue(monstaWetSlider.getValue()), juce::dontSendNotification);

    transientAttackValueLabel.setText(formatPercentValue(transientAttackSlider.getValue()), juce::dontSendNotification);
    transientSustainValueLabel.setText(formatPercentValue(transientSustainSlider.getValue()), juce::dontSendNotification);
    transientGainValueLabel.setText(formatDbValue(static_cast<float>(transientGainSlider.getValue())), juce::dontSendNotification);

    updateChainSummary();
    repaint(toDisplaySpace(eqBounds));
    repaint(toDisplaySpace(stereoBounds));
    repaint(toDisplaySpace(compBounds));
    repaint(toDisplaySpace(reverbBounds));
    repaint(toDisplaySpace(monstaBounds));
    repaint(toDisplaySpace(transientBounds));
}

int SoundModuleComponent::currentEqBandIndex() const
{
    return clampEqBandIndex(eqBandCombo.getSelectedId() - 1);
}

int SoundModuleComponent::findEqDisplayBandAt(juce::Point<float> position) const
{
    if (layoutMode == LayoutMode::Overview && !isOverviewCardExpanded(OverviewCard::Eq))
        return -1;

    if (eqDisplayBounds.isEmpty() || !eqDisplayBounds.toFloat().contains(position))
        return -1;

    const auto responseArea = getEqDisplayResponseArea(eqDisplayBounds);
    float bestDistanceSquared = std::numeric_limits<float>::max();
    int bestBandIndex = -1;
    for (int bandIndex = 0; bandIndex < kEqBandCount; ++bandIndex)
    {
        const auto markerPosition = getEqBandMarkerPosition(responseArea, currentSoundState.eq, bandIndex);
        const auto dx = markerPosition.x - position.x;
        const auto dy = markerPosition.y - position.y;
        const float distanceSquared = dx * dx + dy * dy;
        if (distanceSquared < bestDistanceSquared)
        {
            bestDistanceSquared = distanceSquared;
            bestBandIndex = bandIndex;
        }
    }

    constexpr float hitRadius = 16.0f;
    return bestDistanceSquared <= hitRadius * hitRadius ? bestBandIndex : -1;
}

void SoundModuleComponent::updateDraggedEqBandFromPosition(juce::Point<float> position)
{
    if (draggedEqBandIndex < 0 || eqDisplayBounds.isEmpty())
        return;

    const auto responseArea = getEqDisplayResponseArea(eqDisplayBounds);
    if (responseArea.isEmpty())
        return;

    const float clampedX = juce::jlimit(responseArea.getX(), responseArea.getRight(), position.x);
    const float clampedY = juce::jlimit(responseArea.getY(), responseArea.getBottom(), position.y);
    const float normalizedX = responseArea.getWidth() > 0.0f
        ? (clampedX - responseArea.getX()) / responseArea.getWidth()
        : 0.0f;

    currentSoundState.eq.selectedBand = draggedEqBandIndex;
    {
        const juce::ScopedValueSetter<bool> scope(isSyncingUi, true);
        eqBandCombo.setSelectedId(draggedEqBandIndex + 1, juce::dontSendNotification);
    }
    updateEqBandUiFromSelection();

    const auto currentShape = static_cast<EqBandShape>(juce::jlimit(0, 2, eqShapeCombo.getSelectedId() - 1));
    const double frequencyHz = eqDisplayFrequencyFromNormalized(normalizedX);
    const double mappedSecondaryValue = juce::jmap(static_cast<double>(clampedY),
                                                   static_cast<double>(responseArea.getBottom()),
                                                   static_cast<double>(responseArea.getY()),
                                                   currentShape == EqBandShape::Bell ? eqGainSlider.getMinimum() : eqQSlider.getMinimum(),
                                                   currentShape == EqBandShape::Bell ? eqGainSlider.getMaximum() : eqQSlider.getMaximum());

    {
        const juce::ScopedValueSetter<bool> scope(isSyncingUi, true);
        eqEnableButton.setToggleState(true, juce::dontSendNotification);
        eqFreqSlider.setValue(juce::jlimit(eqFreqSlider.getMinimum(), eqFreqSlider.getMaximum(), frequencyHz), juce::dontSendNotification);
        if (currentShape == EqBandShape::Bell)
            eqGainSlider.setValue(juce::jlimit(eqGainSlider.getMinimum(), eqGainSlider.getMaximum(), mappedSecondaryValue), juce::dontSendNotification);
        else
            eqQSlider.setValue(juce::jlimit(eqQSlider.getMinimum(), eqQSlider.getMaximum(), mappedSecondaryValue), juce::dontSendNotification);
    }

    commitEqBandControlsToState();
    setHoveredEqBandIndex(draggedEqBandIndex);
    emitSoundLayerChange();
}

void SoundModuleComponent::setHoveredEqBandIndex(int newHoveredBandIndex)
{
    const int normalizedIndex = newHoveredBandIndex >= 0 ? clampEqBandIndex(newHoveredBandIndex) : -1;
    if (hoveredEqBandIndex == normalizedIndex)
        return;

    hoveredEqBandIndex = normalizedIndex;
    if (draggedEqBandIndex >= 0)
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(hoveredEqBandIndex >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint(toDisplaySpace(eqDisplayBounds));
}

void SoundModuleComponent::updateEqBandUiFromSelection()
{
    const int bandIndex = currentEqBandIndex();
    currentSoundState.eq.selectedBand = bandIndex;

    switch (bandIndex)
    {
        case 0:
            eqDescriptorLabel.setText("Rumble trim before the shell", juce::dontSendNotification);
            eqBandMeaningLabel.setText("FOCUS: WEIGHT", juce::dontSendNotification);
            eqBandRangeLabel.setText("25-80 Hz  |  rumble / sub cleanup / floor", juce::dontSendNotification);
            eqFreqSlider.setRange(25.0, 80.0, 1.0);
            eqFreqSlider.setSkewFactorFromMidPoint(45.0);
            eqGainSlider.setRange(-12.0, 12.0, 0.1);
            eqQSlider.setRange(0.3, 2.2, 0.01);
            break;
        case 1:
            eqDescriptorLabel.setText("Low-end thump and weight", juce::dontSendNotification);
            eqBandMeaningLabel.setText("FOCUS: WEIGHT", juce::dontSendNotification);
            eqBandRangeLabel.setText("40-90 Hz  |  weight / thump / low mass", juce::dontSendNotification);
            eqFreqSlider.setRange(40.0, 90.0, 1.0);
            eqFreqSlider.setSkewFactorFromMidPoint(65.0);
            eqGainSlider.setRange(-12.0, 6.0, 0.1);
            eqQSlider.setRange(0.3, 2.2, 0.01);
            break;
        case 2:
            eqDescriptorLabel.setText("Shell weight and punch", juce::dontSendNotification);
            eqBandMeaningLabel.setText("FOCUS: BODY", juce::dontSendNotification);
            eqBandRangeLabel.setText("120-250 Hz  |  body / punch / fullness", juce::dontSendNotification);
            eqFreqSlider.setRange(120.0, 250.0, 1.0);
            eqFreqSlider.setSkewFactorFromMidPoint(180.0);
            eqGainSlider.setRange(-10.0, 8.0, 0.1);
            eqQSlider.setRange(0.4, 2.8, 0.01);
            break;
        case 3:
            eqDescriptorLabel.setText("Mud and box control", juce::dontSendNotification);
            eqBandMeaningLabel.setText("FOCUS: BOX", juce::dontSendNotification);
            eqBandRangeLabel.setText("300-700 Hz  |  mud / boxiness / ring", juce::dontSendNotification);
            eqFreqSlider.setRange(300.0, 700.0, 1.0);
            eqFreqSlider.setSkewFactorFromMidPoint(420.0);
            eqGainSlider.setRange(-12.0, 6.0, 0.1);
            eqQSlider.setRange(0.5, 4.0, 0.01);
            break;
        case 4:
            eqDescriptorLabel.setText("Stick, click and bite", juce::dontSendNotification);
            eqBandMeaningLabel.setText("FOCUS: ATTACK", juce::dontSendNotification);
            eqBandRangeLabel.setText("2-5 kHz  |  attack / snap / click", juce::dontSendNotification);
            eqFreqSlider.setRange(2000.0, 5000.0, 1.0);
            eqFreqSlider.setSkewFactorFromMidPoint(3200.0);
            eqGainSlider.setRange(-10.0, 10.0, 0.1);
            eqQSlider.setRange(0.5, 5.0, 0.01);
            break;
        case 5:
            eqDescriptorLabel.setText("Top sheen and openness", juce::dontSendNotification);
            eqBandMeaningLabel.setText("FOCUS: AIR", juce::dontSendNotification);
            eqBandRangeLabel.setText("7-12 kHz  |  air / sheen / sizzle", juce::dontSendNotification);
            eqFreqSlider.setRange(7000.0, 12000.0, 1.0);
            eqFreqSlider.setSkewFactorFromMidPoint(9000.0);
            eqGainSlider.setRange(-8.0, 8.0, 0.1);
            eqQSlider.setRange(0.3, 3.5, 0.01);
            break;
        case 6:
            eqDescriptorLabel.setText("Top-end ceiling and hiss trim", juce::dontSendNotification);
            eqBandMeaningLabel.setText("FOCUS: AIR", juce::dontSendNotification);
            eqBandRangeLabel.setText("9-18 kHz  |  air cap / hiss trim / edge", juce::dontSendNotification);
            eqFreqSlider.setRange(9000.0, 18000.0, 1.0);
            eqFreqSlider.setSkewFactorFromMidPoint(14000.0);
            eqGainSlider.setRange(-12.0, 12.0, 0.1);
            eqQSlider.setRange(0.3, 2.8, 0.01);
            break;
        default:
            eqDescriptorLabel.setText("Shell weight and punch", juce::dontSendNotification);
            eqBandMeaningLabel.setText("FOCUS: BODY", juce::dontSendNotification);
            eqBandRangeLabel.setText("120-250 Hz  |  body / punch / fullness", juce::dontSendNotification);
            eqFreqSlider.setRange(120.0, 250.0, 1.0);
            eqFreqSlider.setSkewFactorFromMidPoint(180.0);
            eqGainSlider.setRange(-10.0, 8.0, 0.1);
            eqQSlider.setRange(0.4, 2.8, 0.01);
            break;
    }

    const auto& band = currentSoundState.eq.bands[static_cast<size_t>(bandIndex)];
    const juce::ScopedValueSetter<bool> scope(isSyncingUi, true);
    eqEnableButton.setToggleState(band.enabled, juce::dontSendNotification);
    eqShapeCombo.setSelectedId(static_cast<int>(band.shape) + 1, juce::dontSendNotification);
    eqFreqSlider.setValue(juce::jlimit(eqFreqSlider.getMinimum(), eqFreqSlider.getMaximum(), static_cast<double>(band.freqHz)),
                          juce::dontSendNotification);
    eqGainSlider.setValue(juce::jlimit(eqGainSlider.getMinimum(), eqGainSlider.getMaximum(), static_cast<double>(band.gainDb)),
                          juce::dontSendNotification);
    eqQSlider.setValue(juce::jlimit(eqQSlider.getMinimum(), eqQSlider.getMaximum(), static_cast<double>(band.q)),
                       juce::dontSendNotification);
}

void SoundModuleComponent::commitEqBandControlsToState()
{
    const int bandIndex = currentEqBandIndex();
    currentSoundState.eq.selectedBand = bandIndex;

    auto& band = currentSoundState.eq.bands[static_cast<size_t>(bandIndex)];
    band.enabled = eqEnableButton.getToggleState();
    band.shape = static_cast<EqBandShape>(juce::jlimit(0, 2, eqShapeCombo.getSelectedId() - 1));
    band.freqHz = static_cast<float>(eqFreqSlider.getValue());
    band.gainDb = static_cast<float>(eqGainSlider.getValue());
    band.q = static_cast<float>(eqQSlider.getValue());
    currentSoundState.eqTone = legacyEqToneFromEqState(currentSoundState.eq);
}

void SoundModuleComponent::syncVisualStateFromCurrentSoundState()
{
    reconcileLegacySoundLayerState(currentSoundState);

    {
        const juce::ScopedValueSetter<bool> scope(isSyncingUi, true);
        eqBandCombo.setSelectedId(currentSoundState.eq.selectedBand + 1, juce::dontSendNotification);
        panSlider.setValue(juce::jlimit(-1.0, 1.0, static_cast<double>(currentSoundState.pan)), juce::dontSendNotification);
        widthSlider.setValue(juce::jlimit(0.0, 2.0, static_cast<double>(currentSoundState.width)), juce::dontSendNotification);
        stereoFocusSlider.setValue(juce::jlimit(0.0, 100.0, static_cast<double>(currentSoundState.stereoFieldFocus) * 100.0),
                                   juce::dontSendNotification);
        stereoEdgeSlider.setValue(juce::jlimit(0.0, 100.0, static_cast<double>(currentSoundState.stereoFieldEdge) * 100.0),
                                  juce::dontSendNotification);
        stereoLowCenterProtectSlider.setValue(juce::jlimit(0.0,
                                                           100.0,
                                                           static_cast<double>(currentSoundState.stereoFieldLowCenterProtect) * 100.0),
                                              juce::dontSendNotification);
        stereoAirSpreadSlider.setValue(juce::jlimit(0.0,
                                                    100.0,
                                                    static_cast<double>(currentSoundState.stereoFieldAirSpread) * 100.0),
                                       juce::dontSendNotification);
        stereoMonoSafeButton.setToggleState(currentSoundState.stereoFieldMonoSafe, juce::dontSendNotification);
    }

    updateEqBandUiFromSelection();

    compressorUiState.mix = juce::jlimit(0.0, 100.0, static_cast<double>(currentSoundState.compressor.mix) * 100.0);
    compressorUiState.ratio = currentSoundState.compressor.ratio;
    compressorUiState.threshold = currentSoundState.compressor.thresholdDb;
    compressorUiState.attack = currentSoundState.compressor.attackMs;
    compressorUiState.release = currentSoundState.compressor.releaseMs;
    compressorUiState.saturation = static_cast<double>(currentSoundState.compressor.saturation) * 100.0;

    reverbUiState.mix = reverbUiPercent(currentSoundState.drumReverb.mix);
    reverbUiState.size = reverbUiPercent(currentSoundState.drumReverb.size);
    reverbUiState.predelay = currentSoundState.drumReverb.predelayMs;
    reverbUiState.tail = reverbUiPercent(currentSoundState.drumReverb.erTail);

    monstaUiState.dry = static_cast<double>(currentSoundState.monstaFx.dry) * 100.0;
    monstaUiState.wet = static_cast<double>(currentSoundState.monstaFx.wet) * 100.0;

    transientUiState.attack = static_cast<double>(currentSoundState.drumTransient.attack) * 100.0;
    transientUiState.sustain = static_cast<double>(currentSoundState.drumTransient.sustain) * 100.0;
    transientUiState.gain = currentSoundState.drumTransient.gainDb;

    syncCompressorVisuals();
    syncReverbVisuals();
    syncMonstaFxVisuals();
    syncTransientVisuals();
}

void SoundModuleComponent::syncCompressorVisuals()
{
    const juce::ScopedValueSetter<bool> scope(isSyncingUi, true);
    compPowerButton.setToggleState(currentSoundState.compressor.enabled, juce::dontSendNotification);
    compOrderCombo.setSelectedId(compressorModeSelectionId(currentSoundState.compressor), juce::dontSendNotification);
    compRatioSlider.setValue(compressorUiState.ratio, juce::dontSendNotification);
    compMixSlider.setValue(compressorUiState.mix, juce::dontSendNotification);
    compThresholdSlider.setValue(compressorUiState.threshold, juce::dontSendNotification);
    compAttackSlider.setValue(compressorUiState.attack, juce::dontSendNotification);
    compReleaseSlider.setValue(compressorUiState.release, juce::dontSendNotification);
    compSaturationSlider.setValue(compressorUiState.saturation, juce::dontSendNotification);
    updateCompressorDescriptor();
}

void SoundModuleComponent::updateCompressorDescriptor()
{
    compDescriptorLabel.setText(compressorDescriptorText(currentSoundState.compressor), juce::dontSendNotification);
}

void SoundModuleComponent::ensureCompressorDefaultsForActivation()
{
    if (compMixSlider.getValue() >= 1.0)
        return;

    const auto defaults = createDefaultCompressorState();
    const juce::ScopedValueSetter<bool> scope(isSyncingUi, true);
    compressorUiState.ratio = defaults.ratio;
    compressorUiState.threshold = defaults.thresholdDb;
    compressorUiState.mix = defaults.mix * 100.0;
    compressorUiState.attack = defaults.attackMs;
    compressorUiState.release = defaults.releaseMs;
    compressorUiState.saturation = defaults.saturation * 100.0;
    compOrderCombo.setSelectedId(compressorModeSelectionId(defaults), juce::dontSendNotification);
    compRatioSlider.setValue(compressorUiState.ratio, juce::dontSendNotification);
    compThresholdSlider.setValue(compressorUiState.threshold, juce::dontSendNotification);
    compMixSlider.setValue(compressorUiState.mix, juce::dontSendNotification);
    compAttackSlider.setValue(compressorUiState.attack, juce::dontSendNotification);
    compReleaseSlider.setValue(compressorUiState.release, juce::dontSendNotification);
    compSaturationSlider.setValue(compressorUiState.saturation, juce::dontSendNotification);
}

void SoundModuleComponent::applyCompressorUiToState()
{
    auto compressor = currentSoundState.compressor;

    compressor.enabled = compPowerButton.getToggleState();
    compressor.ratio = static_cast<float>(compRatioSlider.getValue());
    compressor.thresholdDb = static_cast<float>(compThresholdSlider.getValue());
    compressor.mix = juce::jlimit(0.0f, 1.0f, static_cast<float>(compMixSlider.getValue() / 100.0));
    compressor.attackMs = static_cast<float>(compAttackSlider.getValue());
    compressor.releaseMs = static_cast<float>(compReleaseSlider.getValue());
    compressor.saturation = juce::jlimit(0.0f, 1.0f, static_cast<float>(compSaturationSlider.getValue() / 100.0));

    applyCompressorModeSelection(compOrderCombo.getSelectedId(), compressor);
    sanitizeCompressorState(compressor);
    currentSoundState.compressor = compressor;
    syncLegacySoundLayerState(currentSoundState);
    updateCompressorDescriptor();
}

void SoundModuleComponent::syncReverbVisuals()
{
    const juce::ScopedValueSetter<bool> scope(isSyncingUi, true);
    reverbSizeSlider.setValue(reverbUiState.size, juce::dontSendNotification);
    reverbMixSlider.setValue(reverbUiState.mix, juce::dontSendNotification);
    reverbPredelaySlider.setValue(reverbUiState.predelay, juce::dontSendNotification);
    reverbTailSlider.setValue(reverbUiState.tail, juce::dontSendNotification);
}

void SoundModuleComponent::syncMonstaFxVisuals()
{
    const juce::ScopedValueSetter<bool> scope(isSyncingUi, true);
    monstaDrySlider.setValue(monstaUiState.dry, juce::dontSendNotification);
    monstaWetSlider.setValue(monstaUiState.wet, juce::dontSendNotification);
    monstaDescriptorLabel.setText(buildMonstaCollapsedCharacter(currentSoundState.monstaFx), juce::dontSendNotification);
}

void SoundModuleComponent::syncTransientVisuals()
{
    const juce::ScopedValueSetter<bool> scope(isSyncingUi, true);
    transientAttackSlider.setValue(transientUiState.attack, juce::dontSendNotification);
    transientSustainSlider.setValue(transientUiState.sustain, juce::dontSendNotification);
    transientGainSlider.setValue(transientUiState.gain, juce::dontSendNotification);
    smoothButton.setToggleState(currentSoundState.drumTransient.smooth, juce::dontSendNotification);
    limitButton.setToggleState(currentSoundState.drumTransient.limit, juce::dontSendNotification);
    transientDescriptorLabel.setText(buildTransientCollapsedCharacter(smoothButton.getToggleState(), limitButton.getToggleState()),
                                     juce::dontSendNotification);
}

void SoundModuleComponent::handleCompressionAmountChange(float normalizedValue)
{
    compressorUiState.mix = juce::jlimit(0.0, 100.0, static_cast<double>(normalizedValue) * 100.0);
    applyCompressorUiToState();
    emitSoundLayerChange();
}

void SoundModuleComponent::handleGateAmountChange(float normalizedValue)
{
    compressorUiState.threshold = -6.0 - juce::jlimit(0.0, 1.0, static_cast<double>(normalizedValue)) * 30.0;
    applyCompressorUiToState();
    emitSoundLayerChange();
}

void SoundModuleComponent::handleDriveAmountChange(float normalizedValue)
{
    compressorUiState.saturation = juce::jlimit(0.0, 100.0, static_cast<double>(normalizedValue) * 100.0);
    applyCompressorUiToState();
    emitSoundLayerChange();
}

void SoundModuleComponent::handleReverbAmountChange(float normalizedValue)
{
    reverbUiState.mix = reverbUiPercent(normalizedValue);

    auto reverb = currentSoundState.drumReverb;
    reverb.enabled = normalizedValue > 0.001f;
    reverb.mix = juce::jlimit(0.0f, 1.0f, normalizedValue);
    reverb.predelayMs = juce::jlimit(0.0f, 60.0f, static_cast<float>(reverbUiState.predelay));
    reverb.size = reverbNormalized(reverbUiState.size);
    reverb.erTail = reverbNormalized(reverbUiState.tail);
    sanitizeDrumReverbState(reverb);
    currentSoundState.drumReverb = reverb;
    syncLegacySoundLayerState(currentSoundState);
    emitSoundLayerChange();
}

void SoundModuleComponent::handleTransientAmountChange(float normalizedValue)
{
    currentSoundState.drumTransient.attack = juce::jlimit(0.0f, 1.0f, normalizedValue);
    transientUiState.attack = static_cast<double>(currentSoundState.drumTransient.attack) * 100.0;
    syncTransientVisuals();
    emitSoundLayerChange();
}

void SoundModuleComponent::resetCurrentState()
{
    if (!targetAvailable)
        return;

    currentSoundState = {};
    currentSoundState.width = 1.0f;
    currentSoundState.compressor = createInactiveCompressorState();
    compressorUiState = {};
    reverbUiState = {};
    monstaUiState = {};
    transientUiState = {};

    bypassButton.setToggleState(false, juce::dontSendNotification);
    eqOrderCombo.setSelectedId(1, juce::dontSendNotification);
    compOrderCombo.setSelectedId(3, juce::dontSendNotification);
    syncVisualStateFromCurrentSoundState();
    updateStripSelection();
    emitSoundLayerChange();
}

void SoundModuleComponent::emitSoundLayerChange()
{
    currentSoundState.eq.selectedBand = clampEqBandIndex(currentSoundState.eq.selectedBand);
    currentSoundState.pan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(panSlider.getValue()));
    currentSoundState.width = juce::jlimit(0.0f, 2.0f, static_cast<float>(widthSlider.getValue()));
    currentSoundState.stereoFieldEnabled = true;
    currentSoundState.stereoFieldFocus = juce::jlimit(0.0f, 1.0f, static_cast<float>(stereoFocusSlider.getValue() / 100.0));
    currentSoundState.stereoFieldEdge = juce::jlimit(0.0f, 1.0f, static_cast<float>(stereoEdgeSlider.getValue() / 100.0));
    currentSoundState.stereoFieldMonoSafe = stereoMonoSafeButton.getToggleState();
    currentSoundState.stereoFieldLowCenterProtect = juce::jlimit(0.0f,
                                                                 1.0f,
                                                                 static_cast<float>(stereoLowCenterProtectSlider.getValue() / 100.0));
    currentSoundState.stereoFieldAirSpread = juce::jlimit(0.0f,
                                                          1.0f,
                                                          static_cast<float>(stereoAirSpreadSlider.getValue() / 100.0));
    currentSoundState.compression = juce::jlimit(0.0f, 1.0f, currentSoundState.compression);
    currentSoundState.reverb = juce::jlimit(0.0f, 1.0f, currentSoundState.reverb);
    currentSoundState.gate = juce::jlimit(0.0f, 1.0f, currentSoundState.gate);
    currentSoundState.monstaFx.dry = juce::jlimit(0.0f, 1.0f, static_cast<float>(monstaDrySlider.getValue() / 100.0));
    currentSoundState.monstaFx.wet = juce::jlimit(0.0f, 1.0f, static_cast<float>(monstaWetSlider.getValue() / 100.0));
    sanitizeMonstaFxState(currentSoundState.monstaFx);
    currentSoundState.drumTransient.attack = juce::jlimit(0.0f, 1.0f, static_cast<float>(transientAttackSlider.getValue() / 100.0));
    currentSoundState.drumTransient.sustain = juce::jlimit(0.0f, 1.0f, static_cast<float>(transientSustainSlider.getValue() / 100.0));
    currentSoundState.drumTransient.gainDb = juce::jlimit(-6.0f, 12.0f, static_cast<float>(transientGainSlider.getValue()));
    currentSoundState.drumTransient.smooth = smoothButton.getToggleState();
    currentSoundState.drumTransient.limit = limitButton.getToggleState();
    sanitizeDrumTransientState(currentSoundState.drumTransient);
    syncLegacySoundLayerState(currentSoundState);

    updateValueLabels();

    if (!onSoundLayerChanged || !targetAvailable)
        return;

    onSoundLayerChanged(currentTarget, currentSoundState);
}
} // namespace bbg
