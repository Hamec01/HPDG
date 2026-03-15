#include "Drill808Generator.h"

#include <algorithm>
#include <array>

namespace bbg
{
namespace
{
enum class BassPhraseRole
{
    Anchor = 0,
    Support,
    Pickup,
    Ending,
    Response
};

const std::array<int, 7>& intervalsForScale(int scaleMode)
{
    static const std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
    static const std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
    static const std::array<int, 7> harmonicMinor { 0, 2, 3, 5, 7, 8, 11 };
    if (scaleMode == 1)
        return major;
    if (scaleMode == 2)
        return harmonicMinor;
    return minor;
}

BassPhraseRole classifyBassRole(int stepInBar, DrillPhraseRole phraseRole)
{
    if (stepInBar == 0 || stepInBar == 10)
        return BassPhraseRole::Anchor;
    if (stepInBar == 15 || phraseRole == DrillPhraseRole::Ending)
        return BassPhraseRole::Ending;
    if (stepInBar <= 2 || stepInBar == 9)
        return BassPhraseRole::Pickup;
    if (stepInBar == 4 || stepInBar == 12)
        return BassPhraseRole::Response;
    return BassPhraseRole::Support;
}

int findScaleDegree(const std::array<int, 7>& scale, int semitone)
{
    for (size_t i = 0; i < scale.size(); ++i)
        if (scale[i] == semitone)
            return static_cast<int>(i);
    return 0;
}

std::vector<int> buildDegreePool(const DrillStyleSpec& spec, BassPhraseRole role, int scaleMode)
{
    std::vector<int> pool;
    pool.push_back(0); // Root always present.

    const bool isMinorLike = scaleMode != 1;
    const int third = isMinorLike ? 2 : 2; // 3rd degree index in both major/minor arrays.
    const int fifth = 4;
    const int seventh = isMinorLike ? 6 : 6;

    switch (spec.pitchMotion)
    {
        case DrillPitchMotion::VeryStatic:
            if (role == BassPhraseRole::Ending || role == BassPhraseRole::Response)
                pool.push_back(fifth);
            break;

        case DrillPitchMotion::StaticWithAccentMoves:
            pool.push_back(fifth);
            if (role == BassPhraseRole::Ending || role == BassPhraseRole::Response)
                pool.push_back(third);
            break;

        case DrillPitchMotion::ControlledMovement:
            pool.push_back(fifth);
            pool.push_back(third);
            if (role == BassPhraseRole::Ending)
                pool.push_back(seventh);
            break;

        case DrillPitchMotion::AggressiveControlledMovement:
            pool.push_back(fifth);
            pool.push_back(third);
            pool.push_back(seventh);
            if (role == BassPhraseRole::Pickup)
                pool.push_back(fifth);
            break;
    }

    std::sort(pool.begin(), pool.end());
    pool.erase(std::unique(pool.begin(), pool.end()), pool.end());
    return pool;
}

int chooseFromPool(const std::vector<int>& pool,
                   const GeneratorParams& params,
                   int previousPitch,
                   BassPhraseRole role,
                   std::mt19937& rng)
{
    const int root = std::clamp(params.keyRoot, 0, 11);
    const auto& scale = intervalsForScale(params.scaleMode);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    int chosenDegree = pool.empty() ? 0 : pool.front();
    if (!pool.empty())
    {
        std::uniform_int_distribution<size_t> pick(0, pool.size() - 1);
        chosenDegree = pool[pick(rng)];
    }

    // Keep anchor continuity strong: mostly repeat previous pitch for weight.
    if (previousPitch > 0 && (role == BassPhraseRole::Anchor || role == BassPhraseRole::Support) && chance(rng) < 0.68f)
    {
        const int prevSemitone = (previousPitch - (36 + root)) % 12;
        const int normalized = (prevSemitone + 12) % 12;
        chosenDegree = findScaleDegree(scale, normalized);
    }

    int octaveShift = 0;
    if (role == BassPhraseRole::Ending && chance(rng) < 0.45f)
        octaveShift = -12;
    else if (role == BassPhraseRole::Pickup && chance(rng) < 0.2f)
        octaveShift = 12;

    const int midi = 36 + root + scale[static_cast<size_t>(std::clamp(chosenDegree, 0, 6))] + octaveShift;
    return std::clamp(midi, 24, 60);
}

void cleanRhythmMonophonic(std::vector<NoteEvent>& notes)
{
    if (notes.empty())
        return;

    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.velocity > b.velocity;
    });

    std::vector<NoteEvent> filtered;
    for (const auto& n : notes)
    {
        if (!filtered.empty() && filtered.back().step == n.step)
        {
            if (n.velocity > filtered.back().velocity)
                filtered.back() = n;
            continue;
        }
        filtered.push_back(n);
    }

    for (size_t i = 1; i < filtered.size(); ++i)
    {
        auto& prev = filtered[i - 1];
        const auto& cur = filtered[i];
        const int stepGap = std::max(1, cur.step - prev.step);
        prev.length = std::min(prev.length, stepGap);
    }

    notes = std::move(filtered);
}

void sortByTime(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.microOffset < b.microOffset;
    });
}
}

