#pragma once

#include <functional>
#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>

#include "../Bass808/BassSample.h"
#include "../Bass808/BassSettings.h"
#include "../Core/Sub808Types.h"

namespace bbg
{
class Bass808Processor;
class Bass808Editor;

// The full HPDG 808 editor over HPDG's window, for the Sub808 lane. HPDG stays covered (no new pattern
// while it is open) but keeps playing, so the bass is shaped against the beat. The editor works on its own
// HPDG 808 instance; every change goes back to the lane as an HPDG 808 sound, and PLAY sounds a note
// through HPDG's own engine.
class Sub808EditorOverlay final : public juce::Component,
                                  private juce::Timer,
                                  private juce::MidiKeyboardState::Listener
{
public:
    struct Host
    {
        std::function<Sub808SoundState()> getSound;
        std::function<void(const Sub808SoundState&)> setSound;
        // the lane's bank sample, as the lane plays it (nullptr: none) and its name
        std::function<std::shared_ptr<const BassSample>()> getBankSample;
        std::function<juce::String()> getBankSampleName;
        std::function<void(int note, int velocity)> audition;
        std::function<void()> close; // the host deletes the overlay (asynchronously)
    };

    explicit Sub808EditorOverlay(Host host);
    ~Sub808EditorOverlay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void loadFromLane();
    void timerCallback() override;
    void handleNoteOn(juce::MidiKeyboardState*, int, int note, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int, int, float) override {}
    juce::Rectangle<int> frameBounds() const;

    Host host;
    std::unique_ptr<Bass808Processor> bass;
    std::unique_ptr<Bass808Editor> editor;
    juce::TextButton laneSampleButton { "LANE SAMPLE" }, closeButton { "CLOSE" };
    juce::Label title;
    Sub808SoundState state;
    BassSettings shownSettings;
    std::shared_ptr<const BassSample> shownSample;
    juce::String shownName;
    float editorScale = 1.0f;
};
} // namespace bbg
