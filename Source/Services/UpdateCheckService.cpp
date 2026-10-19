#include "UpdateCheckService.h"

#include <juce_cryptography/juce_cryptography.h>

#ifndef HPDG_VERSION_TAG
 #error "HPDG_VERSION_TAG comes from CMakeLists.txt"
#endif

namespace bbg::update
{
namespace
{
constexpr auto kApiBaseUrl = "https://hamloprod.org/api/hpdg";
constexpr auto kReleasePageUrl = "https://hamloprod.org/en/vst";
constexpr auto kPlatform = "windows-x64";
constexpr int kTimeoutMs = 5000;
constexpr int kMaxResponseBytes = 64 * 1024;
constexpr int kMaxNotesChars = 600; // the button tooltip
constexpr juce::int64 kMaxInstallerBytes = juce::int64(1024) * 1024 * 1024;
constexpr auto kKeyAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"; // Crockford base32
constexpr int kKeyGroups = 4;
constexpr int kKeyGroupLength = 4;

// The last check of this plugin process. Plain data, so a reopened editor shows it without
// asking the site again.
struct CheckMemo
{
    std::mutex mutex;
    bool started = false;
    bool done = false;
    UpdateService::Snapshot snapshot;
};

CheckMemo& checkMemo()
{
    static CheckMemo memo;
    return memo;
}

bool parseNumber(const juce::String& text, int& out)
{
    if (text.isEmpty() || !text.containsOnly("0123456789") || text.length() > 6)
        return false;
    out = text.getIntValue();
    return true;
}

// "https://Host:443/path?q" -> scheme "https", host "host", path "/path". False for anything odd
// (user info, backslashes, spaces, an empty host).
bool splitUrl(const juce::String& url, juce::String& scheme, juce::String& host, juce::String* path = nullptr)
{
    const auto text = url.trim();
    if (text != url || text.containsAnyOf(" \t\r\n\\") || !text.contains("://"))
        return false;

    scheme = text.upToFirstOccurrenceOf("://", false, false).toLowerCase();
    auto authority = text.fromFirstOccurrenceOf("://", false, false);
    for (const auto* stop : { "/", "?", "#" })
        authority = authority.upToFirstOccurrenceOf(stop, false, false);
    if (authority.contains("@"))
        return false;
    if (path != nullptr)
    {
        auto rest = text.fromFirstOccurrenceOf("://", false, false).substring(authority.length());
        for (const auto* stop : { "?", "#" })
            rest = rest.upToFirstOccurrenceOf(stop, false, false);
        *path = rest;
    }

    host = authority.upToFirstOccurrenceOf(":", false, false).toLowerCase();
    const auto port = authority.fromFirstOccurrenceOf(":", false, false);
    if (authority.contains(":") && (port.isEmpty() || !port.containsOnly("0123456789")))
        return false;
    return host.isNotEmpty() && host.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789.-");
}

bool isHamloprodHost(const juce::String& host)
{
    return host == "hamloprod.org" || host == "www.hamloprod.org";
}

// The installer lives in the private Contabo S3 bucket of the site; /download hands out a
// presigned (10 min) path-style link: https://usc1.contabostorage.com/hamloprod-private/hpdg/<file>.
constexpr auto kStorageHost = "usc1.contabostorage.com";
constexpr auto kStoragePathPrefix = "/hamloprod-private/hpdg/";

bool isInstallerStoragePath(const juce::String& path)
{
    // No way out of the prefix: no "..", no encoded dots or slashes, nothing after it but one file.
    const auto file = path.fromFirstOccurrenceOf(kStoragePathPrefix, false, false);
    const auto lower = path.toLowerCase();
    return path.startsWith(kStoragePathPrefix) && file.isNotEmpty() && !file.contains("/") && !path.contains("..")
           && !lower.contains("%2e") && !lower.contains("%2f") && !lower.contains("%5c");
}

bool isLocalHost(const juce::String& host)
{
    return host == "localhost" || host == "127.0.0.1";
}

bool isSha256Hex(const juce::String& text)
{
    return text.length() == 64 && text.toLowerCase().containsOnly("0123456789abcdef");
}

juce::String requestBody(const juce::String& licenseKey)
{
    auto* body = new juce::DynamicObject();
    body->setProperty("licenseKey", licenseKey);
    body->setProperty("currentVersion", HPDG_VERSION_TAG);
    body->setProperty("platform", kPlatform);
    return juce::JSON::toString(juce::var(body), true);
}

juce::String readSmall(juce::InputStream& stream)
{
    juce::MemoryBlock data;
    stream.readIntoMemoryBlock(data, kMaxResponseBytes);
    return data.toString();
}

juce::File downloadFolder()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HPDG_Update");
}

UpdateService::Failure failureFor(DownloadRefusal refusal)
{
    switch (refusal)
    {
        case DownloadRefusal::InvalidKey: return UpdateService::Failure::InvalidKey;
        case DownloadRefusal::Limit: return UpdateService::Failure::Limit;
        case DownloadRefusal::NoRelease: return UpdateService::Failure::NoRelease;
        case DownloadRefusal::Untrusted: return UpdateService::Failure::Untrusted;
        case DownloadRefusal::Malformed: return UpdateService::Failure::Network;
        case DownloadRefusal::None:
        default: return UpdateService::Failure::None;
    }
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

juce::String ReleaseVersion::toTag() const
{
    auto text = juce::String(major) + "." + juce::String(minor) + "." + juce::String(patch);
    if (label.isNotEmpty())
    {
        text << "-" << label;
        if (labelNumber > 0)
            text << "." << labelNumber;
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

bool isLocalApiOverride(const juce::String& base)
{
    juce::String scheme, host;
    return splitUrl(base, scheme, host) && scheme == "http" && isLocalHost(host);
}

juce::String apiBaseUrl()
{
    const auto override = juce::SystemStats::getEnvironmentVariable("HPDG_UPDATE_API", {}).trim();
    if (override.isNotEmpty() && isLocalApiOverride(override))
        return override.trimCharactersAtEnd("/");
    return kApiBaseUrl;
}

bool isAllowedDownloadUrl(const juce::String& url, bool allowLocalhost)
{
    juce::String scheme, host, path;
    if (!splitUrl(url, scheme, host, &path))
        return false;
    if (scheme == "http")
        return allowLocalhost && isLocalHost(host);
    if (scheme != "https")
        return false;
    return isHamloprodHost(host) || (host == kStorageHost && isInstallerStoragePath(path));
}

bool isAllowedPageUrl(const juce::String& url)
{
    juce::String scheme, host;
    return splitUrl(url, scheme, host) && scheme == "https" && isHamloprodHost(host);
}

bool parseLatestResponse(const juce::String& json, LatestRelease& out)
{
    const auto root = juce::JSON::parse(json);
    if (!root.isObject())
        return false;

    const auto version = ReleaseVersion::parse(root.getProperty("version", {}).toString());
    if (!version.valid)
        return false;

    out.version = version;
    out.title = root.getProperty("title", {}).toString().trim();
    if (out.title.isEmpty())
        out.title = "HPDG " + version.toDisplayString();
    auto notes = root.getProperty("notes", {}).toString().replace("\r\n", "\n").trim();
    if (notes.length() > kMaxNotesChars)
        notes = notes.substring(0, kMaxNotesChars).trimEnd() + "...";
    out.notes = notes;
    const auto page = root.getProperty("page", {}).toString().trim();
    out.pageUrl = juce::URL(isAllowedPageUrl(page) ? page : juce::String(kReleasePageUrl));
    return true;
}

DownloadRefusal parseDownloadResponse(int statusCode, const juce::String& json, DownloadTicket& out,
                                      bool allowLocalhost)
{
    if (statusCode == 401 || statusCode == 403)
        return DownloadRefusal::InvalidKey;
    if (statusCode == 429)
        return DownloadRefusal::Limit;
    if (statusCode == 404)
        return DownloadRefusal::NoRelease;
    if (statusCode != 200)
        return DownloadRefusal::Malformed;

    const auto root = juce::JSON::parse(json);
    if (!root.isObject())
        return DownloadRefusal::Malformed;

    const auto url = root.getProperty("url", {}).toString();
    const auto sha = root.getProperty("sha256", {}).toString().trim().toLowerCase();
    const auto version = ReleaseVersion::parse(root.getProperty("version", {}).toString());
    if (url.isEmpty() || !isSha256Hex(sha) || !version.valid)
        return DownloadRefusal::Malformed;
    if (!isAllowedDownloadUrl(url, allowLocalhost))
        return DownloadRefusal::Untrusted;

    const auto size = root.getProperty("size", 0);
    out.url = url;
    out.sha256 = sha;
    out.version = version;
    out.size = size.isInt() || size.isInt64() || size.isDouble() ? juce::jmax<juce::int64>(0, static_cast<juce::int64>(size)) : 0;
    if (out.size > kMaxInstallerBytes)
        return DownloadRefusal::Malformed;
    return DownloadRefusal::None;
}

juce::String normalizeLicenseKey(const juce::String& text)
{
    // "HPDG-..." / "HPDG ..." is the prefix; 16 bare characters are the key without it.
    const auto upper = text.toUpperCase().trim();
    const bool separatedPrefix = upper.startsWith("HPDG") && juce::String(" -_").containsChar(upper[4]);
    auto compact = upper.removeCharacters(" \t\r\n-_");
    if (separatedPrefix || (compact.length() == 4 + kKeyGroups * kKeyGroupLength && compact.startsWith("HPDG")))
        compact = compact.substring(4);
    if (compact.length() != kKeyGroups * kKeyGroupLength)
        return {};

    juce::String key("HPDG");
    for (int i = 0; i < compact.length(); ++i)
    {
        auto c = compact[i];
        if (c == 'O') c = '0';
        if (c == 'I' || c == 'L') c = '1';
        if (juce::String(kKeyAlphabet).indexOfChar(c) < 0)
            return {};
        if (i % kKeyGroupLength == 0)
            key << "-";
        key << juce::String::charToString(c);
    }
    return key;
}

juce::String sha256OfFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return {};
    juce::FileInputStream stream(file);
    if (!stream.openedOk())
        return {};
    return juce::SHA256(stream).toHexString().toLowerCase();
}

bool sha256Equals(const juce::String& a, const juce::String& b)
{
    return isSha256Hex(a) && a.trim().equalsIgnoreCase(b.trim());
}

LicenseStore::LicenseStore(juce::File directory)
    : file(directory.getChildFile("license.json"))
{
}

juce::File LicenseStore::defaultDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("HamloProd")
        .getChildFile("HPDG");
}

juce::String LicenseStore::load() const
{
    if (!file.existsAsFile())
        return {};
    return normalizeLicenseKey(juce::JSON::parse(file).getProperty("licenseKey", {}).toString());
}

bool LicenseStore::save(const juce::String& key)
{
    const auto normalized = normalizeLicenseKey(key);
    if (normalized.isEmpty() || !file.getParentDirectory().createDirectory().wasOk())
        return false;
    auto* object = new juce::DynamicObject();
    object->setProperty("licenseKey", normalized);
    return file.replaceWithText(juce::JSON::toString(juce::var(object)));
}

void LicenseStore::clear()
{
    file.deleteFile();
}

UpdateService::UpdateService()
    : juce::Thread("HPDG update")
{
    auto& memo = checkMemo();
    const std::lock_guard<std::mutex> lock(memo.mutex);
    if (memo.done)
        current = memo.snapshot;
}

UpdateService::~UpdateService()
{
    removeAllChangeListeners();
    stopThread(kTimeoutMs + 2000);
}

UpdateService::Snapshot UpdateService::snapshot() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return current;
}

void UpdateService::checkOnce()
{
    {
        auto& memo = checkMemo();
        const std::lock_guard<std::mutex> lock(memo.mutex);
        if (memo.started)
            return;
        memo.started = true;
    }
    checkNow();
}

void UpdateService::checkNow()
{
    start(Job::Check);
}

void UpdateService::download(const juce::String& licenseKey)
{
    start(Job::Download, licenseKey);
}

void UpdateService::start(Job job, const juce::String& licenseKey)
{
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (pendingJob != Job::None || current.state == State::Checking || current.state == State::Downloading)
            return; // one job at a time; the button shows the running one
    }
    // The previous job has published its result; its thread is at most a few lines from the end.
    if (!waitForThreadToExit(2000))
        return;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        pendingJob = job;
        pendingKey = licenseKey;
        if (job == Job::Check)
            current.state = State::Checking;
        else
        {
            current.state = State::Downloading;
            current.progress = 0.0f;
        }
        current.failure = Failure::None;
    }
    sendChangeMessage();
    startThread(juce::Thread::Priority::low);
}

