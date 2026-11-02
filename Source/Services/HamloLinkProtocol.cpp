#include "HamloLinkProtocol.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace hamlo::link
{
namespace
{
constexpr char kMagic[8] { 'H', 'A', 'M', 'L', 'O', 'L', 'N', 'K' };
constexpr juce::int64 kDeadSlotMs = 60 * 60 * 1000;
constexpr size_t kHeaderBytes = offsetof(Snapshot, instanceName);

// The counter lives in shared memory; std::atomic<std::uint32_t> has the same size and layout and is lock-free.
std::atomic<std::uint32_t>& counter(Snapshot* s) noexcept
{
    static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t) && std::atomic<std::uint32_t>::is_always_lock_free);
    return *reinterpret_cast<std::atomic<std::uint32_t>*>(&s->sequence);
}
} // namespace

void initialise(Snapshot& s) noexcept
{
    std::memset(&s, 0, sizeof(Snapshot));
    std::memcpy(s.magic, kMagic, sizeof(kMagic));
    s.versionMajor = kVersionMajor;
    s.versionMinor = kVersionMinor;
}

bool samePayload(const Snapshot& a, const Snapshot& b) noexcept
{
    const auto* pa = reinterpret_cast<const char*>(&a) + kHeaderBytes;
    const auto* pb = reinterpret_cast<const char*>(&b) + kHeaderBytes;
    return std::memcmp(pa, pb, sizeof(Snapshot) - kHeaderBytes) == 0 && a.instanceId == b.instanceId;
}

juce::String roleName(const Snapshot& s, int roleId)
{
    if (roleId <= 0 || roleId >= kMaxRoles)
        return {};
    return juce::String::fromUTF8(s.roleNames[roleId], static_cast<int>(strnlen(s.roleNames[roleId], kRoleChars)));
}

Status check(const Snapshot& s, juce::int64 nowMs) noexcept
{
    if (std::memcmp(s.magic, kMagic, sizeof(kMagic)) != 0)
        return Status::Missing;
    if (s.versionMajor > kVersionMajor)
        return Status::NewerMajor;
    if (s.versionMajor < kVersionMajor)
        return Status::Missing; // there is no older protocol
    if (s.heartbeatMs <= 0 || nowMs - s.heartbeatMs > kAliveMs)
        return Status::Stale;
    return Status::Ok;
}

juce::File linkDirectory()
{
   #if JUCE_WINDOWS
    const auto base = juce::File::getSpecialLocation(juce::File::windowsLocalAppData);
   #else
    const auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #endif
    return base.getChildFile("HamloProd").getChildFile("Link");
}

juce::File slotFile(std::uint64_t instanceId)
{
    return linkDirectory().getChildFile("hpdg-" + juce::String::toHexString(static_cast<juce::int64>(instanceId)).paddedLeft('0', 16) + ".slot");
}

