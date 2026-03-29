#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "../Source/Core/TrackSemantics.h"

namespace bbg
{
namespace
{
[[noreturn]] void fail(const char* message)
{
    throw std::runtime_error(message);
}

void expect(bool condition, const char* message)
{
    if (!condition)
        fail(message);
}

constexpr std::array<TrackType, kTrackTypeCount> allTrackTypes {
    TrackType::HiHat,
    TrackType::OpenHat,
    TrackType::Snare,
    TrackType::ClapGhostSnare,
    TrackType::Kick,
    TrackType::GhostKick,
    TrackType::Ride,
    TrackType::Cymbal,
    TrackType::Perc,
    TrackType::HatFX,
    TrackType::Sub808,
};

constexpr bool legacyHatFamily(TrackType type)
{
    return type == TrackType::HiHat
        || type == TrackType::HatFX
        || type == TrackType::OpenHat
        || type == TrackType::Perc;
}

constexpr bool legacyKickFamily(TrackType type)
{
    return type == TrackType::Kick || type == TrackType::GhostKick;
}

void testRoleMapping()
{
    expect(roleFromTrackType(TrackType::HiHat) == TrackRole::HiHat, "HiHat role mapping mismatch.");
    expect(roleFromTrackType(TrackType::OpenHat) == TrackRole::OpenHat, "OpenHat role mapping mismatch.");
    expect(roleFromTrackType(TrackType::Snare) == TrackRole::Snare, "Snare role mapping mismatch.");
    expect(roleFromTrackType(TrackType::ClapGhostSnare) == TrackRole::ClapGhostSnare, "Clap/GhostSnare role mapping mismatch.");
    expect(roleFromTrackType(TrackType::Kick) == TrackRole::Kick, "Kick role mapping mismatch.");
    expect(roleFromTrackType(TrackType::GhostKick) == TrackRole::GhostKick, "GhostKick role mapping mismatch.");
    expect(roleFromTrackType(TrackType::Ride) == TrackRole::Ride, "Ride role mapping mismatch.");
    expect(roleFromTrackType(TrackType::Cymbal) == TrackRole::Cymbal, "Cymbal role mapping mismatch.");
    expect(roleFromTrackType(TrackType::Perc) == TrackRole::Perc, "Perc role mapping mismatch.");
    expect(roleFromTrackType(TrackType::HatFX) == TrackRole::HatFX, "HatFX role mapping mismatch.");
    expect(roleFromTrackType(TrackType::Sub808) == TrackRole::Bass, "Sub808 role mapping mismatch.");
}

void testFamilyMapping()
{
    expect(familyFromTrackType(TrackType::HiHat) == TrackFamily::HatFamily, "HiHat family mapping mismatch.");
    expect(familyFromTrackType(TrackType::OpenHat) == TrackFamily::HatFamily, "OpenHat family mapping mismatch.");
    expect(familyFromTrackType(TrackType::Perc) == TrackFamily::HatFamily, "Perc family mapping mismatch.");
    expect(familyFromTrackType(TrackType::HatFX) == TrackFamily::HatFamily, "HatFX family mapping mismatch.");
    expect(familyFromTrackType(TrackType::Snare) == TrackFamily::SnareFamily, "Snare family mapping mismatch.");
    expect(familyFromTrackType(TrackType::ClapGhostSnare) == TrackFamily::SnareFamily, "Clap/GhostSnare family mapping mismatch.");
    expect(familyFromTrackType(TrackType::Kick) == TrackFamily::KickFamily, "Kick family mapping mismatch.");
    expect(familyFromTrackType(TrackType::GhostKick) == TrackFamily::KickFamily, "GhostKick family mapping mismatch.");
    expect(familyFromTrackType(TrackType::Ride) == TrackFamily::CymbalFamily, "Ride family mapping mismatch.");
    expect(familyFromTrackType(TrackType::Cymbal) == TrackFamily::CymbalFamily, "Cymbal family mapping mismatch.");
    expect(familyFromTrackType(TrackType::Sub808) == TrackFamily::BassFamily, "Sub808 family mapping mismatch.");
}

void testHelperParity()
{
    for (auto type : allTrackTypes)
    {
        expect(isHatFamily(type) == legacyHatFamily(type), "Hat family helper diverged from legacy logic.");
        expect(isKickFamily(type) == legacyKickFamily(type), "Kick family helper diverged from legacy logic.");
    }
}

void testDefaultLaneRoleLabels()
{
    expect(std::string_view(defaultLaneRoleForTrackType(TrackType::Kick)) == "core_pulse", "Kick lane-role label mismatch.");
    expect(std::string_view(defaultLaneRoleForTrackType(TrackType::Snare)) == "backbeat", "Snare lane-role label mismatch.");
    expect(std::string_view(defaultLaneRoleForTrackType(TrackType::HiHat)) == "carrier", "HiHat lane-role label mismatch.");
    expect(std::string_view(defaultLaneRoleForTrackType(TrackType::OpenHat)) == "accent", "OpenHat lane-role label mismatch.");
    expect(std::string_view(defaultLaneRoleForTrackType(TrackType::ClapGhostSnare)) == "support", "Clap/GhostSnare lane-role label mismatch.");
    expect(std::string_view(defaultLaneRoleForTrackType(TrackType::GhostKick)) == "support", "GhostKick lane-role label mismatch.");
    expect(std::string_view(defaultLaneRoleForTrackType(TrackType::HatFX)) == "accent_fx", "HatFX lane-role label mismatch.");
    expect(std::string_view(defaultLaneRoleForTrackType(TrackType::Ride)) == "support", "Ride lane-role label mismatch.");
    expect(std::string_view(defaultLaneRoleForTrackType(TrackType::Cymbal)) == "crash", "Cymbal lane-role label mismatch.");
    expect(std::string_view(defaultLaneRoleForTrackType(TrackType::Perc)) == "texture", "Perc lane-role label mismatch.");
    expect(std::string_view(defaultLaneRoleForTrackType(TrackType::Sub808)) == "bass_anchor", "Sub808 lane-role label mismatch.");
    expect(std::string_view(defaultLaneRoleForTrackType(static_cast<TrackType>(kTrackTypeCount))) == "lane", "Default lane-role label mismatch.");
}

int runTest(const char* name, const std::function<void()>& test)
{
    try
    {
        test();
        std::cout << "[PASS] " << name << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << name << ": " << error.what() << std::endl;
        return 1;
    }
}
} // namespace
} // namespace bbg

int main()
{
    using namespace bbg;

    int failures = 0;
    failures += runTest("Track role mapping", testRoleMapping);
    failures += runTest("Track family mapping", testFamilyMapping);
    failures += runTest("Helper parity", testHelperParity);
    failures += runTest("Default lane-role labels", testDefaultLaneRoleLabels);

    if (failures == 0)
    {
        std::cout << "All track semantics tests passed." << std::endl;
        return 0;
    }

    std::cerr << failures << " track semantics test(s) failed." << std::endl;
    return 1;
}