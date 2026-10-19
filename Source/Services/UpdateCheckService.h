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

// The release list from the GitHub API (github.com/Hamec01/HPDG_beta-releases: public, releases only): the newest non-draft release
// by its tag ("v0.1.0-beta.3"; betas are published as pre-releases and count). Title and notes come
// from the release; the page is always the HPDG page on hamloprod.org - the installer goes to buyers
// only, so a release on the public repository never carries it and is never linked from the plugin.
// Returns false for anything unexpected or an empty list.
bool parseGitHubReleases(const juce::String& json, LatestRelease& out);

// Asks GitHub for the releases in the background, at most once per plugin process. Hold it in a
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
