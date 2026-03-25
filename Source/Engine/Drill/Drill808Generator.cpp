#include "Drill808Generator.h"

#include <algorithm>
#include <array>

#include "../../Core/PatternProject.h"
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

std::vector<int> buildDegreePool(const Drill808StyleSpec& spec, BassPhraseRole role, int scaleMode)
{
    std::vector<int> pool;
    pool.push_back(0); // Root always present.

    const bool isMinorLike = scaleMode != 1;
    const int third = isMinorLike ? 2 : 2; // 3rd degree index in both major/minor arrays.
    const int fifth = 4;
    const int seventh = isMinorLike ? 6 : 6;

    switch (spec.pitchMotion)
    {
        case Drill808PitchMotion::VeryStatic:
            if (role == BassPhraseRole::Ending || role == BassPhraseRole::Response)
                pool.push_back(fifth);
            break;

        case Drill808PitchMotion::ControlledMovement:
            pool.push_back(fifth);
            pool.push_back(third);
            if (role == BassPhraseRole::Ending)
                pool.push_back(seventh);
            break;

        case Drill808PitchMotion::AggressiveControlledMovement:
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

float subBalanceWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(laneBiasFor(styleInfluence, TrackType::Sub808).balanceWeight, 0.65f, 1.6f);
}

float lowEndCouplingWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.lowEndCouplingWeight, 0.65f, 1.6f);
}
}

