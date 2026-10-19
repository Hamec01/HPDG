#pragma once

#include <functional>
#include <mutex>
#include <vector>

#include <juce_core/juce_core.h>

namespace bbg::update
{
// "0.1.0-beta.2": number plus an optional label. A final release (no label) is newer than
// any of its own betas.
struct ReleaseVersion
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    juce::String label;   // "beta", "rc"; empty for a final release
    int labelNumber = 0;  // the 2 of "beta.2"
    bool valid = false;

    static ReleaseVersion parse(const juce::String& tag);
    juce::String toDisplayString() const; // "0.1.0 Beta 2"
};

// < 0 when a is older than b, 0 when equal, > 0 when a is newer.
int compare(const ReleaseVersion& a, const ReleaseVersion& b);

// The version this build was made from (HPDG_VERSION_TAG from CMakeLists.txt).
ReleaseVersion currentVersion();
juce::String currentDisplayVersion();

struct LatestRelease
{
    ReleaseVersion version;
    juce::String title;
    juce::String notes;
    juce::URL pageUrl;
};

// latest.json published on hamloprod.org:
//   { "version": "0.1.0-beta.3", "title": "HPDG 0.1.0 Beta 3", "notes": "...", "url": "https://hamloprod.org/en/vst" }
// Returns false for anything unexpected. The page link must stay on https://hamloprod.org.
bool parseManifest(const juce::String& json, LatestRelease& out);

// Downloads latest.json in the background, at most once per plugin process. Hold it in a
// juce::SharedResourcePointer: the last owner going away stops a request still in flight.
class UpdateChecker : private juce::Thread
{
public:
    using Callback = std::function<void(const LatestRelease&)>;

    UpdateChecker();
    ~UpdateChecker() override;

    // The callback runs on the message thread and only when a release newer than this build is
    // published. Nothing happens when the request fails (no internet, site down).
    void whenNewerReleaseAvailable(Callback callback);

private:
    void run() override;

    std::vector<Callback> waiting; // guarded by the shared result's mutex
};
} // namespace bbg::update
