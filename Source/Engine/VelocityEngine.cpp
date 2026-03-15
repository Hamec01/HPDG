#include "VelocityEngine.h"

#include <algorithm>

namespace bbg
{
void VelocityEngine::applyVelocityShape(PatternProject& project,
                                        const BoomBapStyleProfile& style,
                                        const std::unordered_set<TrackType>& mutableTracks)
{
    const float amount = std::clamp(project.params.velocityAmount, 0.0f, 1.0f);

    for (auto& track : project.tracks)
    {
        if (mutableTracks.find(track.type) == mutableTracks.end())
            continue;

        for (auto& note : track.notes)
        {
            const int stepInBar = note.step % 16;
            int minVel = note.isGhost ? style.ghostVelocityMin : style.snareVelocityMin;
            int maxVel = note.isGhost ? style.ghostVelocityMax : style.snareVelocityMax;

            if (track.type == TrackType::Kick)
            {
                minVel = style.kickVelocityMin;
                maxVel = style.kickVelocityMax;
            }
            else if (track.type == TrackType::HiHat)
            {
                minVel = style.hatVelocityMin;
                maxVel = style.hatVelocityMax;
            }
            else if (track.type == TrackType::ClapGhostSnare)
            {
                minVel = style.clapVelocityMin;
                maxVel = style.clapVelocityMax;
            }
            else if (track.type == TrackType::OpenHat)
            {
                minVel = style.openHatVelocityMin;
                maxVel = style.openHatVelocityMax;
            }
            else if (track.type == TrackType::Ride)
            {
                minVel = std::max(40, style.hatVelocityMin - 8);
                maxVel = std::min(120, style.hatVelocityMax - 4);
            }
            else if (track.type == TrackType::Perc)
            {
                minVel = style.percVelocityMin;
                maxVel = style.percVelocityMax;
            }

            int target = (minVel + maxVel) / 2;
            if ((note.step % 16) >= 12 && !note.isGhost)
                target = std::min(maxVel, target + 4);

            if (track.type == TrackType::Kick)
            {
                if (stepInBar == 0 || stepInBar == 8)
                    target = std::min(maxVel, target + 8);
                if (stepInBar == 15)
                    target = std::max(minVel, target - 6);
            }

            if (track.type == TrackType::Snare)
            {
                if (stepInBar == 4 || stepInBar == 12)
                    target = std::min(maxVel, target + 6);
            }

            if (track.type == TrackType::HiHat)
            {
                if ((stepInBar % 4) == 0)
                    target = std::min(maxVel, target + 7);
                else if ((stepInBar % 2) == 1)
                    target = std::max(minVel, target - 4);

                if (stepInBar >= 14)
                    target = std::min(maxVel, target + 4);
            }

            if (track.type == TrackType::Ride)
            {
                if ((stepInBar % 4) == 2)
                    target = std::min(maxVel, target + 6);
                if (stepInBar >= 12)
                    target = std::min(maxVel, target + 3);
            }

            if (style.substyle == BoomBapSubstyle::RussianUnderground)
            {
                if (track.type == TrackType::Kick || track.type == TrackType::Snare)
                    target = std::clamp(target, minVel + 4, maxVel - 2);
                if (track.type == TrackType::HiHat || track.type == TrackType::Ride || track.type == TrackType::Perc)
                    target = std::clamp(target - 3, minVel, maxVel);
            }
            else if (style.substyle == BoomBapSubstyle::BoomBapGold || style.substyle == BoomBapSubstyle::Jazzy)
            {
                if (!note.isGhost && (track.type == TrackType::HiHat || track.type == TrackType::Ride))
                    target = std::min(maxVel, target + ((stepInBar >= 12) ? 5 : 2));
            }

            note.velocity = static_cast<int>(note.velocity * (1.0f - amount) + target * amount);
            note.velocity = std::clamp(note.velocity, minVel, maxVel);

            if (note.isGhost)
                note.velocity = std::min(note.velocity, style.ghostVelocityMax - 2);
        }
    }
}
} // namespace bbg
