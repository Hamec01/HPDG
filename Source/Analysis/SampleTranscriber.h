#pragma once

#include <algorithm>
#include <vector>

#include "BasslineInferer.h"
#include "SampleAnalysisRequest.h"

namespace bbg
{
class SampleTranscriber
{
public:
    SampleTranscription transcribe(const LaneEvidenceMap& evidence,
                                   const BasslineInferenceResult& bassline,
                                   const SampleAnalysisRequest& request) const
    {
        SampleTranscription transcription;

        if (request.detectDrumEvents)
        {
            for (int step = 0; step < static_cast<int>(evidence.steps.size()); ++step)
            {
                const auto& current = evidence.steps[static_cast<size_t>(step)];
                const auto* prev = step > 0 ? &evidence.steps[static_cast<size_t>(step - 1)] : nullptr;
                const auto* next = step + 1 < static_cast<int>(evidence.steps.size()) ? &evidence.steps[static_cast<size_t>(step + 1)] : nullptr;
                const int stepInBar = evidence.stepsPerBar > 0 ? step % evidence.stepsPerBar : step;

                const bool kickPeak = isLocalPeak(current.kickProbability, prev != nullptr ? prev->kickProbability : current.kickProbability, next != nullptr ? next->kickProbability : current.kickProbability);
                const bool snarePeak = isLocalPeak(current.snareProbability, prev != nullptr ? prev->snareProbability : current.snareProbability, next != nullptr ? next->snareProbability : current.snareProbability);
                const bool hatPeak = isLocalPeak(current.hatProbability, prev != nullptr ? prev->hatProbability : current.hatProbability, next != nullptr ? next->hatProbability : current.hatProbability);
                const bool openPeak = isLocalPeak(current.openHatProbability, prev != nullptr ? prev->openHatProbability : current.openHatProbability, next != nullptr ? next->openHatProbability : current.openHatProbability);
                const bool percPeak = isLocalPeak(current.percProbability, prev != nullptr ? prev->percProbability : current.percProbability, next != nullptr ? next->percProbability : current.percProbability);

                if (current.kickProbability > 0.70f || ((current.isStrongBeat || kickPeak) && current.kickProbability > 0.46f))
                {
                    const bool ghost = !current.isStrongBeat && current.kickProbability < 0.60f;
                    addOrUpgrade(transcription.drumEvents,
                                 makeEvent(ghost ? TrackType::GhostKick : TrackType::Kick,
                                           step,
                                           1,
                                           static_cast<int>(std::round(74.0f + current.kickProbability * 46.0f)),
                                           36,
                                           current.kickProbability,
                                           ghost));
                }

                const bool canonicalBackbeat = stepInBar == 4 || stepInBar == 12;
                if (current.snareProbability > 0.68f || ((canonicalBackbeat || snarePeak) && current.snareProbability > 0.44f))
                {
                    const bool ghost = !canonicalBackbeat && current.snareProbability < 0.58f;
                    addOrUpgrade(transcription.drumEvents,
                                 makeEvent(ghost ? TrackType::ClapGhostSnare : TrackType::Snare,
                                           step,
                                           1,
                                           static_cast<int>(std::round(70.0f + current.snareProbability * 48.0f)),
                                           38,
                                           current.snareProbability,
                                           ghost));
                }

                if (current.openHatProbability > current.hatProbability + 0.08f
                    && (current.openHatProbability > 0.60f || (openPeak && current.openHatProbability > 0.42f)))
                {
                    addOrUpgrade(transcription.drumEvents,
                                 makeEvent(TrackType::OpenHat,
                                           step,
                                           current.sustain > 0.45f ? 2 : 1,
                                           static_cast<int>(std::round(62.0f + current.openHatProbability * 34.0f)),
                                           46,
                                           current.openHatProbability,
                                           false));
                }
                else if (current.hatProbability > 0.48f
                         || (((stepInBar % 2) == 0 || hatPeak) && current.hatProbability > 0.38f))
                {
                    addOrUpgrade(transcription.drumEvents,
                                 makeEvent(TrackType::HiHat,
                                           step,
                                           1,
                                           static_cast<int>(std::round(42.0f + current.hatProbability * 42.0f)),
                                           42,
                                           current.hatProbability,
                                           false));
                }

                if (current.percProbability > 0.58f || (percPeak && current.percProbability > 0.42f))
                {
                    addOrUpgrade(transcription.drumEvents,
                                 makeEvent(TrackType::Perc,
                                           step,
                                           1,
                                           static_cast<int>(std::round(44.0f + current.percProbability * 34.0f)),
                                           50,
                                           current.percProbability,
                                           false));
                }
            }
        }

        if (request.detectBassline)
            transcription.bassEvents = bassline.events;

        transcription.hasDetectedDrums = !transcription.drumEvents.empty();
        transcription.hasDetectedBass = !transcription.bassEvents.empty();
        return transcription;
    }

private:
    static bool isLocalPeak(float value, float prev, float next)
    {
        return value >= prev && value >= next;
    }

    static TranscribedEvent makeEvent(TrackType lane,
                                      int step,
                                      int lengthSteps,
                                      int velocity,
                                      int pitch,
                                      float confidence,
                                      bool ghost)
    {
        TranscribedEvent event;
        event.lane = lane;
        event.step = step;
        event.lengthSteps = std::max(1, lengthSteps);
        event.velocity = std::clamp(velocity, 1, 127);
        event.pitch = std::clamp(pitch, 0, 127);
        event.confidence = std::clamp(confidence, 0.0f, 1.0f);
        event.ghost = ghost;
        return event;
    }

    static void addOrUpgrade(std::vector<TranscribedEvent>& events, const TranscribedEvent& candidate)
    {
        auto existing = std::find_if(events.begin(), events.end(), [&](const TranscribedEvent& event)
        {
            return event.lane == candidate.lane && event.step == candidate.step;
        });

        if (existing == events.end())
        {
            events.push_back(candidate);
            return;
        }

        if (candidate.confidence > existing->confidence)
            *existing = candidate;
    }
};
} // namespace bbg