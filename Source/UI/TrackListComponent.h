#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "TrackRowComponent.h"

namespace bbg
{
class TrackListComponent : public juce::Component
{
public:
    TrackListComponent();
    void paint(juce::Graphics& g) override;

    void resized() override;

    void setTracks(const std::vector<TrackState>& tracks, int bassKeyRootChoice, int bassScaleModeChoice);
    void setRowHeight(int newHeight);
    int getRowHeight() const { return rowHeight; }
    int getVisibleRowCount() const { return static_cast<int>(rows.size()); }
    int getContentHeight() const;

    std::function<void(TrackType)> onRegenerateTrack;
    std::function<void(TrackType, bool)> onSoloTrack;
    std::function<void(TrackType, bool)> onMuteTrack;
    std::function<void(TrackType)> onClearTrack;
    std::function<void(TrackType, bool)> onLockTrack;
    std::function<void(TrackType, bool)> onEnableTrack;
    std::function<void(TrackType)> onDragTrack;
    std::function<void(TrackType)> onDragTrackGesture;
    std::function<void(TrackType)> onExportTrack;
    std::function<void(TrackType)> onPrevSampleTrack;
    std::function<void(TrackType)> onNextSampleTrack;
    std::function<void(TrackType, float)> onLaneVolumeTrack;
    std::function<void(int)> onBassKeyChanged;
    std::function<void(int)> onBassScaleChanged;

private:
    std::vector<std::unique_ptr<TrackRowComponent>> rows;
    int rowHeight = 30;
    int rulerHeight = 24;
};
} // namespace bbg