void Drill808Generator::generateRhythm(TrackState& subTrack,
                                       const TrackState& kickTrack,
                                       const GeneratorParams& params,
                                       const DrillStyleProfile& style,
                                       const Drill808StyleSpec& spec,
                                       const StyleInfluenceState& styleInfluence,
                                       float subActivity,
                                       const std::vector<DrillPhraseRole>& phrase,
                                       const DrillGrooveBlueprint* blueprint,
                                       std::mt19937& rng) const
{
    subTrack.notes.clear();

    const int bars = std::max(1, params.bars);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.sub808VelocityMin, style.sub808VelocityMax);

    const float activity = std::clamp(subActivity * style.sub808Activity * subBalanceWeight(styleInfluence) * lowEndCouplingWeight(styleInfluence), 0.1f, 1.0f);
    const float anchorFollow = std::clamp(spec.anchorFollowProbability * activity, 0.06f, 0.96f);
    const float supportFollow = std::clamp(spec.supportFollowProbability * activity, 0.04f, 0.9f);
    const float restartGateBase = std::clamp(spec.restartProbability * activity, 0.0f, 0.88f);
    const float denseTopRejectGate = std::clamp(spec.rejectIfDenseTopProbability * style.hatFxIntensity * 0.65f, 0.0f, 0.9f);

    std::vector<int> startsPerBar(static_cast<size_t>(bars), 0);
    std::vector<int> restartsPerWindow(static_cast<size_t>(std::max(1, (bars + 1) / 2)), 0);

    auto hasStartNear = [&](int step, int dist)
    {
        for (const auto& n : subTrack.notes)
            if (std::abs(n.step - step) <= dist)
                return true;
        return false;
    };

    auto addStart = [&](int step, int length, int velocity)
    {
        const int bar = std::clamp(step / 16, 0, bars - 1);
        if (startsPerBar[static_cast<size_t>(bar)] >= spec.maxStartsPerBar)
            return false;
        if (hasStartNear(step, 1))
            return false;

        subTrack.notes.push_back({ 36, step, std::max(1, length), std::clamp(velocity, style.sub808VelocityMin, style.sub808VelocityMax), 0, false });
        startsPerBar[static_cast<size_t>(bar)] += 1;
        return true;
    };

    for (const auto& k : kickTrack.notes)
    {
        if (k.step < 0 || k.step >= (bars * 16))
            continue;

        const int bar = k.step / 16;
        const int stepInBar = k.step % 16;
        const auto* slot = blueprint != nullptr ? blueprint->slotAt(k.step) : nullptr;
        const bool denseTopBar = blueprint != nullptr
            && bar < static_cast<int>(blueprint->barPlans.size())
            && blueprint->barPlans[static_cast<size_t>(bar)].densityBudget >= 0.68f;

        const auto phraseRole = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;
        const bool phraseEdge = stepInBar >= 12;
        const bool anchor = stepInBar == 0 || stepInBar == 10;
        const bool pickup = stepInBar == 15 || stepInBar <= 1;
        const int window = std::clamp(bar / 2, 0, static_cast<int>(restartsPerWindow.size()) - 1);

        float reinforce = anchor ? anchorFollow : supportFollow;
        if (anchor)
            reinforce += 0.10f;
        if (phraseEdge)
            reinforce += spec.edgeAnchorProbability * 0.18f;
        if (pickup)
            reinforce += spec.pickupBassProbability * 0.16f;
        if (!anchor && chance(rng) < denseTopRejectGate)
            reinforce *= 0.7f;
        if (slot != nullptr)
        {
            if (slot->kickEventType == DrillKickEventType::AnchorKick)
                reinforce *= 1.08f;
            else if (slot->kickEventType == DrillKickEventType::PhraseEdgeKick)
                reinforce *= slot->majorEventReserved ? 0.62f : 0.86f;
            else if (slot->kickEventType == DrillKickEventType::SupportKick)
                reinforce *= 0.92f;

            reinforce *= std::clamp(0.45f + slot->subStartWeight, 0.25f, 1.25f);
            if (slot->snareProtection && !anchor)
                reinforce *= 0.36f;
            if (slot->majorEventReserved && slot->phraseEdgeWeight >= 0.7f && !anchor)
                reinforce *= 0.42f;
            if (denseTopBar)
                reinforce *= 0.88f;
        }

        int anchorLength = 2;
        if (chance(rng) < spec.longHoldProbability)
        {
            anchorLength = phraseEdge && chance(rng) < spec.phraseHoldProbability ? 5 : 3;
            if (spec.rhythmMode == Drill808RhythmMode::SparseThreat)
                anchorLength = phraseEdge ? 7 : 5;
            else if (spec.rhythmMode == Drill808RhythmMode::FrontalPressure)
                anchorLength = phraseEdge ? 3 : 2;
        }
        if (slot != nullptr && slot->subHoldPreferred)
            anchorLength = std::max(anchorLength, phraseEdge ? 5 : 4);
        if (denseTopBar)
            anchorLength = std::max(anchorLength, phraseEdge ? 5 : 4);

        if (chance(rng) < std::clamp(reinforce, 0.1f, 0.98f))
            addStart(k.step, anchorLength, vel(rng));

        const bool blueprintRestartAllowed = (slot == nullptr)
            || (slot->subRestartAllowed && (!slot->majorEventReserved || slot->phraseEdgeWeight < 0.7f) && !slot->snareProtection);
        float restartGate = restartGateBase;
        if (denseTopBar)
            restartGate *= 0.52f;
        if (slot != nullptr)
        {
            if (slot->kickEventType == DrillKickEventType::PhraseEdgeKick)
                restartGate *= 0.72f;
            if (slot->subHoldPreferred)
                restartGate *= 0.42f;
            if (slot->majorEventReserved)
                restartGate *= 0.56f;
            if (slot->snareProtection)
                restartGate *= 0.34f;
            if (phraseEdge && !slot->subRestartAllowed)
                restartGate = 0.0f;
        }

        const bool canRestart = restartsPerWindow[static_cast<size_t>(window)] < spec.maxMajorRestartsPer2Bars;
        if (canRestart && stepInBar < 15 && startsPerBar[static_cast<size_t>(bar)] < spec.maxStartsPerBar
            && blueprintRestartAllowed
            && chance(rng) < restartGate)
        {
            const int restartStep = (spec.rhythmMode == Drill808RhythmMode::FrontalPressure && stepInBar > 2)
                ? k.step - 1
                : k.step + 1;
            if (addStart(std::clamp(restartStep, 0, bars * 16 - 1), 1, std::max(style.sub808VelocityMin, vel(rng) - 10)))
                restartsPerWindow[static_cast<size_t>(window)] += 1;
        }

        if (phraseRole == DrillPhraseRole::Ending
            && startsPerBar[static_cast<size_t>(bar)] < spec.maxStartsPerBar
            && (slot == nullptr || slot->subRestartAllowed)
            && chance(rng) < std::clamp(spec.edgeAnchorProbability, 0.08f, 0.92f))
        {
            const int endStep = std::min(bars * 16 - 1, bar * 16 + 15);
            const int hold = chance(rng) < spec.phraseHoldProbability ? 6 : 3;
            addStart(endStep, hold, std::max(style.sub808VelocityMin, vel(rng) - 6));
        }
    }

    // Enforce deterministic starts-per-bar range.
    for (int bar = 0; bar < bars; ++bar)
    {
        while (startsPerBar[static_cast<size_t>(bar)] < spec.minStartsPerBar)
        {
            const int anchorStep = bar * 16 + (startsPerBar[static_cast<size_t>(bar)] == 0 ? 0 : 10);
            if (!addStart(anchorStep,
                          spec.rhythmMode == Drill808RhythmMode::SparseThreat ? 5 : 3,
                          std::max(style.sub808VelocityMin, vel(rng) - 4)))
                break;
        }
    }

    cleanRhythmMonophonic(subTrack.notes);
}

