#include "TrackListComponent.h"

#include "../Core/TrackRegistry.h"

namespace bbg
{
TrackListComponent::TrackListComponent() = default;

void TrackListComponent::setRowHeight(int newHeight)
{
    rowHeight = juce::jlimit(22, 80, newHeight);
    resized();
    repaint();
}

int TrackListComponent::getContentHeight() const
{
    return rulerHeight + rowHeight * static_cast<int>(rows.size());
}

void TrackListComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(17, 19, 23));

    g.setColour(juce::Colour::fromRGB(30, 34, 40));
    g.fillRect(0, 0, getWidth(), rulerHeight);
    g.setColour(juce::Colour::fromRGB(210, 216, 224));
    g.setFont(juce::Font(12.5f, juce::Font::bold));
    g.drawText("INSTRUMENT RACK", 10, 0, getWidth() - 12, rulerHeight, juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 20));
    g.drawLine(0.0f, static_cast<float>(rulerHeight), static_cast<float>(getWidth()), static_cast<float>(rulerHeight));
}

void TrackListComponent::setTracks(const std::vector<TrackState>& tracks, int bassKeyRootChoice, int bassScaleModeChoice)
{
    std::vector<const TrackState*> visibleTracks;
    visibleTracks.reserve(tracks.size());

    for (const auto& track : tracks)
    {
        const auto* info = TrackRegistry::find(track.type);
        if (info != nullptr && info->visibleInUI)
            visibleTracks.push_back(&track);
    }

    if (rows.size() == visibleTracks.size() && !rows.empty())
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            rows[i]->syncFromState(*visibleTracks[i]);
            rows[i]->setBassControls(bassKeyRootChoice, bassScaleModeChoice);
        }

        repaint();
        return;
    }

    rows.clear();

    for (const auto* track : visibleTracks)
    {
        const auto* info = TrackRegistry::find(track->type);
        if (info == nullptr)
            continue;

        auto row = std::make_unique<TrackRowComponent>(*info);
        row->syncFromState(*track);
        row->onRegenerate = [this](TrackType type)
        {
            if (onRegenerateTrack)
                onRegenerateTrack(type);
        };
        row->onSoloChanged = [this](TrackType type, bool value)
        {
            if (onSoloTrack)
                onSoloTrack(type, value);
        };
        row->onMuteChanged = [this](TrackType type, bool value)
        {
            if (onMuteTrack)
                onMuteTrack(type, value);
        };
        row->onClear = [this](TrackType type)
        {
            if (onClearTrack)
                onClearTrack(type);
        };
        row->onLockChanged = [this](TrackType type, bool value)
        {
            if (onLockTrack)
                onLockTrack(type, value);
        };
        row->onEnableChanged = [this](TrackType type, bool value)
        {
            if (onEnableTrack)
                onEnableTrack(type, value);
        };
        row->onDrag = [this](TrackType type)
        {
            if (onDragTrack)
                onDragTrack(type);
        };
        row->onDragGesture = [this](TrackType type)
        {
            if (onDragTrackGesture)
                onDragTrackGesture(type);
        };
        row->onExport = [this](TrackType type)
        {
            if (onExportTrack)
                onExportTrack(type);
        };
        row->onPrevSample = [this](TrackType type)
        {
            if (onPrevSampleTrack)
                onPrevSampleTrack(type);
        };
        row->onNextSample = [this](TrackType type)
        {
            if (onNextSampleTrack)
                onNextSampleTrack(type);
        };
        row->onLaneVolumeChanged = [this](TrackType type, float volume)
        {
            if (onLaneVolumeTrack)
                onLaneVolumeTrack(type, volume);
        };
        row->onBassKeyChanged = [this](int choice)
        {
            if (onBassKeyChanged)
                onBassKeyChanged(choice);
        };
        row->onBassScaleChanged = [this](int choice)
        {
            if (onBassScaleChanged)
                onBassScaleChanged(choice);
        };
        row->setBassControls(bassKeyRootChoice, bassScaleModeChoice);

        addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }

    resized();
    repaint();
}

void TrackListComponent::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(rulerHeight);

    for (auto& row : rows)
        row->setBounds(area.removeFromTop(rowHeight));
}
} // namespace bbg