void Drill808Generator::generateRhythm(TrackState& subTrack,
                                       const TrackState& kickTrack,
                                       const GeneratorParams& params,
                                       const DrillStyleProfile& style,
                                       const DrillStyleSpec& spec,
                                       float subActivity,
                                       const std::vector<DrillPhraseRole>& phrase,
                                       std::mt19937& rng) const
{
    subTrack.notes.clear();

    const int bars = std::max(1, params.bars);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.sub808VelocityMin, style.sub808VelocityMax);

    const float activity = std::clamp(subActivity * style.sub808Activity, 0.1f, 1.0f);
    const float followKick = std::clamp(spec.followKickProbability * activity, 0.1f, 0.96f);
    const float sustain = std::clamp(spec.sustainProbability, 0.08f, 0.92f);
    const float counterGap = std::clamp(spec.counterGapProbability * (spec.preferSparseSpace ? 0.8f : 1.0f), 0.0f, 0.62f);

    for (const auto& k : kickTrack.notes)
    {
        if (k.step < 0 || k.step >= (bars * 16))
            continue;

        const int bar = k.step / 16;
        const int stepInBar = k.step % 16;

        const auto phraseRole = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;
        const bool phraseEdge = stepInBar >= 12;

        float reinforce = followKick;
        if (phraseEdge)
            reinforce += spec.phraseEdgeAnchorProbability * 0.2f;

        int anchorLength = 1;
        if (chance(rng) < sustain)
            anchorLength = (spec.preferSparseSpace && phraseEdge) ? 4 : (spec.rhythmMode == Drill808Mode::PhraseAnchored ? 3 : 2);

        if (chance(rng) < std::clamp(reinforce, 0.1f, 0.98f))
            subTrack.notes.push_back({ 36, k.step, anchorLength, vel(rng), 0, false });

        if (stepInBar < 15 && chance(rng) < std::clamp(counterGap, 0.0f, 0.9f))
            subTrack.notes.push_back({ 36, k.step + 1, 1, std::max(style.sub808VelocityMin, vel(rng) - 10), 0, false });

        if (stepInBar > 1 && chance(rng) < 0.12f * activity)
            subTrack.notes.push_back({ 36, k.step - 1, 1, std::max(style.sub808VelocityMin, vel(rng) - 12), 0, false });

        if (phraseRole == DrillPhraseRole::Ending && chance(rng) < std::clamp(spec.phraseEdgeAnchorProbability, 0.12f, 0.9f))
        {
            const int endStep = std::min(bars * 16 - 1, bar * 16 + 15);
            subTrack.notes.push_back({ 36, endStep, spec.preferSparseSpace ? 3 : 2, std::max(style.sub808VelocityMin, vel(rng) - 6), 0, false });
        }
    }

    cleanRhythmMonophonic(subTrack.notes);
}

void Drill808Generator::assignPitches(TrackState& subTrack,
                                      const GeneratorParams& params,
                                      const DrillStyleProfile& style,
                                      const DrillStyleSpec& spec,
                                      const std::vector<DrillPhraseRole>& phrase,
                                      std::mt19937& rng) const
{
    (void) style;
    if (subTrack.notes.empty())
        return;

    sortByTime(subTrack.notes);
    int previousPitch = -1;

    for (auto& note : subTrack.notes)
    {
        const int bar = std::max(0, note.step / 16);
        const int stepInBar = note.step % 16;
        const auto phraseRole = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;
        const auto bassRole = classifyBassRole(stepInBar, phraseRole);
        const auto pool = buildDegreePool(spec, bassRole, params.scaleMode);
        note.pitch = chooseFromPool(pool, params, previousPitch, bassRole, rng);
        previousPitch = note.pitch;
    }
}

void Drill808Generator::applySlides(TrackState& subTrack,
                                    const DrillStyleSpec& spec,
                                    const std::vector<DrillPhraseRole>& phrase,
                                    std::mt19937& rng) const
{
    if (subTrack.notes.size() < 2)
        return;

    sortByTime(subTrack.notes);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    float slideProbability = 0.14f;
    if (spec.slideMode == DrillSlideMode::Moderate)
        slideProbability = 0.22f;
    else if (spec.slideMode == DrillSlideMode::Aggressive)
        slideProbability = 0.34f;

    for (size_t i = 0; i + 1 < subTrack.notes.size(); ++i)
    {
        auto& current = subTrack.notes[i];
        auto& next = subTrack.notes[i + 1];
        const int gap = next.step - current.step;
        if (gap <= 0 || gap > 3)
            continue;
        if (current.pitch == next.pitch)
            continue;

        const int bar = std::max(0, current.step / 16);
        const int stepInBar = current.step % 16;
        const auto phraseRole = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;
        float gate = slideProbability;
        if (phraseRole == DrillPhraseRole::Ending || stepInBar >= 13)
            gate += 0.1f;

        if (chance(rng) < std::clamp(gate, 0.02f, 0.82f))
        {
            // Overlap keeps existing slide-capable interpretation where host/synth supports glide-by-overlap.
            current.length = std::max(current.length, gap + 1);
        }
    }
}

void Drill808Generator::generate(TrackState& subTrack,
                                 const TrackState& kickTrack,
                                 const GeneratorParams& params,
                                 const DrillStyleProfile& style,
                                 const DrillStyleSpec& spec,
                                 float subActivity,
                                 const std::vector<DrillPhraseRole>& phrase,
                                 std::mt19937& rng) const
{
    generateRhythm(subTrack, kickTrack, params, style, spec, subActivity, phrase, rng);
    assignPitches(subTrack, params, style, spec, phrase, rng);
    applySlides(subTrack, spec, phrase, rng);
    cleanRhythmMonophonic(subTrack.notes);
}
} // namespace bbg