void Drill808Generator::assignPitches(TrackState& subTrack,
                                      const GeneratorParams& params,
                                      const DrillStyleProfile& style,
                                      const Drill808StyleSpec& spec,
                                      const std::vector<DrillPhraseRole>& phrase,
                                      const DrillGrooveBlueprint* blueprint,
                                      std::mt19937& rng) const
{
    (void) style;

    if (subTrack.notes.empty())
        return;

    sortByTime(subTrack.notes);
    const int bars = std::max(1, params.bars);
    const int twoBarWindows = std::max(1, (bars + 1) / 2);
    std::vector<int> pitchChanges(static_cast<size_t>(twoBarWindows), 0);
    int previousPitch = -1;
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    auto weightedPick = [&](BassPhraseRole role)
    {
        float rootW = spec.rootWeight;
        float octaveW = spec.allowOctaveJumps ? spec.octaveWeight : 0.0f;
        float fifthW = spec.fifthWeight;
        float flat7W = spec.allowFlat7 ? spec.flat7Weight : 0.0f;
        float minor3W = spec.allowMinor3 ? spec.minor3Weight : 0.0f;

        if (role == BassPhraseRole::Anchor)
            rootW += 0.12f;
        if (role == BassPhraseRole::Ending && !spec.preferRootOnPhraseEdge)
            octaveW += 0.06f;
        if (role == BassPhraseRole::Pickup && spec.pitchMotion == Drill808PitchMotion::AggressiveControlledMovement)
            fifthW += 0.05f;
        if (role == BassPhraseRole::Ending && spec.preferRootOnPhraseEdge)
        {
            rootW += 0.14f;
            octaveW *= 0.65f;
        }

        const float sum = std::max(0.0001f, rootW + octaveW + fifthW + flat7W + minor3W);
        const float needle = chance(rng) * sum;
        float acc = rootW;
        if (needle <= acc)
            return 0; // root
        acc += octaveW;
        if (needle <= acc)
            return 1; // octave
        acc += fifthW;
        if (needle <= acc)
            return 2; // fifth
        acc += flat7W;
        if (needle <= acc)
            return 3; // flat7
        return 4;     // minor3
    };

    for (auto& note : subTrack.notes)
    {
        const int bar = std::max(0, note.step / 16);
        const int stepInBar = note.step % 16;
        const auto* slot = blueprint != nullptr ? blueprint->slotAt(note.step) : nullptr;
        const bool denseTopBar = blueprint != nullptr
            && bar < static_cast<int>(blueprint->barPlans.size())
            && blueprint->barPlans[static_cast<size_t>(bar)].densityBudget >= 0.68f;
        const auto phraseRole = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;
        const auto bassRole = classifyBassRole(stepInBar, phraseRole);
        const auto pool = buildDegreePool(spec, bassRole, params.scaleMode);
        int targetPitch = chooseFromPool(pool, params, previousPitch, bassRole, rng);

        // Root-heavy weighted motion from the substyle table.
        const int pick = weightedPick(bassRole);
        const int root = std::clamp(params.keyRoot, 0, 11);
        int candidate = 36 + root;
        if (pick == 1)
            candidate += 12;
        else if (pick == 2)
            candidate += 7;
        else if (pick == 3)
            candidate += 10;
        else if (pick == 4)
            candidate += 3;
        candidate = std::clamp(candidate, 24, 60);

        if (chance(rng) < 0.78f)
            targetPitch = candidate;

        const int window = std::clamp(bar / 2, 0, twoBarWindows - 1);
        const int maxPitchChanges = denseTopBar ? std::max(1, spec.maxPitchChangesPer2Bars - 1) : spec.maxPitchChangesPer2Bars;
        if (previousPitch > 0 && targetPitch != previousPitch)
        {
            if (pitchChanges[static_cast<size_t>(window)] >= maxPitchChanges)
                targetPitch = previousPitch;
            else if ((bassRole == BassPhraseRole::Anchor || bassRole == BassPhraseRole::Support) && chance(rng) < 0.58f)
                targetPitch = previousPitch;
            else if ((slot != nullptr && slot->subHoldPreferred) || denseTopBar)
            {
                if (chance(rng) < 0.62f)
                    targetPitch = previousPitch;
                else
                    pitchChanges[static_cast<size_t>(window)] += 1;
            }
            else
                pitchChanges[static_cast<size_t>(window)] += 1;
        }

        note.pitch = std::clamp(targetPitch, 24, 60);
        previousPitch = note.pitch;
    }
}

