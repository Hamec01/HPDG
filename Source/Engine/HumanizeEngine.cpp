#include "HumanizeEngine.h"

#include <algorithm>

namespace bbg
{
void HumanizeEngine::applyHumanize(PatternProject& project,
                                   const BoomBapStyleProfile& style,
                                   const std::unordered_set<TrackType>& mutableTracks,
                                   std::mt19937& rng)
{
    const float amount = std::clamp(project.params.humanizeAmount, 0.0f, 1.0f);
    float earlyScale = 1.0f;
    float lateScale = 1.0f;
    float velScale = 1.0f;

    if (style.substyle == BoomBapSubstyle::RussianUnderground)
    {
        earlyScale = 0.7f;
        lateScale = 0.74f;
        velScale = 0.7f;
    }
    else if (style.substyle == BoomBapSubstyle::BoomBapGold || style.substyle == BoomBapSubstyle::Jazzy)
    {
        earlyScale = 1.08f;
        lateScale = 1.16f;
        velScale = 1.12f;
    }

    const int maxEarly = static_cast<int>(amount * 7.0f * earlyScale);
    const int maxLate = static_cast<int>(amount * (12.0f + style.barVariationAmount * 8.0f) * lateScale);
    const int maxVelocity = static_cast<int>(amount * 10.0f * velScale);

    std::uniform_int_distribution<int> velJitter(-maxVelocity, maxVelocity);
    std::uniform_int_distribution<int> earlyLate(-maxEarly, maxLate);

    for (auto& track : project.tracks)
    {
        if (mutableTracks.find(track.type) == mutableTracks.end())
            continue;

        for (auto& note : track.notes)
        {
            const int stepInBar = note.step % 16;
            const bool snareAnchor = (track.type == TrackType::Snare) && ((note.step % 16 == 4) || (note.step % 16 == 12));
            int timingJitter = snareAnchor ? std::max(0, earlyLate(rng) / 3) : earlyLate(rng);

            if (track.type == TrackType::HiHat)
                timingJitter /= 2;
            if (track.type == TrackType::Ride)
                timingJitter = static_cast<int>(timingJitter * 0.75f);
            if (track.type == TrackType::Kick)
            {
                const bool anchor = (stepInBar == 0 || stepInBar == 8);
                if (anchor)
                    timingJitter = std::clamp(timingJitter, -2, 3);
                else if (stepInBar == 15)
                    timingJitter = std::clamp(timingJitter, -6, 14);
                else
                    timingJitter = std::clamp(timingJitter, -5, 9);
            }

            if (track.type == TrackType::Snare)
            {
                if (stepInBar == 4)
                    timingJitter = std::max(1, timingJitter);
                else if (stepInBar == 12)
                    timingJitter = std::max(0, timingJitter - 1);
            }

            if (track.type == TrackType::ClapGhostSnare)
                timingJitter = std::clamp(timingJitter + 4, 2, 18);

            if (note.isGhost)
                timingJitter = std::clamp(timingJitter, -12, 18);

            if (style.substyle == BoomBapSubstyle::RussianUnderground)
                timingJitter = std::clamp(timingJitter, -6, 9);

            note.microOffset += timingJitter;
            note.velocity = std::clamp(note.velocity + velJitter(rng), 1, 127);
        }
    }
}
} // namespace bbg