void UpdateService::publish(const Snapshot& next)
{
    {
        const std::lock_guard<std::mutex> lock(mutex);
        current = next;
    }
    sendChangeMessage();
}

void UpdateService::run()
{
    Job job;
    juce::String key;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        job = pendingJob;
        key = pendingKey;
        pendingJob = Job::None;
        pendingKey.clear();
    }

    if (job == Job::Check)
        runCheck();
    else if (job == Job::Download)
        runDownload(key);
}

void UpdateService::runCheck()
{
    auto next = snapshot();
    int statusCode = 0;
    const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                             .withConnectionTimeoutMs(kTimeoutMs)
                             .withStatusCode(&statusCode)
                             .withExtraHeaders("User-Agent: HPDG/" HPDG_VERSION_TAG "\r\nAccept: application/json");

    LatestRelease latest;
    bool answered = false;
    if (auto stream = juce::URL(apiBaseUrl() + "/latest").createInputStream(options))
        answered = statusCode == 200 && !threadShouldExit() && parseLatestResponse(readSmall(*stream), latest);

    if (threadShouldExit())
        return;

    next.failure = Failure::None;
    if (!answered)
        next.state = State::Offline;
    else if (compare(latest.version, currentVersion()) > 0)
    {
        next.state = State::UpdateAvailable;
        next.latest = latest;
    }
    else
        next.state = State::UpToDate;

    {
        auto& memo = checkMemo();
        const std::lock_guard<std::mutex> lock(memo.mutex);
        memo.done = true;
        memo.snapshot = next;
    }
    publish(next);
}