void Drill808Generator::applySlides(TrackState& subTrack,
                                    const Drill808StyleSpec& spec,
                                    const std::vector<DrillPhraseRole>& phrase,
                                    const DrillGrooveBlueprint* blueprint,
                                    std::mt19937& rng) const
{
    if (subTrack.notes.size() < 2)
        return;

    sortByTime(subTrack.notes);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    float slideProbability = 0.12f;
    if (spec.slideMode == Drill808SlideMode::Moderate)
        slideProbability = 0.22f;
    else if (spec.slideMode == Drill808SlideMode::Strong)
        slideProbability = 0.34f;

    const int windows = std::max(1, static_cast<int>((subTrack.notes.back().step / 16 + 2) / 2));
    std::vector<int> slideCount(static_cast<size_t>(windows), 0);

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
        const auto* slot = blueprint != nullptr ? blueprint->slotAt(current.step) : nullptr;
        const auto phraseRole = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;
        const int window = std::clamp(bar / 2, 0, windows - 1);

        if (slideCount[static_cast<size_t>(window)] >= spec.maxSlidesPer2Bars)
            continue;

        float gate = slideProbability;
        if (phraseRole == DrillPhraseRole::Ending || stepInBar >= 13)
            gate += 0.1f;
        if (spec.slideMode == Drill808SlideMode::Sparse && gap == 1)
            gate *= 0.84f;
        if (phraseRole == DrillPhraseRole::Ending && spec.allowLongSlideAtPhraseEdge)
            gate += 0.08f;
        if (slot != nullptr)
        {
            if (!slot->slideAllowed)
                continue;
            if (slot->subHoldPreferred)
                gate *= 0.68f;
            if (slot->majorEventReserved && slot->phraseEdgeWeight >= 0.7f)
                gate *= 0.56f;
            if (slot->snareProtection)
                gate *= 0.46f;
        }

        if (chance(rng) < std::clamp(gate, 0.02f, 0.82f))
        {
            current.length = std::max(current.length, gap + 1);
            if (phraseRole == DrillPhraseRole::Ending && spec.allowLongSlideAtPhraseEdge)
                current.length = std::max(current.length, gap + 2);

            if (spec.slideMode == Drill808SlideMode::Sparse && phraseRole == DrillPhraseRole::Ending && next.pitch > current.pitch)
                std::swap(current.pitch, next.pitch);

            slideCount[static_cast<size_t>(window)] += 1;
        }
    }
}

void Drill808Generator::generate(TrackState& subTrack,
                                 const TrackState& kickTrack,
                                 const GeneratorParams& params,
                                 const DrillStyleProfile& style,
                                 const Drill808StyleSpec& spec,
                                 const StyleInfluenceState& styleInfluence,
                                 float subActivity,
                                 const std::vector<DrillPhraseRole>& phrase,
                                 const DrillGrooveBlueprint* blueprint,
                                 std::mt19937& rng) const
{
    generateRhythm(subTrack, kickTrack, params, style, spec, styleInfluence, subActivity, phrase, blueprint, rng);
    assignPitches(subTrack, params, style, spec, phrase, blueprint, rng);
    applySlides(subTrack, spec, phrase, blueprint, rng);
    cleanRhythmMonophonic(subTrack.notes);
}
} // namespace bbg
