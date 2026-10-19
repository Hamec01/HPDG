#include "UpdateCheckService.h"

#include <juce_events/juce_events.h>

#ifndef HPDG_VERSION_TAG
 #error "HPDG_VERSION_TAG comes from CMakeLists.txt"
#endif

namespace bbg::update
{
namespace
{
constexpr auto kReleasesUrl = "https://api.github.com/repos/Hamec01/HPDG_beta-releases/releases?per_page=20";
constexpr auto kReleasePageUrl = "https://hamloprod.org/en/vst";
constexpr int kTimeoutMs = 5000;
constexpr int kMaxResponseBytes = 512 * 1024;
constexpr int kMaxNotesChars = 600; // the button tooltip

// Result of the one request per process. Plain data, so it outlives every checker safely.
// Its mutex also guards each checker's waiting list.
struct CheckResult
{
    std::mutex mutex;
    bool done = false;
    bool hasNewer = false;
    LatestRelease newer;
};

CheckResult& checkResult()
{
    static CheckResult result;
    return result;
}

bool parseNumber(const juce::String& text, int& out)
{
    if (text.isEmpty() || !text.containsOnly("0123456789") || text.length() > 6)
        return false;
    out = text.getIntValue();
    return true;
}
} // namespace

ReleaseVersion ReleaseVersion::parse(const juce::String& tag)
{
    ReleaseVersion version;
    auto text = tag.trim();
    if (text.startsWithIgnoreCase("v"))
        text = text.substring(1);

    const auto numberPart = text.upToFirstOccurrenceOf("-", false, false);
    const auto labelPart = text.fromFirstOccurrenceOf("-", false, false).toLowerCase();

    juce::StringArray numbers;
    numbers.addTokens(numberPart, ".", {});
    if (numbers.isEmpty() || numbers.size() > 3 || (text.contains("-") && labelPart.isEmpty()))
        return {};

    int* const fields[] = { &version.major, &version.minor, &version.patch };
    for (int i = 0; i < numbers.size(); ++i)
        if (!parseNumber(numbers[i], *fields[i]))
            return {};

    if (labelPart.isNotEmpty())
    {
        // "beta.2" and "beta2" are both accepted.
        version.label = labelPart.initialSectionNotContaining("0123456789.");
        const auto digits = labelPart.substring(version.label.length()).trimCharactersAtStart(".");
        if (version.label.isEmpty() || !version.label.containsOnly("abcdefghijklmnopqrstuvwxyz"))
            return {};
        if (digits.isNotEmpty() && !parseNumber(digits, version.labelNumber))
            return {};
    }

    version.valid = true;
    return version;
}

juce::String ReleaseVersion::toDisplayString() const
{
    auto text = juce::String(major) + "." + juce::String(minor) + "." + juce::String(patch);
    if (label.isNotEmpty())
    {
        text << " " << label.substring(0, 1).toUpperCase() << label.substring(1);
        if (labelNumber > 0)
            text << " " << labelNumber;
    }
    return text;
}

int compare(const ReleaseVersion& a, const ReleaseVersion& b)
{
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;

    if (a.label.isEmpty() != b.label.isEmpty())
        return a.label.isEmpty() ? 1 : -1; // the final release beats its betas
    if (a.label != b.label)
        return a.label.compare(b.label) < 0 ? -1 : 1; // alpha < beta < rc
    if (a.labelNumber != b.labelNumber)
        return a.labelNumber < b.labelNumber ? -1 : 1;
    return 0;
}

ReleaseVersion currentVersion()
{
    return ReleaseVersion::parse(HPDG_VERSION_TAG);
}

juce::String currentDisplayVersion()
{
    return currentVersion().toDisplayString();
}

bool parseGitHubReleases(const juce::String& json, LatestRelease& out)
{
    const auto root = juce::JSON::parse(json);
    const auto* releases = root.getArray();
    if (releases == nullptr)
        return false;

    const juce::var* newest = nullptr;
    ReleaseVersion newestVersion;
    for (const auto& release : *releases)
    {
        if (!release.isObject() || static_cast<bool>(release.getProperty("draft", false)))
            continue;
        const auto version = ReleaseVersion::parse(release.getProperty("tag_name", {}).toString());
        if (version.valid && (newest == nullptr || compare(version, newestVersion) > 0))
        {
            newest = &release;
            newestVersion = version;
        }
    }
    if (newest == nullptr)
        return false;

    out.version = newestVersion;
    out.title = newest->getProperty("name", {}).toString().trim();
    if (out.title.isEmpty())
        out.title = "HPDG " + newestVersion.toDisplayString();
    auto notes = newest->getProperty("body", {}).toString().replace("\r\n", "\n").trim();
    if (notes.length() > kMaxNotesChars)
        notes = notes.substring(0, kMaxNotesChars).trimEnd() + "...";
    out.notes = notes;
    out.pageUrl = juce::URL(kReleasePageUrl);
    return true;
}

UpdateChecker::UpdateChecker()
    : juce::Thread("HPDG update check")
{
}

UpdateChecker::~UpdateChecker()
{
    stopThread(kTimeoutMs + 1000);
}

void UpdateChecker::whenNewerReleaseAvailable(Callback callback)
{
    if (callback == nullptr)
        return;

    auto& result = checkResult();
    const std::lock_guard<std::mutex> lock(result.mutex);
    if (result.done)
    {
        if (result.hasNewer)
            juce::MessageManager::callAsync([callback, release = result.newer] { callback(release); });
        return;
    }

    waiting.push_back(std::move(callback));
    if (!isThreadRunning())
        startThread(juce::Thread::Priority::low);
}

void UpdateChecker::run()
{
    LatestRelease release;
    bool hasNewer = false;

    int statusCode = 0;
    const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                             .withConnectionTimeoutMs(kTimeoutMs)
                             .withStatusCode(&statusCode)
                             .withExtraHeaders("User-Agent: HPDG/" HPDG_VERSION_TAG "\r\n"
                                               "Accept: application/vnd.github+json");
    if (auto stream = juce::URL(kReleasesUrl).createInputStream(options))
    {
        if (statusCode == 200 && !threadShouldExit())
        {
            juce::MemoryBlock data;
            stream->readIntoMemoryBlock(data, kMaxResponseBytes);
            hasNewer = parseGitHubReleases(data.toString(), release)
                       && compare(release.version, currentVersion()) > 0;
        }
    }

    if (threadShouldExit())
        return; // stopped mid-request: leave the result open for the next owner

    std::vector<Callback> callbacks;
    {
        auto& result = checkResult();
        const std::lock_guard<std::mutex> lock(result.mutex);
        result.done = true;
        result.hasNewer = hasNewer;
        result.newer = release;
        callbacks.swap(waiting);
    }

    if (hasNewer)
        for (auto& callback : callbacks)
            juce::MessageManager::callAsync([callback, release] { callback(release); });
}
} // namespace bbg::update
