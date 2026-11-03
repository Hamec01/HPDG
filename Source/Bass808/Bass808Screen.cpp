#include "Bass808Screen.h"

#include <complex>

#include "Audio/Synth808Voice.h"
#include "Bass808/Bass808Widgets.h"
#include "Bass808/BassSaturator.h"

namespace bbg
{
namespace
{
using namespace b808;

// Scientific pitch, as the rest of HPDG: 65.4 Hz is C2.
juce::String noteName(int note) { return juce::MidiMessage::getMidiNoteName(note, true, true, 4); }

// The waveform of `data` (from `first`, `count` samples) as min / max per pixel column.
void drawWave(juce::Graphics& g, const std::vector<float>& data, size_t first, size_t count, juce::Rectangle<float> area,
              juce::Colour fill, float scale = 1.0f)
{
    if (data.empty() || count == 0 || area.getWidth() < 2.0f)
        return;
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f * scale;
    juce::Path path;
    const int columns = static_cast<int>(area.getWidth());
    std::vector<float> top(static_cast<size_t>(columns)), bottom(static_cast<size_t>(columns));
    for (int x = 0; x < columns; ++x)
    {
        const size_t a = first + count * static_cast<size_t>(x) / static_cast<size_t>(columns);
        const size_t b = std::max(a + 1, first + count * static_cast<size_t>(x + 1) / static_cast<size_t>(columns));
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = a; i < b && i < data.size(); ++i)
        {
            lo = std::min(lo, data[i]);
            hi = std::max(hi, data[i]);
        }
        top[static_cast<size_t>(x)] = mid - juce::jlimit(-1.0f, 1.0f, hi) * half;
        bottom[static_cast<size_t>(x)] = mid - juce::jlimit(-1.0f, 1.0f, lo) * half;
    }
    path.startNewSubPath(area.getX(), top[0]);
    for (int x = 1; x < columns; ++x)
        path.lineTo(area.getX() + static_cast<float>(x), top[static_cast<size_t>(x)]);
    for (int x = columns - 1; x >= 0; --x)
        path.lineTo(area.getX() + static_cast<float>(x), bottom[static_cast<size_t>(x)]);
    path.closeSubPath();
    g.setColour(fill.withAlpha(0.55f));
    g.fillPath(path);
    g.setColour(fill);
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

// A line through `count` samples from `first` (a few periods up close).
void drawLine(juce::Graphics& g, const float* data, int count, juce::Rectangle<float> area, juce::Colour colour, float thickness, float scale)
{
    if (count < 2)
        return;
    juce::Path path;
    for (int i = 0; i < count; ++i)
    {
        const float x = area.getX() + area.getWidth() * static_cast<float>(i) / static_cast<float>(count - 1);
        const float y = area.getCentreY() - juce::jlimit(-1.2f, 1.2f, data[i] * scale) * area.getHeight() * 0.45f;
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(0.25f));
    g.strokePath(path, juce::PathStrokeType(thickness * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void grid(juce::Graphics& g, juce::Rectangle<float> area, int columns, int rows)
{
    g.setColour(colour::text.withAlpha(0.06f));
    for (int i = 1; i < columns; ++i)
        g.drawVerticalLine(juce::roundToInt(area.getX() + area.getWidth() * static_cast<float>(i) / static_cast<float>(columns)), area.getY(), area.getBottom());
    for (int i = 1; i < rows; ++i)
        g.drawHorizontalLine(juce::roundToInt(area.getY() + area.getHeight() * static_cast<float>(i) / static_cast<float>(rows)), area.getX(), area.getRight());
    g.setColour(colour::text.withAlpha(0.12f));
    g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());
}

void label(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Colour c, float size = 12.0f,
           juce::Justification just = juce::Justification::topLeft)
{
    g.setColour(c);
    g.setFont(juce::FontOptions(size));
    g.drawText(text, area, just);
}

float peakOf(const std::vector<float>& data)
{
    float peak = 1.0e-6f;
    for (auto v : data)
        peak = std::max(peak, std::abs(v));
    return peak;
}

// Seconds until the level (20 ms RMS) is `dropDb` under its peak.
double timeToDrop(const std::vector<float>& data, double rate, double dropDb)
{
    const int window = static_cast<int>(0.02 * rate);
    if (static_cast<int>(data.size()) < 2 * window)
        return 0.0;
    std::vector<double> level;
    double sum = 0.0;
    for (size_t i = 0; i < data.size(); ++i)
    {
        sum += static_cast<double>(data[i]) * data[i];
        if (i >= static_cast<size_t>(window))
            sum -= static_cast<double>(data[i - static_cast<size_t>(window)]) * data[i - static_cast<size_t>(window)];
        level.push_back(sum);
    }
    const auto peakAt = std::max_element(level.begin(), level.end());
    const double threshold = *peakAt * std::pow(10.0, -dropDb / 10.0);
    for (auto it = peakAt; it != level.end(); ++it)
        if (*it < threshold)
            return static_cast<double>(std::distance(level.begin(), it)) / rate;
    return -1.0; // longer than the preview
}
} // namespace

Bass808Screen::Bass808Screen(Bass808Processor& processor) : bass(processor)
{
    live.resize(4096);
    startTimerHz(30);
}

void Bass808Screen::setMode(Mode newMode)
{
    mode = newMode;
    repaint();
}

juce::Rectangle<float> Bass808Screen::plotArea() const
{
    return getLocalBounds().toFloat().reduced(14.0f, 12.0f).withTrimmedTop(18.0f);
}

void Bass808Screen::timerCallback()
{
    // re-render the previews when the knobs, the sample or the note changed (and then held still 100 ms)
    std::vector<float> now;
    for (auto* parameter : bass.getParameters())
        now.push_back(parameter->getValue());
    const auto sample = bass.getSample();
    if (now != snapshot || sample != renderedSample || bass.lastNote() != renderedNote)
    {
        snapshot = std::move(now);
        renderedSample = sample;
        renderedNote = bass.lastNote();
        settleTicks = 3;
        repaint();
    }
    if (settleTicks > 0 && --settleTicks == 0)
    {
        renderPreviews();
        repaint();
    }

    const bool wasLive = liveActive;
    liveActive = bass.recentPeak() > 0.002f;
    if (liveActive)
        bass.copyScope(live.data(), static_cast<int>(live.size()));
    if ((liveActive || wasLive) && (mode == Synth || mode == Mix))
        repaint();
}

void Bass808Screen::renderPreviews()
{
    settings = bass.currentSettings();
    const int note = renderedNote;
    full = BassEngine::renderNote(settings, renderedSample, note, kSeconds, kRate);
    synthLayer.clear();
    sampleLayer.clear();
    if (renderedSample != nullptr && settings.sampleLevelDb > -59.5f && settings.synthLevelDb > -59.5f)
    {
        // each layer with the other one almost silent (not off: the handoff stays as it is)
        auto onlySynth = settings, onlySample = settings;
        onlySynth.sampleLevelDb = -59.0f;
        onlySample.synthLevelDb = -59.0f;
        synthLayer = BassEngine::renderNote(onlySynth, renderedSample, note, kMixSeconds, kRate);
        sampleLayer = BassEngine::renderNote(onlySample, renderedSample, note, kMixSeconds, kRate);
    }
}

void Bass808Screen::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colour::deep);
    g.fillRoundedRectangle(bounds, 8.0f);
    juce::ColourGradient glow(colour::teal.withAlpha(0.08f), bounds.getCentreX(), bounds.getCentreY(),
                              juce::Colours::transparentBlack, bounds.getX(), bounds.getY(), true);
    g.setGradientFill(glow);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(colour::panelEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    const auto area = plotArea();
    switch (mode)
    {
        case Synth: paintSynth(g, area); break;
        case Sample: paintSample(g, area); break;
        case Mix: paintMix(g, area); break;
        case Effects: paintEffects(g, area); break;
    }
}

void Bass808Screen::paintSynth(juce::Graphics& g, juce::Rectangle<float> area)
{
    auto noteArea = area.removeFromLeft(area.getWidth() * 0.66f);
    area.removeFromLeft(14.0f);
    const auto cycleArea = area;
    grid(g, noteArea, 8, 4);
    grid(g, cycleArea, 3, 4);

    const double hz = BassEngine::noteHz(renderedNote, settings.tuneSemitones);
    if (!full.empty())
    {
        const float scale = 0.95f / peakOf(full);
        drawWave(g, full, 0, full.size(), noteArea, colour::accentGlow, scale);
        // up close: three periods of the body, 0.3 s in (past the punch)
        const int period = juce::jmax(2, static_cast<int>(kRate / hz));
        const size_t from = std::min(full.size() - 1, static_cast<size_t>(0.3 * kRate));
        size_t start = from;
        while (start + 1 < full.size() && start < from + static_cast<size_t>(period) && !(full[start] <= 0.0f && full[start + 1] > 0.0f))
            ++start;
        const int count = std::min(3 * period, static_cast<int>(full.size() - start));
        drawLine(g, full.data() + start, count, cycleArea, colour::accentGlow.withAlpha(liveActive ? 0.35f : 1.0f), 1.6f, scale);

        const double t6 = timeToDrop(full, kRate, 6.0), t24 = timeToDrop(full, kRate, 24.0);
        const auto seconds = [](double t) { return t < 0.0 ? juce::String(">1.6 s") : juce::String(t, 2) + " s"; };
        label(g, "-6 dB " + seconds(t6) + "    -24 dB " + seconds(t24), noteArea.withHeight(16.0f).translated(0.0f, noteArea.getHeight() - 14.0f),
              colour::textMuted, 11.0f, juce::Justification::bottomRight);
    }
    if (liveActive)
    {
        // the live output, three periods of the note, from a rising zero crossing
        const double rate = bass.getSampleRate() > 0.0 ? bass.getSampleRate() : 48000.0;
        const int period = juce::jmax(2, static_cast<int>(rate / BassEngine::noteHz(bass.lastNote(), settings.tuneSemitones)));
        const int count = std::min(3 * period, static_cast<int>(live.size()) / 2);
        int start = static_cast<int>(live.size()) - count - 1;
        for (int i = start; i > start - period && i > 0; --i)
            if (live[static_cast<size_t>(i)] <= 0.0f && live[static_cast<size_t>(i + 1)] > 0.0f)
            {
                start = i;
                break;
            }
        drawLine(g, live.data() + start, count, cycleArea, colour::tealBright, 1.6f, 0.95f / juce::jmax(0.05f, bass.recentPeak()));
        label(g, "LIVE", cycleArea.withHeight(16.0f), colour::tealBright, 11.0f, juce::Justification::topRight);
    }

    static const char* waves[] { "SINE", "TRIANGLE", "SAW", "SQUARE", "REESE" };
    label(g, juce::String("NOTE  ") + noteName(renderedNote) + "  " + juce::String(hz, 1) + " Hz   " + waves[juce::jlimit(0, 4, settings.wave)],
          getLocalBounds().toFloat().reduced(14.0f, 8.0f), colour::text, 12.0f);
    label(g, "WAVEFORM", cycleArea.withY(getLocalBounds().toFloat().getY() + 8.0f).withHeight(16.0f), colour::textMuted, 11.0f);
    label(g, "1.6 s", noteArea.withHeight(14.0f).translated(0.0f, -16.0f), colour::textMuted, 11.0f, juce::Justification::topRight);
}

float Bass808Screen::markerValue(int marker) const
{
    return bass.apvts.getRawParameterValue(markerId(marker))->load();
}

const char* Bass808Screen::markerId(int marker) const
{
    return marker == 0 ? "smpStart" : (marker == 1 ? "smpLoopStart" : "smpEnd");
}

int Bass808Screen::markerAt(juce::Point<float> p) const
{
    const auto area = plotArea();
    int best = -1;
    float distance = 9.0f;
    for (int m = 0; m < 3; ++m)
    {
        if (m == 1 && bass.apvts.getRawParameterValue("smpLoop")->load() < 0.5f)
            continue;
        const float x = area.getX() + area.getWidth() * markerValue(m);
        if (std::abs(p.x - x) < distance)
        {
            distance = std::abs(p.x - x);
            best = m;
        }
    }
    return best;
}

void Bass808Screen::paintSample(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto sample = renderedSample;
    if (sample == nullptr)
    {
        auto box = area.reduced(40.0f, 20.0f);
        juce::Path outline;
        outline.addRoundedRectangle(box, 10.0f);
        juce::Path dashed;
        const float dashes[] { 8.0f, 6.0f };
        juce::PathStrokeType(1.4f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(colour::teal);
        g.fillPath(dashed);
        label(g, "DROP A WAV HERE  or press LOAD", box.withTrimmedBottom(box.getHeight() * 0.45f), colour::textBright, 18.0f,
              juce::Justification::centredBottom);
        label(g, "a kick becomes an 808: loop its body, play it on the keys, stretch it - or layer any bass one-shot under the synth",
              box.withTrimmedTop(box.getHeight() * 0.6f), colour::textMuted, 12.5f, juce::Justification::centredTop);
        return;
    }
    grid(g, area, 10, 4);
    const auto& data = sample->data;
    const float mid = area.getCentreY(), half = area.getHeight() * 0.47f;
    const int columns = static_cast<int>(area.getWidth());
    const float start = markerValue(0), end = markerValue(2), loop = markerValue(1);
    const bool looping = bass.apvts.getRawParameterValue("smpLoop")->load() >= 0.5f;
    for (int x = 0; x < columns; ++x)
    {
        const float position = static_cast<float>(x) / static_cast<float>(columns);
        const size_t a = data.size() * static_cast<size_t>(x) / static_cast<size_t>(columns);
        const size_t b = std::max(a + 1, data.size() * static_cast<size_t>(x + 1) / static_cast<size_t>(columns));
        float lo = 0.0f, hi = 0.0f;
        for (size_t i = a; i < b && i < data.size(); ++i)
        {
            lo = std::min(lo, data[i]);
            hi = std::max(hi, data[i]);
        }
        const bool inside = position >= start && position <= end;
        auto c = inside ? colour::accentGlow : colour::textMuted.withAlpha(0.35f);
        if (inside && looping && position >= loop)
            c = colour::tealBright;
        g.setColour(c);
        g.drawVerticalLine(juce::roundToInt(area.getX()) + x, mid - hi * half, mid - lo * half + 1.0f);
    }
    if (looping)
    {
        const float x0 = area.getX() + area.getWidth() * loop, x1 = area.getX() + area.getWidth() * end;
        g.setColour(colour::teal.withAlpha(0.16f));
        g.fillRect(juce::Rectangle<float>(x0, area.getY(), x1 - x0, area.getHeight()));
    }
    static const char* names[] { "START", "LOOP", "END" };
    for (int m = 0; m < 3; ++m)
    {
        if (m == 1 && !looping)
            continue;
        const float x = area.getX() + area.getWidth() * markerValue(m);
        const auto c = m == 1 ? colour::tealBright : colour::textBright;
        g.setColour(c.withAlpha(m == hovering || m == dragging ? 1.0f : 0.75f));
        g.drawLine(x, area.getY(), x, area.getBottom(), m == hovering || m == dragging ? 2.0f : 1.2f);
        juce::Path flag;
        flag.addTriangle(x - 6.0f, area.getY(), x + 6.0f, area.getY(), x, area.getY() + 8.0f);
        g.fillPath(flag);
        const bool left = m == 2; // END reads to the left of its line
        label(g, names[m], juce::Rectangle<float>(left ? x - 54.0f : x + 4.0f, area.getBottom() - 14.0f, 50.0f, 14.0f), c, 10.5f,
              left ? juce::Justification::topRight : juce::Justification::topLeft);
    }

    juce::String info = sample->name + "   " + juce::String(sample->lengthSeconds(), 2) + " s   ";
    if (sample->rootHz > 0.0)
    {
        const double note = 69.0 + 12.0 * std::log2(sample->rootHz / 440.0);
        const int nearest = juce::roundToInt(note);
        const int cents = juce::roundToInt((note - nearest) * 100.0);
        info += "pitch " + noteName(nearest) + (cents != 0 ? juce::String(cents > 0 ? " +" : " ") + juce::String(cents) + " ct" : juce::String())
                + "  (" + juce::String(sample->rootHz, 1) + " Hz)";
    }
    else
    {
        info += "no pitch found: played as C2";
    }
    label(g, info, getLocalBounds().toFloat().reduced(14.0f, 8.0f), colour::text, 12.0f);
    label(g, "drag START / LOOP / END", getLocalBounds().toFloat().reduced(14.0f, 8.0f), colour::textMuted, 11.0f, juce::Justification::topRight);
}

void Bass808Screen::paintMix(juce::Graphics& g, juce::Rectangle<float> area)
{
    grid(g, area, 6, 4);
    const size_t count = static_cast<size_t>(kMixSeconds * kRate);
    if (!full.empty())
    {
        const float scale = 0.95f / peakOf(full);
        if (!synthLayer.empty())
        {
            drawWave(g, synthLayer, 0, std::min(count, synthLayer.size()), area, colour::teal, scale);
            drawWave(g, sampleLayer, 0, std::min(count, sampleLayer.size()), area, colour::text.withAlpha(0.7f), scale);
        }
        juce::Path sum;
        const int columns = static_cast<int>(area.getWidth());
        for (int x = 0; x < columns; ++x)
        {
            const size_t a = std::min(full.size() - 1, count * static_cast<size_t>(x) / static_cast<size_t>(columns));
            const size_t b = std::min(full.size(), std::max(a + 1, count * static_cast<size_t>(x + 1) / static_cast<size_t>(columns)));
            float hi = 0.0f;
            for (size_t i = a; i < b; ++i)
                hi = std::max(hi, std::abs(full[i]));
            const float y = area.getCentreY() - juce::jlimit(0.0f, 1.0f, hi * scale) * area.getHeight() * 0.5f;
            if (x == 0)
                sum.startNewSubPath(area.getX(), y);
            else
                sum.lineTo(area.getX() + static_cast<float>(x), y);
        }
        g.setColour(colour::accentGlow);
        g.strokePath(sum, juce::PathStrokeType(1.8f));
    }
    const float handoff = bass.apvts.getRawParameterValue("handoff")->load();
    const float x = area.getX() + area.getWidth() * static_cast<float>(handoff * 0.001 / kMixSeconds);
    if (handoff >= 0.5f)
    {
        g.setColour(colour::textBright.withAlpha(dragging == 3 ? 1.0f : 0.7f));
        g.drawLine(x, area.getY(), x, area.getBottom(), dragging == 3 ? 2.0f : 1.2f);
        label(g, "HANDOFF " + juce::String(juce::roundToInt(handoff)) + " ms", juce::Rectangle<float>(x + 5.0f, area.getY(), 140.0f, 14.0f),
              colour::textBright, 11.0f);
    }
    const auto top = getLocalBounds().toFloat().reduced(14.0f, 8.0f);
    if (renderedSample == nullptr)
        label(g, "LAYERS   no sample loaded: the synth alone (load one on the SAMPLE tab)", top, colour::text, 12.0f);
    else
    {
        label(g, "LAYERS   ", top, colour::text, 12.0f);
        label(g, "synth", top.withTrimmedLeft(70.0f), colour::tealBright, 12.0f);
        label(g, "sample", top.withTrimmedLeft(112.0f), colour::textBright, 12.0f);
        label(g, "sum (envelope)", top.withTrimmedLeft(164.0f), colour::accentGlow, 12.0f);
        label(g, handoff >= 0.5f ? "drag the handoff point" : "layered: drag across to hand the sample over to the synth", top,
              colour::textMuted, 11.0f, juce::Justification::topRight);
    }
    label(g, "0.6 s", area.withTrimmedTop(area.getHeight() - 14.0f), colour::textMuted, 11.0f, juce::Justification::bottomRight);
}

void Bass808Screen::paintEffects(juce::Graphics& g, juce::Rectangle<float> area)
{
    auto filterArea = area.removeFromLeft(area.getWidth() * 0.56f);
    area.removeFromLeft(18.0f);
    auto satArea = area;
    grid(g, filterArea, 8, 6);

    // filter response (the analog prototypes of the voice's SVF), 20 Hz .. 8 kHz, -36 .. +18 dB
    const double hz = BassEngine::noteHz(renderedNote, settings.tuneSemitones);
    const double cutoff = juce::jlimit(16.0, 20000.0, settings.cutoffHz * std::exp2(settings.filterKey * std::log2(hz / Synth808Voice::kC2Hz)));
    const auto response = [&](double f, double fc) -> double
    {
        using C = std::complex<double>;
        const double k = 1.41421356 * (1.0 - settings.resonance) + 0.06 * settings.resonance;
        const auto section = [](C s, double kk) { return 1.0 / (s * s + kk * s + 1.0); };
        if (settings.filterType == BassSettings::Vowel)
        {
            static constexpr double vowels[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
            static constexpr double weights[3] { 1.0, 0.6, 0.3 };
            const double v = juce::jlimit(0.0, 4.0, static_cast<double>(settings.vowel));
            const int i = std::min(3, static_cast<int>(v));
            const double t = v - i, kv = 1.0 / (5.0 + 15.0 * settings.resonance);
            C sum = section(C(0.0, f / 150.0), 1.41421356);
            for (int n = 0; n < 3; ++n)
            {
                const double fn = vowels[i][n] * (1.0 - t) + vowels[i + 1][n] * t;
                const C s(0.0, f / fn);
                sum += 2.0 * weights[n] * kv * s * section(s, kv);
            }
            return std::abs(sum);
        }
        const C s(0.0, f / fc);
        switch (settings.filterType)
        {
            case BassSettings::LP24: return std::abs(section(s, k) * section(s, 1.41421356));
            case BassSettings::HP12: return std::abs(s * s * section(s, k));
            case BassSettings::BP: return std::abs(k * s * section(s, k));
            default: return std::abs(section(s, k));
        }
    };
    const auto xOf = [&](double f) { return filterArea.getX() + filterArea.getWidth() * static_cast<float>(std::log(f / 20.0) / std::log(8000.0 / 20.0)); };
    const auto yOf = [&](double db) { return filterArea.getY() + filterArea.getHeight() * static_cast<float>((18.0 - db) / 54.0); };
    const bool open = settings.filterType != BassSettings::Vowel && settings.cutoffHz >= 19999.0f && settings.filterEnvOct == 0.0f && settings.lfoDepth == 0.0f;
    // the note's harmonics: which of them pass
    for (int n = 1; n <= 16; ++n)
    {
        const double f = hz * n;
        if (f > 8000.0)
            break;
        g.setColour((n == 1 ? colour::tealBright : colour::teal).withAlpha(n == 1 ? 0.8f : 0.45f));
        const float x = xOf(f);
        g.drawLine(x, filterArea.getBottom() - (n == 1 ? 26.0f : 14.0f), x, filterArea.getBottom(), n == 1 ? 2.0f : 1.2f);
    }
    const auto curve = [&](double fc, juce::Colour c, float thickness)
    {
        juce::Path path;
        for (int x = 0; x <= static_cast<int>(filterArea.getWidth()); x += 2)
        {
            const double f = 20.0 * std::pow(8000.0 / 20.0, x / filterArea.getWidth());
            const double db = open ? 0.0 : 20.0 * std::log10(std::max(1.0e-6, response(f, fc)));
            const float y = juce::jlimit(filterArea.getY(), filterArea.getBottom(), yOf(db));
            if (x == 0)
                path.startNewSubPath(filterArea.getX(), y);
            else
                path.lineTo(filterArea.getX() + static_cast<float>(x), y);
        }
        g.setColour(c.withAlpha(0.25f));
        g.strokePath(path, juce::PathStrokeType(thickness * 3.0f));
        g.setColour(c);
        g.strokePath(path, juce::PathStrokeType(thickness));
    };
    if (!open && settings.filterType != BassSettings::Vowel && settings.filterEnvOct != 0.0f)
        curve(juce::jlimit(16.0, 20000.0, cutoff * std::exp2(settings.filterEnvOct)), colour::textMuted, 1.0f); // where the envelope starts
    curve(cutoff, colour::accentGlow, 1.8f);
    g.setColour(colour::text.withAlpha(0.18f));
    g.drawHorizontalLine(juce::roundToInt(yOf(0.0)), filterArea.getX(), filterArea.getRight());
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0 })
        label(g, f >= 1000.0 ? juce::String(f / 1000.0, 0) + "k" : juce::String(f, 0), juce::Rectangle<float>(xOf(f) + 2.0f, filterArea.getBottom() - 12.0f, 30.0f, 12.0f),
              colour::textMuted, 10.0f);
    static const char* types[] { "LP12", "LP24", "HP12", "BP", "VOWEL" };
    const auto top = getLocalBounds().toFloat().reduced(14.0f, 8.0f);
    juce::String title = juce::String("FILTER  ") + types[juce::jlimit(0, 4, settings.filterType)];
    if (open)
        title += "  open";
    else if (settings.filterType != BassSettings::Vowel)
        title += "  " + juce::String(juce::roundToInt(cutoff)) + " Hz at " + noteName(renderedNote);
    label(g, title, top, colour::text, 12.0f);

    // the saturator: transfer curve and harmonics, from its own code
    juce::String satTitle = "SATURATOR  off";
    if (settings.satModel > 0)
    {
        static const char* models[] { "Off", "Classic", "Tape", "Tube", "Iron", "Germanium", "Vinyl" };
        satTitle = juce::String("SATURATOR  ") + models[juce::jlimit(0, 6, settings.satModel)] + "  " + juce::String(settings.satDriveDb, 1) + " dB";
        const auto params = BassEngine::saturatorParams(settings);
        auto curveArea = satArea.removeFromLeft(satArea.getHeight());
        satArea.removeFromLeft(12.0f);
        grid(g, curveArea, 4, 4);
        std::array<float, 256> in {}, out {};
        BassSaturator::transferCurve(params, in, out);
        float outPeak = 1.0e-6f;
        for (auto v : out)
            outPeak = std::max(outPeak, std::abs(v));
        juce::Path transfer;
        for (size_t i = 0; i < in.size(); ++i)
        {
            const float x = curveArea.getCentreX() + in[i] * curveArea.getWidth() * 0.48f;
            const float y = curveArea.getCentreY() - out[i] / outPeak * curveArea.getHeight() * 0.48f;
            if (i == 0)
                transfer.startNewSubPath(x, y);
            else
                transfer.lineTo(x, y);
        }
        g.setColour(colour::accentGlow);
        g.strokePath(transfer, juce::PathStrokeType(1.6f));
        std::array<double, 9> db {};
        BassSaturator::harmonicProfile(params, -12.0, db);
        const float barWidth = satArea.getWidth() / 9.0f;
        for (size_t n = 0; n < db.size(); ++n)
        {
            const float height = juce::jlimit(0.0f, 1.0f, static_cast<float>((db[n] + 72.0) / 72.0)) * satArea.getHeight();
            const auto bar = juce::Rectangle<float>(satArea.getX() + barWidth * static_cast<float>(n) + 2.0f, satArea.getBottom() - height, barWidth - 4.0f, height);
            g.setColour(n == 0 ? colour::tealBright : (n % 2 == 1 ? colour::accentGlow : colour::accent));
            g.fillRect(bar);
            label(g, "H" + juce::String(n + 1), juce::Rectangle<float>(bar.getX(), satArea.getBottom() + 1.0f, barWidth, 12.0f), colour::textMuted, 9.5f);
        }
    }
    else
    {
        label(g, "pick a model: Classic, Tape, Tube,", satArea.withTrimmedBottom(satArea.getHeight() * 0.5f), colour::textMuted, 12.0f,
              juce::Justification::centredBottom);
        label(g, "Iron, Germanium, Vinyl", satArea.withTrimmedTop(satArea.getHeight() * 0.5f + 2.0f), colour::textMuted, 12.0f,
              juce::Justification::centredTop);
    }
    label(g, satTitle, top.withTrimmedLeft(filterArea.getWidth() + 18.0f), colour::text, 12.0f);
}

void Bass808Screen::setParameter(const char* id, float value)
{
    if (auto* parameter = bass.apvts.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void Bass808Screen::mouseMove(const juce::MouseEvent& event)
{
    const int now = mode == Sample ? markerAt(event.position) : -1;
    if (now != hovering)
    {
        hovering = now;
        setMouseCursor(now >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void Bass808Screen::mouseDown(const juce::MouseEvent& event)
{
    dragging = -1;
    dragId = nullptr;
    if (mode == Sample && renderedSample != nullptr)
        dragging = markerAt(event.position);
    else if (mode == Mix)
        dragging = 3;
    if (dragging >= 0)
    {
        dragId = dragging == 3 ? "handoff" : markerId(dragging);
        if (auto* parameter = bass.apvts.getParameter(dragId))
            parameter->beginChangeGesture();
        mouseDrag(event);
    }
}

void Bass808Screen::mouseDrag(const juce::MouseEvent& event)
{
    if (dragId == nullptr)
        return;
    const auto area = plotArea();
    const float position = juce::jlimit(0.0f, 1.0f, (event.position.x - area.getX()) / area.getWidth());
    if (dragging == 3)
    {
        setParameter(dragId, position * static_cast<float>(kMixSeconds * 1000.0) < 3.0f ? 0.0f : position * static_cast<float>(kMixSeconds * 1000.0));
    }
    else
    {
        const float start = markerValue(0), end = markerValue(2);
        float value = position;
        if (dragging == 0)
            value = std::min(value, end - 0.01f);
        else if (dragging == 2)
            value = std::max(value, start + 0.01f);
        else
            value = juce::jlimit(start, end - 0.005f, value);
        setParameter(dragId, value);
    }
    repaint();
}

void Bass808Screen::mouseUp(const juce::MouseEvent&)
{
    if (dragId != nullptr)
        if (auto* parameter = bass.apvts.getParameter(dragId))
            parameter->endChangeGesture();
    dragging = -1;
    dragId = nullptr;
    repaint();
}
} // namespace bbg