void UpdateService::runDownload(const juce::String& licenseKey)
{
    auto next = snapshot();
    const auto fail = [this, &next](Failure failure)
    {
        if (threadShouldExit())
            return;
        next.state = State::Failed;
        next.failure = failure;
        next.progress = 0.0f;
        ++next.finishedDownloads;
        publish(next);
    };

    const auto base = apiBaseUrl();
    const bool local = isLocalApiOverride(base);

    // 1. The link: the server checks the purchase behind the key.
    DownloadTicket ticket;
    {
        int statusCode = 0;
        const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                 .withConnectionTimeoutMs(kTimeoutMs)
                                 .withStatusCode(&statusCode)
                                 .withExtraHeaders("User-Agent: HPDG/" HPDG_VERSION_TAG
                                                   "\r\nContent-Type: application/json\r\nAccept: application/json");
        auto stream = juce::URL(base + "/download").withPOSTData(requestBody(licenseKey)).createInputStream(options);
        if (threadShouldExit())
            return;
        if (stream == nullptr && statusCode == 0)
            return fail(Failure::Network);
        const auto refusal = parseDownloadResponse(statusCode, stream != nullptr ? readSmall(*stream) : juce::String(),
                                                   ticket, local);
        if (refusal != DownloadRefusal::None)
            return fail(failureFor(refusal));
    }

    // 2. The file, into %TEMP%\HPDG_Update. The name comes from the version, never the server.
    const auto folder = downloadFolder();
    if (!folder.createDirectory().wasOk())
        return fail(Failure::Disk);
    const auto target = folder.getChildFile("HPDG_VST3_Setup_" + ticket.version.toTag() + ".exe");
    const auto partial = target.withFileExtension(".part");
    partial.deleteFile();

    int statusCode = 0;
    const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                             .withConnectionTimeoutMs(kTimeoutMs)
                             .withStatusCode(&statusCode)
                             .withExtraHeaders("User-Agent: HPDG/" HPDG_VERSION_TAG);
    auto stream = juce::URL(ticket.url).createInputStream(options);
    if (threadShouldExit())
        return;
    if (stream == nullptr || statusCode != 200)
        return fail(Failure::Network);

    const auto total = ticket.size > 0 ? ticket.size : stream->getTotalLength();
    bool complete = false;
    bool diskError = false;
    {
        juce::FileOutputStream output(partial);
        if (!output.openedOk())
            return fail(Failure::Disk);

        constexpr int chunkBytes = 64 * 1024;
        juce::HeapBlock<char> buffer(chunkBytes);
        juce::int64 written = 0;
        int lastPercent = -1;
        while (!threadShouldExit() && written <= kMaxInstallerBytes)
        {
            const auto read = stream->read(buffer.get(), chunkBytes);
            if (read <= 0)
                break; // end of the file or a dropped connection: the size check below decides
            if (!output.write(buffer.get(), static_cast<size_t>(read)))
            {
                diskError = true;
                break;
            }
            written += read;

            const auto percent = total > 0 ? static_cast<int>(juce::jlimit<juce::int64>(0, 100, written * 100 / total)) : 0;
            if (percent != lastPercent)
            {
                lastPercent = percent;
                next.state = State::Downloading;
                next.progress = static_cast<float>(percent) / 100.0f;
                publish(next);
            }
        }
        output.flush();
        diskError = diskError || output.getStatus().failed();
        complete = !threadShouldExit() && !diskError && written > 0 && written <= kMaxInstallerBytes
                   && (total <= 0 || written == total);
    }

    if (!complete)
    {
        partial.deleteFile();
        if (!threadShouldExit())
            fail(diskError ? Failure::Disk : Failure::Network);
        return;
    }

    // 3. Only a file whose SHA-256 matches the server's is ever started.
    if (!sha256Equals(sha256OfFile(partial), ticket.sha256))
    {
        partial.deleteFile();
        return fail(Failure::Checksum);
    }
    target.deleteFile();
    if (!partial.moveFileTo(target))
    {
        partial.deleteFile();
        return fail(Failure::Disk);
    }

    next.state = State::ReadyToInstall;
    next.failure = Failure::None;
    next.progress = 1.0f;
    next.installer = target;
    ++next.finishedDownloads;
    publish(next);
}

bool UpdateService::launchInstaller()
{
    const auto file = snapshot().installer;
    return file.existsAsFile() && file.startAsProcess();
}
} // namespace bbg::update
