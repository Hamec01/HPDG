#include "SampleTrimEditorComponent.h"

#include <cmath>

#include "SketchTheme.h"

namespace bbg
{
namespace
{
juce::String formatTime(double seconds)
{
    const int minutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    return juce::String(minutes) + ":" + (rest < 10.0 ? "0" : "") + juce::String(rest, 2);
}
} // namespace

//==============================================================================
class SampleTrimEditorComponent::WaveView final : public juce::Component
{
public:
    explicit WaveView(SampleTrimEditorComponent& ownerToUse) : owner(ownerToUse) {}

    double viewStart = 0.0;
    double viewEnd = 1.0;

    double duration() const { return owner.source != nullptr ? owner.source->getDurationSeconds() : 1.0; }

    double xToTime(float x) const
    {
        return viewStart + (static_cast<double>(x) / juce::jmax(1, getWidth())) * (viewEnd - viewStart);
    }

    float timeToX(double t) const
    {
        return static_cast<float>((t - viewStart) / juce::jmax(1.0e-9, viewEnd - viewStart) * getWidth());
    }

    void setView(double start, double end)
    {
        const double total = duration();
        const double width = juce::jlimit(juce::jmin(0.05, total), total, end - start);
        start = juce::jlimit(0.0, juce::jmax(0.0, total - width), start);
        viewStart = start;
        viewEnd = start + width;
        owner.scrollBar.setRangeLimits(0.0, total, juce::dontSendNotification);
        owner.scrollBar.setCurrentRange(viewStart, width, juce::dontSendNotification);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(sketch::Theme::paperLight());
        g.fillRoundedRectangle(bounds, 3.0f);

        const float mid = bounds.getCentreY();
        const float halfHeight = bounds.getHeight() * 0.44f;

        // Beat / bar grid of the detected (or edited) tempo.
        const double beat = 60.0 / juce::jmax(20.0, owner.gridBpm);
        const float pixelsPerBeat = static_cast<float>(beat / juce::jmax(1.0e-9, viewEnd - viewStart) * getWidth());
        if (pixelsPerBeat >= 4.0f)
        {
            const double firstBeat = std::floor((viewStart - owner.gridAnchor) / beat);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            for (double k = firstBeat; owner.gridAnchor + k * beat <= viewEnd; k += 1.0)
            {
                const double t = owner.gridAnchor + k * beat;
                const auto beatIndex = static_cast<long long>(std::llround(k));
                const bool barLine = ((beatIndex % 4) + 4) % 4 == 0;
                const float x = timeToX(t);
                g.setColour(sketch::Theme::graphiteSoft().withAlpha(barLine ? 0.40f : 0.13f));
                g.drawVerticalLine(juce::roundToInt(x), bounds.getY(), bounds.getBottom());
                if (barLine && pixelsPerBeat * 4.0f >= 26.0f)
                {
                    g.setColour(sketch::Theme::graphiteSoft().withAlpha(0.7f));
                    g.drawText(juce::String(beatIndex / 4 + 1), juce::Rectangle<float>(x + 2.0f, bounds.getY() + 1.0f, 30.0f, 12.0f),
                               juce::Justification::centredLeft, false);
                }
            }
        }

        // Waveform (cached peaks).
        if (owner.source != nullptr && owner.source->isLoaded())
        {
            const int bins = juce::jmax(1, getWidth());
            owner.source->getPeaks(viewStart, viewEnd, bins, peaks);
            juce::Path wave;
            for (int x = 0; x < bins; ++x)
            {
                const auto& p = peaks[static_cast<size_t>(x)];
                const float top = mid - juce::jlimit(0.0f, 1.0f, p.second) * halfHeight;
                const float bottom = mid - juce::jlimit(-1.0f, 0.0f, p.first) * halfHeight;
                wave.addRectangle(static_cast<float>(x), top, 1.0f, juce::jmax(1.0f, bottom - top));
            }
            g.setColour(sketch::Theme::graphite().withAlpha(0.62f));
            g.fillPath(wave);

            // Hits (transients) as small ticks at the bottom.
            g.setColour(sketch::Theme::ochreDeep().withAlpha(0.55f));
            for (const auto t : owner.source->getTransients())
                if (t >= viewStart && t <= viewEnd)
                    g.drawVerticalLine(juce::roundToInt(timeToX(t)), bounds.getBottom() - 6.0f, bounds.getBottom());
        }

        // Selection: the outside is dimmed, the inside tinted, edges drawn as handles.
        const float sx = timeToX(owner.selection.getStart());
        const float ex = timeToX(owner.selection.getEnd());
        g.setColour(sketch::Theme::paperShadow().withAlpha(0.55f));
        if (sx > 0.0f)
            g.fillRect(0.0f, bounds.getY(), juce::jmin(sx, bounds.getWidth()), bounds.getHeight());
        if (ex < bounds.getWidth())
            g.fillRect(juce::jmax(0.0f, ex), bounds.getY(), bounds.getWidth() - juce::jmax(0.0f, ex), bounds.getHeight());
        g.setColour(sketch::Theme::blueWash().withAlpha(0.28f));
        g.fillRect(juce::Rectangle<float>(sx, bounds.getY(), ex - sx, bounds.getHeight()).getIntersection(bounds));
        g.setColour(sketch::Theme::ochreDeep());
        for (const float x : { sx, ex })
        {
            if (x < -4.0f || x > bounds.getWidth() + 4.0f)
                continue;
            g.fillRect(x - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
            g.fillRect(x - 4.0f, bounds.getY(), 8.0f, 6.0f);
            g.fillRect(x - 4.0f, bounds.getBottom() - 6.0f, 8.0f, 6.0f);
        }

        // Length hint right on the selection.
        const auto label = juce::String(owner.selectionBars(), 2) + " bars · " + juce::String(owner.selection.getLength(), 2) + " s";
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        const float labelX = juce::jlimit(2.0f, juce::jmax(2.0f, bounds.getWidth() - 150.0f), sx + 6.0f);
        g.setColour(sketch::Theme::paperLight().withAlpha(0.85f));
        g.fillRoundedRectangle(labelX - 3.0f, bounds.getY() + 14.0f, 146.0f, 17.0f, 3.0f);
        g.setColour(sketch::Theme::graphite());
        g.drawText(label, juce::Rectangle<float>(labelX, bounds.getY() + 14.0f, 140.0f, 17.0f), juce::Justification::centredLeft, false);

        // Playhead.
        if (owner.getPlayheadSeconds)
        {
            const double t = owner.getPlayheadSeconds();
            if (t >= viewStart && t <= viewEnd)
            {
                g.setColour(juce::Colours::darkred.withAlpha(0.85f));
                g.fillRect(timeToX(t) - 0.75f, bounds.getY(), 1.5f, bounds.getHeight());
            }
        }

        g.setColour(sketch::Theme::graphiteSoft().withAlpha(0.6f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto mode = hitTestMode(e.position.x);
        setMouseCursor(mode == Drag::Start || mode == Drag::End ? juce::MouseCursor::LeftRightResizeCursor
                       : mode == Drag::Move ? juce::MouseCursor::DraggingHandCursor
                                            : juce::MouseCursor::IBeamCursor);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        drag = hitTestMode(e.position.x);
        const double t = xToTime(e.position.x);
        dragOrigin = owner.snapTime(t);
        moveOffset = t - owner.selection.getStart();
        if (drag == Drag::New)
            owner.setSelection({ dragOrigin, dragOrigin });
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const double t = juce::jlimit(0.0, duration(), xToTime(e.position.x));
        auto sel = owner.selection;
        switch (drag)
        {
            case Drag::Start:
                sel.setStart(juce::jmin(owner.snapTime(t), sel.getEnd() - 0.02));
                break;
            case Drag::End:
                sel.setEnd(juce::jmax(owner.snapTime(t), sel.getStart() + 0.02));
                break;
            case Drag::Move:
            {
                const double length = sel.getLength();
                const double start = juce::jlimit(0.0, juce::jmax(0.0, duration() - length), owner.snapTime(t - moveOffset));
                sel = { start, start + length };
                break;
            }
            case Drag::New:
            {
                const double snapped = owner.snapTime(t);
                sel = { juce::jmin(dragOrigin, snapped), juce::jmax(dragOrigin, snapped) };
                break;
            }
        }
        owner.setSelection(sel);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        // A plain click (no drag) keeps the previous selection instead of leaving an empty one.
        if (drag == Drag::New && owner.selection.getLength() < 0.02)
            owner.setSelection(lastValidSelection);
        lastValidSelection = owner.selection;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        // Double click selects the bar under the cursor.
        const double bar = 240.0 / juce::jmax(20.0, owner.gridBpm);
        const double t = xToTime(e.position.x);
        const double start = owner.gridAnchor + std::floor((t - owner.gridAnchor) / bar) * bar;
        owner.setSelection({ juce::jmax(0.0, start), start + bar });
        lastValidSelection = owner.selection;
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const double width = viewEnd - viewStart;
        if (e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
        {
            const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
            setView(viewStart - delta * width * 0.5, viewEnd - delta * width * 0.5);
            return;
        }
        const double pivot = xToTime(e.position.x);
        const double factor = std::pow(0.8, wheel.deltaY * 4.0);
        const double newWidth = width * factor;
        const double ratio = (pivot - viewStart) / juce::jmax(1.0e-9, width);
        setView(pivot - ratio * newWidth, pivot - ratio * newWidth + newWidth);
    }

    juce::Range<double> lastValidSelection;

private:
    enum class Drag { New, Start, End, Move };

    Drag hitTestMode(float x) const
    {
        const float sx = timeToX(owner.selection.getStart());
        const float ex = timeToX(owner.selection.getEnd());
        if (std::abs(x - sx) <= 6.0f)
            return Drag::Start;
        if (std::abs(x - ex) <= 6.0f)
            return Drag::End;
        if (x > sx && x < ex)
            return Drag::Move;
        return Drag::New;
    }

    SampleTrimEditorComponent& owner;
    Drag drag = Drag::New;
    double dragOrigin = 0.0;
    double moveOffset = 0.0;
    std::vector<std::pair<float, float>> peaks;
};

//==============================================================================
SampleTrimEditorComponent::SampleTrimEditorComponent(std::shared_ptr<const SampleSourceAudio> sourceToUse,
                                                     juce::Range<double> initialSelection,
                                                     bool isGuideMode)
    : source(std::move(sourceToUse))
    , guideMode(isGuideMode)
{
    setWantsKeyboardFocus(true);
    waveView = std::make_unique<WaveView>(*this);
    addAndMakeVisible(*waveView);
    addAndMakeVisible(scrollBar);
    scrollBar.setAutoHide(false);
    struct ScrollListener final : juce::ScrollBar::Listener
    {
        explicit ScrollListener(WaveView& v) : view(v) {}
        void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override
        {
            const double width = view.viewEnd - view.viewStart;
            view.setView(newRangeStart, newRangeStart + width);
        }
        WaveView& view;
    };
    scrollListener = std::make_unique<ScrollListener>(*waveView);
    scrollBar.addListener(scrollListener.get());

    for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &bpmCaption, &bpmValue, &halfButton, &doubleButton, &anchorButton,
                                                             &snapCombo, &hitsToggle, &lengthCombo, &autoButton, &wholeButton,
                                                             &infoLabel, &hintLabel, &playButton, &analyzeButton, &cancelButton })
        addAndMakeVisible(c);

    titleLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    titleLabel.setText("Trim sample - " + (source != nullptr ? source->getFile().getFileName() : juce::String()) + "   ("
                           + formatTime(source != nullptr ? source->getDurationSeconds() : 0.0) + ")",
                       juce::dontSendNotification);

    if (source != nullptr && source->getEstimatedBpm() > 0.0)
    {
        gridBpm = source->getEstimatedBpm();
        gridAnchor = source->getDownbeatSeconds();
    }

    bpmCaption.setText("Grid BPM", juce::dontSendNotification);
    bpmCaption.setJustificationType(juce::Justification::centredRight);
    bpmValue.setEditable(true);
    bpmValue.setJustificationType(juce::Justification::centred);
    bpmValue.setColour(juce::Label::outlineColourId, sketch::Theme::graphiteSoft().withAlpha(0.5f));
    bpmValue.setTooltip("Detected tempo of the whole file. Type a value if the grid is off.");
    bpmValue.onTextChange = [this] { setGridBpm(bpmValue.getText().getDoubleValue()); };
    halfButton.onClick = [this] { setGridBpm(gridBpm * 0.5); };
    doubleButton.onClick = [this] { setGridBpm(gridBpm * 2.0); };
    anchorButton.setTooltip("Move the bar grid so that bar 1 starts at the selection start.");
    anchorButton.onClick = [this]
    {
        gridAnchor = selection.getStart();
        waveView->repaint();
        updateInfo();
    };

    snapCombo.addItem("Snap: off", static_cast<int>(Snap::Off));
    snapCombo.addItem("Snap: beat", static_cast<int>(Snap::Beat));
    snapCombo.addItem("Snap: bar", static_cast<int>(Snap::Bar));
    snapCombo.setSelectedId(static_cast<int>(snap), juce::dontSendNotification);
    snapCombo.onChange = [this] { snap = static_cast<Snap>(snapCombo.getSelectedId()); };
    hitsToggle.setToggleState(snapToHits, juce::dontSendNotification);
    hitsToggle.setTooltip("Edges stick to the nearest drum hit so a cut never lands in the middle of a hit.");
    hitsToggle.onClick = [this] { snapToHits = hitsToggle.getToggleState(); };

    for (const int bars : { 1, 2, 4, 8, 16 })
        lengthCombo.addItem(juce::String(bars) + (bars == 1 ? " bar" : " bars"), bars);
    lengthCombo.setTextWhenNothingSelected("Length...");
    lengthCombo.setTooltip("Set the selection to this many bars from its start.");
    lengthCombo.onChange = [this]
    {
        const int bars = lengthCombo.getSelectedId();
        if (bars <= 0)
            return;
        setSelection({ selection.getStart(), selection.getStart() + bars * 240.0 / gridBpm });
        lengthCombo.setSelectedId(0, juce::dontSendNotification);
    };
    autoButton.setTooltip("Find the most drum-like fragment (steady, dense hits) at the detected tempo.");
    autoButton.onClick = [this] { autoSelect(juce::roundToInt(juce::jmax(1.0, selectionBars()))); };
    wholeButton.onClick = [this] { selectWholeFile(); };

    infoLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    hintLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    hintLabel.setColour(juce::Label::textColourId, sketch::Theme::graphiteSoft());
    hintLabel.setText(guideMode
                          ? "Guide: start the selection on bar 1 - drums and bass are built from this fragment."
                          : "Copy break: select whole bars of the break. Wheel = zoom, Shift+wheel = scroll, double click = one bar.",
                      juce::dontSendNotification);

    playButton.onClick = [this] { togglePlayback(); };
    analyzeButton.onClick = [this] { finishAnalyze(); };
    cancelButton.onClick = [this]
    {
        if (onStopPlayback)
            onStopPlayback();
        if (onCancel)
            onCancel();
    };

    const double total = source != nullptr ? source->getDurationSeconds() : 1.0;
    waveView->setView(0.0, total);
    if (initialSelection.getLength() > 0.02)
        setSelection(initialSelection);
    else
        autoSelect(8);
    waveView->lastValidSelection = selection;
    // Start zoomed to a comfortable window around the selection when the file is long.
    if (total > selection.getLength() * 4.0)
    {
        const double pad = selection.getLength() * 1.5;
        waveView->setView(selection.getStart() - pad, selection.getEnd() + pad);
    }
    bpmValue.setText(juce::String(gridBpm, 2), juce::dontSendNotification);
    setSize(1000, 380);
    startTimerHz(30);
}

SampleTrimEditorComponent::~SampleTrimEditorComponent()
{
    scrollBar.removeListener(scrollListener.get());
    if (onStopPlayback)
        onStopPlayback();
}

void SampleTrimEditorComponent::setSelection(juce::Range<double> newSelection)
{
    const double total = source != nullptr ? source->getDurationSeconds() : 0.0;
    double start = juce::jlimit(0.0, total, newSelection.getStart());
    double end = juce::jlimit(start, total, newSelection.getEnd());
    if (end - start > kMaxSelectionSeconds)
        end = start + kMaxSelectionSeconds;
    selection = { start, end };
    if (isPlaying() && onPlaySelection && selection.getLength() > 0.02)
        onPlaySelection(selection); // keep the audition on the edited region
    updateInfo();
    if (waveView != nullptr)
        waveView->repaint();
}

void SampleTrimEditorComponent::setGridBpm(double bpm)
{
    if (bpm < 20.0 || bpm > 400.0)
    {
        bpmValue.setText(juce::String(gridBpm, 2), juce::dontSendNotification);
        return;
    }
    gridBpm = bpm;
    bpmValue.setText(juce::String(gridBpm, 2), juce::dontSendNotification);
    updateInfo();
    if (waveView != nullptr)
        waveView->repaint();
}

void SampleTrimEditorComponent::setSnap(Snap newSnap)
{
    snap = newSnap;
    snapCombo.setSelectedId(static_cast<int>(snap), juce::dontSendNotification);
}

void SampleTrimEditorComponent::setSnapToHits(bool enabled)
{
    snapToHits = enabled;
    hitsToggle.setToggleState(enabled, juce::dontSendNotification);
}

double SampleTrimEditorComponent::snapTime(double seconds) const
{
    double t = seconds;
    if (snap != Snap::Off)
    {
        t = SampleSourceAudio::snapToGrid(t, gridBpm, gridAnchor, snap == Snap::Bar ? 4 : 1);
        // A real hit a few ms off the grid line wins over the line itself.
        if (snapToHits && source != nullptr)
            t = source->snapToTransient(t, 0.03);
    }
    else if (snapToHits && source != nullptr)
    {
        t = source->snapToTransient(t, 0.08);
    }
    const double total = source != nullptr ? source->getDurationSeconds() : t;
    return juce::jlimit(0.0, total, t);
}

double SampleTrimEditorComponent::selectionBars() const
{
    return selection.getLength() * gridBpm / 240.0;
}

juce::String SampleTrimEditorComponent::selectionInfo() const
{
    return juce::String(selectionBars(), 2) + " bars · " + juce::String(selection.getLength(), 2) + " s · "
        + formatTime(selection.getStart()) + " - " + formatTime(selection.getEnd());
}

juce::String SampleTrimEditorComponent::qualityWarning() const
{
    const double bars = selectionBars();
    if (selection.getLength() < 0.5)
        return "Selection is too short to analyze.";
    if (bars < 1.9)
        return "Less than 2 bars: tempo and swing will be less accurate.";
    if (bars > 16.1)
        return "Only the first 16 bars are analyzed.";
    int hits = 0;
    if (source != nullptr)
        for (const auto t : source->getTransients())
            if (selection.contains(t))
                ++hits;
    if (source != nullptr && hits < juce::roundToInt(bars * 2.0))
        return "Few hits in this fragment: tempo and swing may be off.";
    return {};
}

void SampleTrimEditorComponent::autoSelect(int bars)
{
    if (source == nullptr)
        return;
    const auto region = source->suggestRegion(juce::jlimit(1, 16, bars));
    gridAnchor = region.getStart();
    setSelection(region);
    if (waveView != nullptr)
    {
        waveView->lastValidSelection = selection;
        const double visible = waveView->viewEnd - waveView->viewStart;
        if (!juce::Range<double>(waveView->viewStart, waveView->viewEnd).contains(selection))
            waveView->setView(selection.getStart() + selection.getLength() * 0.5 - visible * 0.5, selection.getStart() + selection.getLength() * 0.5 + visible * 0.5);
    }
}

void SampleTrimEditorComponent::selectWholeFile()
{
    if (source == nullptr)
        return;
    setSelection({ 0.0, source->getDurationSeconds() });
    if (waveView != nullptr)
    {
        waveView->lastValidSelection = selection;
        waveView->setView(0.0, source->getDurationSeconds());
    }
}

void SampleTrimEditorComponent::updateInfo()
{
    const auto warning = qualityWarning();
    infoLabel.setText(selectionInfo() + (warning.isNotEmpty() ? "   !  " + warning : juce::String()), juce::dontSendNotification);
    infoLabel.setColour(juce::Label::textColourId, warning.isNotEmpty() ? juce::Colours::darkred : sketch::Theme::graphite());
    analyzeButton.setEnabled(selection.getLength() >= 0.5);
}

bool SampleTrimEditorComponent::isPlaying() const
{
    return getPlayheadSeconds && getPlayheadSeconds() >= 0.0;
}

void SampleTrimEditorComponent::togglePlayback()
{
    if (isPlaying())
    {
        if (onStopPlayback)
            onStopPlayback();
    }
    else if (onPlaySelection && selection.getLength() > 0.02)
    {
        onPlaySelection(selection);
    }
}

void SampleTrimEditorComponent::finishAnalyze()
{
    if (selection.getLength() < 0.5)
        return;
    if (onStopPlayback)
        onStopPlayback();
    if (onAnalyzeSelection)
        onAnalyzeSelection(selection);
}

void SampleTrimEditorComponent::timerCallback()
{
    const bool playing = isPlaying();
    playButton.setButtonText(playing ? "Stop [Space]" : "Play [Space]");
    if (playing || wasPlaying)
        waveView->repaint();
    wasPlaying = playing;
}

bool SampleTrimEditorComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlayback();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        finishAnalyze();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        cancelButton.triggerClick();
        return true;
    }
    return false;
}

void SampleTrimEditorComponent::visibilityChanged()
{
    if (isShowing())
        grabKeyboardFocus();
}

void SampleTrimEditorComponent::paint(juce::Graphics& g)
{
    g.fillAll(sketch::Theme::paper());
}

void SampleTrimEditorComponent::resized()
{
    auto area = getLocalBounds().reduced(10);
    auto top = area.removeFromTop(26);
    titleLabel.setBounds(top.removeFromLeft(juce::jmax(200, top.getWidth() - 520)));
    wholeButton.setBounds(top.removeFromRight(80));
    top.removeFromRight(4);
    autoButton.setBounds(top.removeFromRight(90));
    top.removeFromRight(4);
    lengthCombo.setBounds(top.removeFromRight(90));
    top.removeFromRight(10);
    hitsToggle.setBounds(top.removeFromRight(110));
    snapCombo.setBounds(top.removeFromRight(100));

    area.removeFromTop(6);
    auto grid = area.removeFromTop(24);
    bpmCaption.setBounds(grid.removeFromLeft(70));
    grid.removeFromLeft(4);
    bpmValue.setBounds(grid.removeFromLeft(70));
    grid.removeFromLeft(4);
    halfButton.setBounds(grid.removeFromLeft(36));
    grid.removeFromLeft(2);
    doubleButton.setBounds(grid.removeFromLeft(36));
    grid.removeFromLeft(8);
    anchorButton.setBounds(grid.removeFromLeft(170));
    grid.removeFromLeft(10);
    hintLabel.setBounds(grid);

    auto bottom = area.removeFromBottom(28);
    cancelButton.setBounds(bottom.removeFromRight(80));
    bottom.removeFromRight(6);
    analyzeButton.setBounds(bottom.removeFromRight(140));
    bottom.removeFromRight(6);
    playButton.setBounds(bottom.removeFromRight(100));
    bottom.removeFromRight(10);
    infoLabel.setBounds(bottom);

    area.removeFromBottom(6);
    scrollBar.setBounds(area.removeFromBottom(12));
    area.removeFromBottom(2);
    area.removeFromTop(6);
    waveView->setBounds(area);
    waveView->setView(waveView->viewStart, waveView->viewEnd);
}
} // namespace bbg
