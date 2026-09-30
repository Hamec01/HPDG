#include "SampleBreakStripComponent.h"

#include <array>

#include "SketchDrawing.h"
#include "SketchTheme.h"
#include "../Core/TimingGrid.h"

namespace bbg
{
namespace
{
bool isSupportedAudioFile(const juce::String& path)
{
    const auto ext = juce::File(path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3";
}

juce::Colour laneColour(TrackType lane)
{
    switch (lane)
    {
        case TrackType::Kick: return sketch::Theme::ochreDeep();
        case TrackType::Snare: return sketch::Theme::blue().darker(0.3f);
        default: return sketch::Theme::graphiteSoft();
    }
}
}

SampleBreakStripComponent::SampleBreakStripComponent()
{
    addAndMakeVisible(openButton);
    addAndMakeVisible(modeCombo);
    addAndMakeVisible(analyzeButton);
    addAndMakeVisible(clearButton);
    addAndMakeVisible(quantizeLabel);
    addAndMakeVisible(quantizeSlider);

    modeCombo.addItem("Copy break (K/S/H)", static_cast<int>(Mode::CopyBreak));
    modeCombo.addItem("Guide generation", static_cast<int>(Mode::Guide));
    modeCombo.setSelectedId(static_cast<int>(Mode::CopyBreak), juce::dontSendNotification);
    modeCombo.setTooltip("Copy break: transcribe Kick / Snare / HiHat with the original groove.\n"
                         "Guide generation: the sample steers the selected genre engine.");
    modeCombo.onChange = [this]
    {
        if (onModeChanged)
            onModeChanged(static_cast<Mode>(modeCombo.getSelectedId()));
    };

    quantizeLabel.setText("Quantize", juce::dontSendNotification);
    quantizeLabel.setJustificationType(juce::Justification::centredRight);
    quantizeLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    quantizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    quantizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
    quantizeSlider.setRange(0.0, 100.0, 1.0);
    quantizeSlider.setTextValueSuffix("%");
    quantizeSlider.setTooltip("0% keeps the break's original groove, 100% snaps every hit to the grid.");
    quantizeSlider.onDragEnd = [this]
    {
        if (onQuantizeChanged)
            onQuantizeChanged(static_cast<float>(quantizeSlider.getValue() / 100.0));
    };

    openButton.onClick = [this] { if (onChooseFile) onChooseFile(); };
    analyzeButton.onClick = [this] { if (onAnalyze) onAnalyze(); };
    clearButton.onClick = [this] { if (onClear) onClear(); };
}

void SampleBreakStripComponent::setState(const State& newState)
{
    state = newState;
    modeCombo.setSelectedId(static_cast<int>(state.mode), juce::dontSendNotification);
    if (!quantizeSlider.isMouseButtonDown())
        quantizeSlider.setValue(state.quantize * 100.0f, juce::dontSendNotification);
    analyzeButton.setButtonText(state.mode == Mode::CopyBreak ? "Analyze + Copy" : "Analyze");
    analyzeButton.setEnabled(state.file.existsAsFile());
    quantizeSlider.setEnabled(state.mode == Mode::CopyBreak);
    clearButton.setEnabled(state.analysisReady || state.file != juce::File());
    repaint();
}

juce::String SampleBreakStripComponent::resultText() const
{
    if (state.error.isNotEmpty())
        return state.error;
    if (!state.analysisReady || !state.analysis.valid)
        return state.file.existsAsFile() ? "Ready - press Analyze" : "Drop a drum break or sample";

    const auto& a = state.analysis;
    return juce::String(a.bpm, 1) + " BPM  |  " + juce::String(a.bars) + (a.bars == 1 ? " bar" : " bars")
        + "  |  swing " + juce::String(juce::roundToInt(a.swingPercent)) + "%\n"
        + "tempo " + juce::String(juce::roundToInt(a.tempoConfidence * 100.0f)) + "%  |  drum loop "
        + juce::String(juce::roundToInt(a.drumLoopConfidence * 100.0f)) + "%\n"
        + "K " + juce::String(a.countLane(TrackType::Kick))
        + "   S " + juce::String(a.countLane(TrackType::Snare))
        + "   H " + juce::String(a.countLane(TrackType::HiHat))
        + (state.harmonyText.isNotEmpty() ? "\n" + state.harmonyText : juce::String());
}

void SampleBreakStripComponent::paint(juce::Graphics& g)
{
    auto panel = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(sketch::Theme::paperLight().withAlpha(0.90f));
    g.fillRoundedRectangle(panel, 4.0f);
    sketch::drawFrame(g, panel, sketch::Theme::graphiteSoft(), 1.15f, 913, 4.0f);

    g.setColour(sketch::Theme::graphite());
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    g.drawText("SAMPLE ANALYSIS", juce::Rectangle<int>(10, 6, 130, 22), juce::Justification::centredLeft, true);

    // Drop zone with the file name.
    const auto drop = dropZoneBounds.toFloat();
    g.setColour(dropHighlight ? sketch::Theme::blueWash() : sketch::Theme::paper().withAlpha(0.8f));
    g.fillRoundedRectangle(drop, 4.0f);
    g.setColour(dropHighlight ? sketch::Theme::blue() : sketch::Theme::graphiteSoft().withAlpha(0.6f));
    const float dashes[] { 4.0f, 3.0f };
    juce::Path outline;
    outline.addRoundedRectangle(drop.reduced(0.5f), 4.0f);
    juce::Path dashed;
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
    g.fillPath(dashed);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.setColour(sketch::Theme::graphite());
    g.drawText(state.file.existsAsFile() ? state.file.getFileName() : "Drop audio here (wav / aif / flac / mp3)",
               dropZoneBounds.reduced(8, 0), juce::Justification::centredLeft, true);

    // Result.
    g.setColour(state.error.isNotEmpty() ? juce::Colours::darkred : sketch::Theme::graphite());
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawFittedText(resultText(), resultBounds, juce::Justification::centredLeft, 4);

    paintTimeline(g, timelineBounds);
}

void SampleBreakStripComponent::paintTimeline(juce::Graphics& g, juce::Rectangle<int> area) const
{
    if (area.isEmpty())
        return;

    g.setColour(sketch::Theme::paper().withAlpha(0.7f));
    g.fillRoundedRectangle(area.toFloat(), 3.0f);

    constexpr int kLabelWidth = 16;
    auto labels = area.removeFromLeft(kLabelWidth);
    const float rowHeight = static_cast<float>(area.getHeight()) / 3.0f;
    const std::array<TrackType, 3> lanes { TrackType::Kick, TrackType::Snare, TrackType::HiHat };
    const std::array<const char*, 3> names { "K", "S", "H" };

    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    for (int row = 0; row < 3; ++row)
    {
        g.setColour(laneColour(lanes[static_cast<size_t>(row)]));
        g.drawText(names[static_cast<size_t>(row)],
                   juce::Rectangle<float>(static_cast<float>(labels.getX()), area.getY() + row * rowHeight, static_cast<float>(kLabelWidth), rowHeight),
                   juce::Justification::centred);
    }

    const auto& a = state.analysis;
    if (!state.analysisReady || !a.valid || a.bars <= 0)
        return;

    const double totalTicks = static_cast<double>(a.bars * TimingGrid::TicksPerBar4_4);
    auto xForTick = [&](double tick)
    {
        return static_cast<float>(area.getX() + (tick / totalTicks) * area.getWidth());
    };

    for (int beat = 0; beat <= a.bars * 4; ++beat)
    {
        const bool barLine = beat % 4 == 0;
        g.setColour(sketch::Theme::graphiteSoft().withAlpha(barLine ? 0.45f : 0.15f));
        const float x = xForTick(beat * TimingGrid::TicksPerBeat);
        g.drawVerticalLine(juce::roundToInt(x), static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
    }

    for (const auto& hit : a.hits)
    {
        const int row = hit.lane == TrackType::Kick ? 0 : hit.lane == TrackType::Snare ? 1 : 2;
        const float x = xForTick(hit.gridTick + hit.timingOffsetTicks);
        const float y = area.getY() + (row + 0.5f) * rowHeight;
        const float radius = juce::jlimit(1.8f, 4.0f, rowHeight * 0.32f) * (0.6f + 0.4f * hit.velocity / 127.0f);
        const auto colour = laneColour(hit.lane).withAlpha(0.35f + 0.65f * hit.velocity / 127.0f);
        g.setColour(colour);
        if (hit.inferred)
            g.drawEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
        else
            g.fillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f);
    }
}

void SampleBreakStripComponent::resized()
{
    auto area = getLocalBounds().reduced(8, 6);
    auto top = area.removeFromTop(24);
    top.removeFromLeft(132);

    clearButton.setBounds(top.removeFromRight(56));
    top.removeFromRight(6);
    analyzeButton.setBounds(top.removeFromRight(110));
    top.removeFromRight(8);
    quantizeSlider.setBounds(top.removeFromRight(130));
    quantizeLabel.setBounds(top.removeFromRight(60));
    top.removeFromRight(8);
    modeCombo.setBounds(top.removeFromRight(160));
    top.removeFromRight(6);
    openButton.setBounds(top.removeFromRight(70));
    top.removeFromRight(6);
    dropZoneBounds = top;

    area.removeFromTop(6);
    resultBounds = area.removeFromLeft(250).withTrimmedLeft(2);
    area.removeFromLeft(8);
    timelineBounds = area;
}

bool SampleBreakStripComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
        if (isSupportedAudioFile(file))
            return true;
    return false;
}

void SampleBreakStripComponent::fileDragEnter(const juce::StringArray&, int, int)
{
    dropHighlight = true;
    repaint();
}

void SampleBreakStripComponent::fileDragExit(const juce::StringArray&)
{
    dropHighlight = false;
    repaint();
}

void SampleBreakStripComponent::filesDropped(const juce::StringArray& files, int, int)
{
    dropHighlight = false;
    repaint();
    for (const auto& path : files)
    {
        if (isSupportedAudioFile(path))
        {
            if (onFileDropped)
                onFileDropped(juce::File(path));
            return;
        }
    }
}
} // namespace bbg
