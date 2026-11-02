#pragma once

#include <functional>
#include <memory>

#include <juce_events/juce_events.h>

#include "../Core/PatternProject.h"
#include "HamloLinkProtocol.h"

namespace bbg
{
// HPDG Link, the HPDG side (HamloRevolution docs/HPDG_LINK.md): publishes the tempo, genre, groove and the pattern
// for the Hamlo plug-ins (CONNECT TO HPDG). Additive: it only reads the project. Message thread only - a 4 Hz timer
// that rebuilds the snapshot when the processor's revision changed (a pattern rebuild or a parameter move) and
// otherwise writes a heartbeat once a second. Never on the audio thread; the project lock is taken only to read a
// changed pattern.
class HamloLinkPublisher final : private juce::Timer
{
public:
    struct Hooks
    {
        std::function<void(hamlo::link::Snapshot&)> fill;    // the payload from the project (takes the lock itself)
        std::function<std::uint32_t()> revision;             // lock-free: changes when the pattern or a parameter did
        std::function<std::uint64_t()> getInstanceId;       // 0 = none yet
        std::function<void(std::uint64_t)> setInstanceId;    // kept in the plug-in state
    };

    explicit HamloLinkPublisher(Hooks);
    ~HamloLinkPublisher() override; // the slot is closed (heartbeat 0) and deleted

    void tick(juce::int64 nowMs); // the timer's work; public for tests
    juce::File getSlotFile() const;
    std::uint64_t getInstanceId() const noexcept { return currentId; }

private:
    void timerCallback() override;
    void close();

    Hooks hooks;
    std::unique_ptr<hamlo::link::SharedSlot> slot;
    std::unique_ptr<hamlo::link::Snapshot> scratch, last;
    std::uint64_t currentId = 0;
    std::uint32_t lastRevision = 0;
    juce::int64 lastWriteMs = 0;
    bool written = false;
};

// The payload from a project (instance id and name are the publisher's): tempo, genre and its substyle, the groove
// parameters, key, bars, counters; every note of the enabled, audible lanes (solo respected) at its played tick
// (grid + timing offset), with its velocity, ghost / slide / fill / accent flags and its semantic role.
void fillLinkSnapshot(const PatternProject&, hamlo::link::Snapshot&);
} // namespace bbg