// ---- the mapping. Windows: both sides share read, write and delete, so a reader can open the slot while HPDG
// writes it and HPDG can delete it while a reader still has it open (juce::MemoryMappedFile's read-only mode does not
// share write access). Elsewhere: juce::MemoryMappedFile read-write on both sides.
struct SharedSlot::Native
{
   #if JUCE_WINDOWS
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    void* view = nullptr;
    ~Native()
    {
        if (view != nullptr) UnmapViewOfFile(view);
        if (mapping != nullptr) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
   #else
    std::unique_ptr<juce::MemoryMappedFile> mapped;
   #endif
};

namespace
{
std::unique_ptr<SharedSlot::Native> mapSlot(const juce::File& f, bool writer)
{
    auto n = std::make_unique<SharedSlot::Native>();
   #if JUCE_WINDOWS
    n->file = CreateFileW(f.getFullPathName().toWideCharPointer(), writer ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, writer ? OPEN_ALWAYS : OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (n->file == INVALID_HANDLE_VALUE)
        return {};
    LARGE_INTEGER size {};
    if (!writer && (!GetFileSizeEx(n->file, &size) || size.QuadPart < static_cast<LONGLONG>(sizeof(Snapshot))))
        return {}; // not (yet) a full slot
    n->mapping = CreateFileMappingW(n->file, nullptr, writer ? PAGE_READWRITE : PAGE_READONLY, 0, static_cast<DWORD>(sizeof(Snapshot)), nullptr);
    if (n->mapping == nullptr)
        return {};
    n->view = MapViewOfFile(n->mapping, writer ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ, 0, 0, sizeof(Snapshot));
    if (n->view == nullptr)
        return {};
   #else
    if (!writer && f.getSize() < static_cast<juce::int64>(sizeof(Snapshot)))
        return {};
    if (writer && f.getSize() < static_cast<juce::int64>(sizeof(Snapshot)))
    {
        juce::MemoryBlock zeros(sizeof(Snapshot), true);
        f.replaceWithData(zeros.getData(), zeros.getSize());
    }
    n->mapped = std::make_unique<juce::MemoryMappedFile>(f, juce::Range<juce::int64>(0, sizeof(Snapshot)), juce::MemoryMappedFile::readWrite, false);
    if (n->mapped->getData() == nullptr)
        return {};
   #endif
    return n;
}

void* addressOf(SharedSlot::Native& n)
{
   #if JUCE_WINDOWS
    return n.view;
   #else
    return n.mapped->getData();
   #endif
}
} // namespace

std::unique_ptr<SharedSlot> SharedSlot::openForWriting(const juce::File& f)
{
    if (!f.getParentDirectory().createDirectory())
        return {};
    auto native = mapSlot(f, true);
    if (native == nullptr)
        return {};
    std::unique_ptr<SharedSlot> slot(new SharedSlot());
    slot->native = std::move(native);
    slot->shared = static_cast<Snapshot*>(addressOf(*slot->native));
    slot->writable = true;
    return slot;
}

std::unique_ptr<SharedSlot> SharedSlot::openForReading(const juce::File& f)
{
    auto native = mapSlot(f, false);
    if (native == nullptr)
        return {};
    std::unique_ptr<SharedSlot> slot(new SharedSlot());
    slot->native = std::move(native);
    slot->shared = static_cast<Snapshot*>(addressOf(*slot->native));
    return slot;
}

SharedSlot::~SharedSlot() = default;

void SharedSlot::write(const Snapshot& s, juce::int64 nowMs) noexcept
{
    if (!writable || shared == nullptr)
        return;
    auto& seq = counter(shared);
    const auto start = seq.load(std::memory_order_relaxed) | 1u; // odd: writing (a crashed writer's odd value stays odd)
    seq.store(start, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    // everything but the counter
    std::memcpy(shared->magic, s.magic, offsetof(Snapshot, sequence));
    std::memcpy(reinterpret_cast<char*>(shared) + offsetof(Snapshot, heartbeatMs), reinterpret_cast<const char*>(&s) + offsetof(Snapshot, heartbeatMs),
                sizeof(Snapshot) - offsetof(Snapshot, heartbeatMs));
    shared->heartbeatMs = nowMs;
    std::atomic_thread_fence(std::memory_order_release);
    seq.store(start + 1u, std::memory_order_release);
}

void SharedSlot::beat(juce::int64 nowMs) noexcept
{
    if (!writable || shared == nullptr)
        return;
    auto& seq = counter(shared);
    const auto start = seq.load(std::memory_order_relaxed) | 1u;
    seq.store(start, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    shared->heartbeatMs = nowMs;
    std::atomic_thread_fence(std::memory_order_release);
    seq.store(start + 1u, std::memory_order_release);
}

Status SharedSlot::read(Snapshot& out) const noexcept
{
    if (shared == nullptr)
        return Status::Missing;
    auto& seq = counter(shared);
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto before = seq.load(std::memory_order_acquire);
        if ((before & 1u) == 0)
        {
            std::memcpy(&out, shared, sizeof(Snapshot));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq.load(std::memory_order_relaxed) == before)
            {
                out.sequence = before;
                return Status::Ok;
            }
        }
        std::this_thread::yield();
    }
    return Status::Busy;
}

std::vector<Listing> listInstances(juce::int64 nowMs)
{
    std::vector<Listing> list;
    for (const auto& entry : juce::RangedDirectoryIterator(linkDirectory(), false, "hpdg-*.slot", juce::File::findFiles))
    {
        Listing l;
        l.file = entry.getFile();
        if (auto slot = SharedSlot::openForReading(l.file))
        {
            auto s = std::make_unique<Snapshot>();
            l.status = slot->read(*s);
            if (l.status == Status::Ok)
                l.status = check(*s, nowMs);
            if (l.status == Status::Ok || l.status == Status::Stale || l.status == Status::NewerMajor)
            {
                l.instanceId = s->instanceId;
                l.name = juce::String::fromUTF8(s->instanceName, static_cast<int>(strnlen(s->instanceName, kNameChars)));
                l.genre = s->genre;
                l.bpm = s->bpm;
            }
        }
        list.push_back(l);
    }
    std::stable_sort(list.begin(), list.end(), [](const Listing& a, const Listing& b)
                     { return (a.status == Status::Ok) > (b.status == Status::Ok); });
    return list;
}

void removeDeadSlots(juce::int64 nowMs)
{
    for (const auto& entry : juce::RangedDirectoryIterator(linkDirectory(), false, "hpdg-*.slot", juce::File::findFiles))
    {
        bool dead = entry.getModificationTime().toMilliseconds() < nowMs - kDeadSlotMs;
        if (auto slot = SharedSlot::openForReading(entry.getFile()))
        {
            auto s = std::make_unique<Snapshot>();
            if (slot->read(*s) == Status::Ok)
                dead = s->heartbeatMs <= 0 || nowMs - s->heartbeatMs > kDeadSlotMs;
        }
        if (dead)
            entry.getFile().deleteFile();
    }
}
} // namespace hamlo::link
