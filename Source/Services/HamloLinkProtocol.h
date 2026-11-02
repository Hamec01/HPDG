#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

// HPDG Link (HamloRevolution docs/HPDG_LINK.md): HPDG publishes its tempo, genre, groove and pattern into a small
// fixed-size shared-memory slot; the Hamlo plug-ins read it. The same file is in HPDG (Source/Services) and in
// HamloRevolution (Source/Link): change both together and bump the version (a new major = an incompatible layout).
//
// Transport: %LOCALAPPDATA%\HamloProd\Link\hpdg-<instanceId>.slot, one file per HPDG instance, mapped by both
// sides (works when the host runs the plug-ins in different processes). Consistency: a seqlock - the writer makes
// `sequence` odd, writes, makes it even; a reader copies and retries until it saw the same even number before and
// after. No locks, no waiting; nothing here is called on an audio thread.
namespace hamlo::link
{
constexpr std::uint16_t kVersionMajor = 1;
constexpr std::uint16_t kVersionMinor = 0;
constexpr int kMaxEvents = 1024;
constexpr int kMaxRoles = 32; // role 0 = none
constexpr int kRoleChars = 24;
constexpr int kNameChars = 64;
constexpr int kPpq = 960;                 // ticks per quarter note (HPDG's TimingGrid)
constexpr juce::int64 kAliveMs = 2000;    // no heartbeat for this long = the publisher is gone
constexpr juce::int64 kHeartbeatMs = 1000;

enum EventFlags : std::uint8_t
{
    Ghost = 1,
    Slide = 2,
    Fill = 4,
    Accent = 8
};

// HPDG's lanes (TrackType order): 0 hi-hat, 1 open hat, 2 snare, 3 clap / ghost snare, 4 kick, 5 ghost kick, 6 ride,
// 7 cymbal, 8 perc, 9 hat FX, 10 808.
enum Lane : std::uint8_t
{
    HiHat, OpenHat, Snare, Clap, Kick, GhostKick, Ride, Cymbal, Perc, HatFx, Sub808, kLanes
};

// HPDG's GenreType order.
enum Genre : std::int32_t
{
    BoomBap, Rap, Trap, Drill, DnB, Techno, Atmospheric
};

#pragma pack(push, 1)
struct Event
{
    std::uint8_t lane = 0;
    std::uint8_t flags = 0;
    std::uint8_t velocity = 0;  // 1 .. 127
    std::uint8_t roleId = 0;    // index into roleNames
    std::int32_t tick = 0;      // 0 .. bars x 4 x kPpq, the timing offset (swing, humanize) applied
    std::int32_t lengthTicks = 0;
};

struct Snapshot
{
    char magic[8];                // "HAMLOLNK"
    std::uint16_t versionMajor;
    std::uint16_t versionMinor;
    std::uint32_t sequence;       // the seqlock counter (odd while writing)
    std::int64_t heartbeatMs;     // juce::Time::currentTimeMillis() of the last write; 0 = closed
    std::uint64_t instanceId;
    char instanceName[kNameChars]; // UTF-8
    float bpm;
    std::int32_t genre;
    std::int32_t substyle;        // the index within that genre's substyles
    float swingPercent;           // 50 .. 75
    float timing, humanize, density, velocityAmount; // 0 .. 1
    std::int32_t intensity;       // 1 .. 5
    std::int32_t keyRoot;         // 0 = C .. 11 = B
    std::int32_t scaleMode;       // 0 minor, 1 major, 2 harmonic minor
    std::int32_t bars;
    std::int32_t phraseLengthBars;
    std::int32_t seed;
    std::int32_t generationCounter;
    std::int32_t mutationCounter;
    std::int32_t eventCount;      // <= kMaxEvents
    Event events[kMaxEvents];
    char roleNames[kMaxRoles][kRoleChars];
};
#pragma pack(pop)

static_assert(sizeof(Event) == 12, "the protocol layout");
static_assert(offsetof(Snapshot, sequence) % 4 == 0, "the seqlock counter must be aligned");

// Sets magic and version, clears the rest (the publisher's starting point; tests).
void initialise(Snapshot&) noexcept;

// The payload is everything after the header (magic .. heartbeat .. instance id): equal payloads = nothing new.
bool samePayload(const Snapshot& a, const Snapshot& b) noexcept;
juce::String roleName(const Snapshot&, int roleId);

enum class Status
{
    Ok,
    Missing,     // no slot / unreadable / not a link slot
    Busy,        // the writer kept writing while we read (retry later)
    NewerMajor,  // published by a newer HPDG: refused (the plug-in shows "update")
    Stale        // no heartbeat for kAliveMs (or closed)
};
Status check(const Snapshot&, juce::int64 nowMs) noexcept;

juce::File linkDirectory();
juce::File slotFile(std::uint64_t instanceId);

// One mapped slot. The publisher opens it for writing (creating the file); readers open an existing one.
class SharedSlot
{
public:
    static std::unique_ptr<SharedSlot> openForWriting(const juce::File&);
    static std::unique_ptr<SharedSlot> openForReading(const juce::File&);
    ~SharedSlot();

    // Writer: the whole snapshot (its header's sequence is taken from the slot), heartbeat = now.
    void write(const Snapshot&, juce::int64 nowMs) noexcept;
    // Writer: only the heartbeat (a seqlock write of the header field).
    void beat(juce::int64 nowMs) noexcept;
    // Reader: a consistent copy, or Busy / Missing.
    Status read(Snapshot&) const noexcept;

    struct Native; // the platform mapping (opaque)

private:
    SharedSlot() = default;
    std::unique_ptr<Native> native;
    Snapshot* shared = nullptr;
    bool writable = false;
};

struct Listing
{
    std::uint64_t instanceId = 0;
    juce::String name;
    std::int32_t genre = 0;
    float bpm = 0.0f;
    Status status = Status::Missing;
    juce::File file;
};
// Every slot in linkDirectory() with its status (alive ones first). Off the audio thread.
std::vector<Listing> listInstances(juce::int64 nowMs);

// Publisher housekeeping: slots whose heartbeat is older than an hour (a crashed HPDG) are deleted.
void removeDeadSlots(juce::int64 nowMs);
} // namespace hamlo::link
