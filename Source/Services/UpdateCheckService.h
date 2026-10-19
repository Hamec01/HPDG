#pragma once

#include <atomic>
#include <mutex>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

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
    juce::String toTag() const;           // "0.1.0-beta.2"
};

// < 0 when a is older than b, 0 when equal, > 0 when a is newer.
int compare(const ReleaseVersion& a, const ReleaseVersion& b);

// The version this build was made from (HPDG_VERSION_TAG from CMakeLists.txt).
ReleaseVersion currentVersion();
juce::String currentDisplayVersion();

// hamloprod.org is the only source of updates (docs/UPDATES_TASK.md). The installer goes to
// buyers only: /latest is public and carries no file, /download hands out a short-lived link
// for a valid license key. No secret lives in the plugin.
//   GET  <api>/latest   -> { "version", "title", "notes", "page" }
//   POST <api>/download { "licenseKey", "currentVersion", "platform" }
//                       -> 200 { "url", "sha256", "version", "size" } | 401/403 | 404 | 429
// <api> is https://hamloprod.org/api/hpdg. HPDG_UPDATE_API in the environment replaces it for a
// local test server, and only with http://localhost or http://127.0.0.1.
juce::String apiBaseUrl();
bool isLocalApiOverride(const juce::String& base);

struct LatestRelease
{
    ReleaseVersion version;
    juce::String title;
    juce::String notes;
    juce::URL pageUrl; // a hamloprod.org page; the HPDG page when the answer has none or another host
};

// The /latest answer. Returns false for anything unexpected.
bool parseLatestResponse(const juce::String& json, LatestRelease& out);

struct DownloadTicket
{
    juce::String url;
    juce::String sha256; // lowercase hex
    ReleaseVersion version;
    juce::int64 size = 0; // bytes; 0 when the server does not say
};

enum class DownloadRefusal
{
    None,       // the ticket is filled
    InvalidKey, // 401 / 403: no purchase behind this key
    Limit,      // 429: too many downloads today
    NoRelease,  // 404: nothing to download
    Untrusted,  // the link points outside the storage allow-list
    Malformed   // anything else
};

// The /download answer. The link must pass isAllowedDownloadUrl and the hash must be 64 hex digits.
DownloadRefusal parseDownloadResponse(int statusCode, const juce::String& json, DownloadTicket& out,
                                      bool allowLocalhost = false);

// https only, and only hamloprod.org or the site's private storage
// (usc1.contabostorage.com/hamloprod-private/hpdg/<file>). allowLocalhost adds
// http://localhost and http://127.0.0.1 (the local test server of HPDG_UPDATE_API).
bool isAllowedDownloadUrl(const juce::String& url, bool allowLocalhost = false);
bool isAllowedPageUrl(const juce::String& url);

// "hpdg 7k3m-2q9x-..." -> "HPDG-7K3M-2Q9X-...". Crockford base32 groups (O reads as 0, I and L
// as 1). Empty when the text is not a key.
juce::String normalizeLicenseKey(const juce::String& text);

// Lowercase hex SHA-256 of the file; empty when it cannot be read.
juce::String sha256OfFile(const juce::File& file);
bool sha256Equals(const juce::String& a, const juce::String& b);

// The license key, kept in the user's settings (%APPDATA%\HamloProd\HPDG\license.json), never
// in a DAW project.
class LicenseStore
{
public:
    explicit LicenseStore(juce::File directory = defaultDirectory());

    static juce::File defaultDirectory();

    juce::String load() const; // empty when none is stored
    bool save(const juce::String& key);
    void clear();

private:
    juce::File file;
};

// The update button's state. One per plugin process: hold it in a juce::SharedResourcePointer.
// Every change is broadcast on the message thread; the work runs on a background thread and a
// failing network never blocks the UI.
class UpdateService : public juce::ChangeBroadcaster,
                      private juce::Thread
{
public:
    enum class State
    {
        Idle,            // nothing asked yet
        Checking,
        UpToDate,
        UpdateAvailable,
        Offline,         // the site did not answer
        Downloading,
        ReadyToInstall,  // downloaded and the SHA-256 matched
        Failed           // see Snapshot::failure; the newer release is still known
    };

    enum class Failure
    {
        None,
        InvalidKey,
        Limit,
        NoRelease,
        Untrusted,
        Network,
        Checksum,
        Disk
    };

    struct Snapshot
    {
        State state = State::Idle;
        Failure failure = Failure::None;
        LatestRelease latest;   // valid from UpdateAvailable on
        float progress = 0.0f;  // 0..1 while downloading (0 when the size is unknown)
        juce::File installer;   // ReadyToInstall
        int finishedDownloads = 0; // grows when a download ends (ready or failed)
    };

    UpdateService();
    ~UpdateService() override;

    Snapshot snapshot() const;

    // The one automatic check per plugin process (the first editor opening).
    void checkOnce();
    void checkNow();
    void download(const juce::String& licenseKey);

    // Starts the verified installer (message thread). The installer itself asks to close the DAW.
    bool launchInstaller();

private:
    enum class Job { None, Check, Download };

    void run() override;
    void runCheck();
    void runDownload(const juce::String& licenseKey);
    void start(Job job, const juce::String& licenseKey = {});
    void publish(const Snapshot& next);

    mutable std::mutex mutex;
    Snapshot current;
    Job pendingJob = Job::None;
    juce::String pendingKey;
};
} // namespace bbg::update
