#include "TrapEngine.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <optional>
#include <random>

#include "../Core/TrackRegistry.h"
#include "../Core/Sub808Types.h"
#include "HiResTiming.h"
#include "PatternPerformanceTransformEngine.h"
#include "StyleInfluence.h"
#include "StyleDefaults.h"

namespace bbg
{
namespace
{
TrackState* findTrack(PatternProject& project, TrackType type)
{
    for (auto& t : project.tracks)
        if (t.type == type)
            return &t;
    return nullptr;
}

int stepIndexOf(const NoteEvent& note) { return note.gridTick / HiResTiming::kTicks1_16; }
int tickForStep(int step) { return step * HiResTiming::kTicks1_16; }
// Algebra notes carry their micro-timing inside gridTick, so a note played slightly early
// would floor into the previous step. Collision checks tolerate an early push of up to a
// 1/64 (late pocket and swing already stay on their own step).
int nearestStepOf(const NoteEvent& note) { return (note.gridTick + HiResTiming::kTicks1_16 / 4) / HiResTiming::kTicks1_16; }

juce::String roleForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick: return "trap_kick";
        case TrackType::Sub808: return "trap_sub";
        case TrackType::HiHat: return "trap_hat";
        case TrackType::HatFX: return "trap_hat_fx";
        case TrackType::Snare: return "trap_backbeat";
        case TrackType::ClapGhostSnare: return "trap_layer";
        case TrackType::OpenHat: return "trap_open";
        case TrackType::GhostKick: return "trap_support";
        case TrackType::Cymbal: return "trap_marker";
        case TrackType::Perc: return "trap_texture";
        case TrackType::Ride: return "trap_ride";
        default: return "trap_lane";
    }
}

void dedupeAndSort(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.gridTick != b.gridTick)
            return a.gridTick < b.gridTick;
        if (a.timingOffsetTicks != b.timingOffsetTicks)
            return a.timingOffsetTicks < b.timingOffsetTicks;
        if (a.pitch != b.pitch)
            return a.pitch < b.pitch;
        return a.velocity > b.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        return a.gridTick == b.gridTick && a.timingOffsetTicks == b.timingOffsetTicks && a.pitch == b.pitch;
    }), notes.end());
}


void expandCoupledTracks(TrackType trigger, std::unordered_set<TrackType>& tracks)
{
    if (trigger == TrackType::HiHat || trigger == TrackType::HatFX)
    {
        tracks.insert(TrackType::HiHat);
        tracks.insert(TrackType::HatFX);
    }

    if (trigger == TrackType::Kick || trigger == TrackType::Sub808)
    {
        tracks.insert(TrackType::Kick);
        tracks.insert(TrackType::Sub808);
    }
}

int nearest16th(int ppqTick)
{
    return static_cast<int>(std::lround(static_cast<double>(ppqTick) / HiResTiming::kTicks1_16));
}

std::unordered_set<int> kickSteps(PatternProject& project)
{
    std::unordered_set<int> steps;
    if (const auto* kick = findTrack(project, TrackType::Kick); kick != nullptr)
        for (const auto& n : kick->notes)
            if (!n.isGhost)
                steps.insert(nearest16th(n.gridTick));
    return steps;
}

// RG / mutate on the 808 keeps the kick (maintainer, 2026-10-14): of the engine's candidates the one
// whose kick is closest to the written kick gives the 808 rhythm.
const TrapAlgebraPattern& closestToKicks(const std::vector<TrapAlgebraPattern>& candidates, const TrapAlgebraPattern& selected,
                                         const std::unordered_set<int>& kicks)
{
    const TrapAlgebraPattern* best = &selected;
    float bestScore = -1.0f;
    for (const auto& c : candidates)
    {
        if (!c.acceptedByThreshold)
            continue;
        std::unordered_set<int> ck;
        for (const auto& n : c.matrix.notesForLane(TrapAlgebraLanes::Kick))
            ck.insert(n.barIndex * 16 + (n.tick64 % 64) / 4);
        int shared = 0;
        for (const int k : ck)
            shared += kicks.count(k) > 0 ? 1 : 0;
        const auto unionSize = ck.size() + kicks.size() - static_cast<size_t>(shared);
        const float score = (unionSize > 0 ? static_cast<float>(shared) / static_cast<float>(unionSize) : 1.0f) + 0.1f * c.selectionQuality;
        if (score > bestScore)
        {
            bestScore = score;
            best = &c;
        }
    }
    return *best;
}

// An 808 start a 16th off a written kick moves onto it (the kick it was written against).
void snap808ToKicks(TrackState& sub, const std::unordered_set<int>& kicks)
{
    std::unordered_set<int> taken;
    for (const auto& n : sub.notes)
        taken.insert(nearest16th(n.gridTick));
    for (auto& n : sub.notes)
    {
        const int step = nearest16th(n.gridTick);
        if (kicks.count(step) > 0)
            continue;
        for (const int target : { step - 1, step + 1 })
            if (kicks.count(target) > 0 && taken.count(target) == 0)
            {
                taken.erase(step);
                taken.insert(target);
                n.gridTick = target * HiResTiming::kTicks1_16;
                n.timingOffsetTicks = 0;
                break;
            }
    }
}

void applyTrapStyleInfluence(PatternProject& project)
{
    juce::String applyError;
    TrapStyleInfluence::apply(project, &applyError);
    juce::ignoreUnused(applyError);
}

void clampLowEndStartsPerBar(TrackState& sub, int bars, int maxStartsPerBar)
{
    for (int bar = 0; bar < bars; ++bar)
    {
        std::vector<size_t> indices;
        for (size_t i = 0; i < sub.notes.size(); ++i)
            if ((stepIndexOf(sub.notes[i]) / 16) == bar)
                indices.push_back(i);

        if (static_cast<int>(indices.size()) <= maxStartsPerBar)
            continue;

        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b)
        {
            const int scoreA = sub.notes[a].velocity + (stepIndexOf(sub.notes[a]) % 16 >= 12 ? 6 : 0);
            const int scoreB = sub.notes[b].velocity + (stepIndexOf(sub.notes[b]) % 16 >= 12 ? 6 : 0);
            return scoreA > scoreB;
        });

        for (size_t i = static_cast<size_t>(maxStartsPerBar); i < indices.size(); ++i)
            sub.notes[indices[i]].gridTick = -1;
    }

    sub.notes.erase(std::remove_if(sub.notes.begin(), sub.notes.end(), [](const NoteEvent& n)
    {
        return n.gridTick < 0;
    }), sub.notes.end());
}

void clampLowEndPitchChanges(TrackState& sub, int bars, int maxPitchChangesPerTwoBars)
{
    for (int window = 0; window < std::max(1, (bars + 1) / 2); ++window)
    {
        const int startTick = tickForStep(window * 32);
        const int endTick = tickForStep(std::min(bars * 16, window * 32 + 32));
        int changes = 0;
        int prevPitch = -1;

        for (auto& note : sub.notes)
        {
            if (note.gridTick < startTick || note.gridTick >= endTick)
                continue;

            if (prevPitch > 0 && note.pitch != prevPitch)
            {
                if (changes >= maxPitchChangesPerTwoBars)
                    note.pitch = prevPitch;
                else
                    ++changes;
            }
            prevPitch = note.pitch;
        }
    }
}

// The 808 line's pitches, written over the whole line once its rhythm is final (2026-10-14,
// docs/audit/TRAP_STAGE.md step 7). The old per-note rule gave root / fifth / octave by position:
// fifths were 40 % of the moves (packs 6 %), two distinct pitches a line (packs 4), a 7-semitone span
// (packs 14). Reference: 70 808 / bass MIDI lines of the maintainer's trap packs
// (tools/trap_pack_reference.py): each bar starts on the tonic 59 %, else on iv 9 %, v 8 %, bVII 5 %,
// bVI / III; inside a bar the 808 repeats 44 %, jumps an octave 10 % (+ 8 % beyond), thirds 13 %,
// steps 9 %, fourths 8 %; half the notes sit an octave above the line's lowest; the 2-bar phrase
// comes back (bar = 2 before 25 %); 12 % of the notes glide into another pitch.
void assignTrap808Line(TrackState& sub, const GeneratorParams& params, int bars)
{
    auto& notes = sub.notes;
    if (notes.empty())
        return;
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });

    std::uint32_t hash = static_cast<std::uint32_t>(params.seed) * 2654435761u + static_cast<std::uint32_t>(params.keyRoot) * 97u;
    for (const auto& n : notes)
        hash = (hash ^ static_cast<std::uint32_t>(n.gridTick + 7)) * 16777619u;
    std::mt19937 rng(hash);
    auto unit = [&rng] { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };
    auto pick = [&](std::initializer_list<std::pair<int, float>> options)
    {
        float total = 0.0f;
        for (const auto& o : options)
            total += o.second;
        float r = unit() * total;
        for (const auto& o : options)
        {
            if (r < o.second)
                return o.first;
            r -= o.second;
        }
        return options.begin()->first;
    };

    const bool major = params.scaleMode == 1;
    const int root = ((params.keyRoot % 12) + 12) % 12;
    const int base = 24 + root; // C1-B1
    auto fold = [](int pitch)
    {
        while (pitch > 47)
            pitch -= 12;
        while (pitch < 24)
            pitch += 12;
        return pitch;
    };
    auto barDegreeMove = [&]
    {
        return major ? pick({ { 0, 0.30f }, { 5, 0.22f }, { 7, 0.18f }, { 9, 0.12f }, { 2, 0.08f }, { 4, 0.10f } })
                     : pick({ { 0, 0.30f }, { 5, 0.20f }, { 7, 0.15f }, { 8, 0.15f }, { 10, 0.12f }, { 3, 0.08f } });
    };
    std::vector<int> barDegree(static_cast<size_t>(std::max(1, bars)), 0);
    for (int bar = 0; bar < bars; ++bar)
    {
        const int inPhrase = bar % 4;
        if (inPhrase == 0)
            barDegree[static_cast<size_t>(bar)] = 0;
        else if (inPhrase == 2)
            barDegree[static_cast<size_t>(bar)] = unit() < 0.85f ? 0 : barDegreeMove();
        else if (inPhrase == 3 && bar >= 2 && unit() < 0.5f)
            barDegree[static_cast<size_t>(bar)] = barDegree[static_cast<size_t>(bar - 2)];
        else
            barDegree[static_cast<size_t>(bar)] = barDegreeMove();
    }
    // Scale tones near the bar's degree for a move inside the bar (third, fourth, step, fifth).
    const std::array<int, 7> scale = major ? std::array<int, 7> { 0, 2, 4, 5, 7, 9, 11 } : std::array<int, 7> { 0, 2, 3, 5, 7, 8, 10 };
    auto inScale = [&](int degree) { return std::find(scale.begin(), scale.end(), ((degree % 12) + 12) % 12) != scale.end(); };

    int previous = -1;
    int previousBar = -1;
    // The line sits low (the root octave) or high (an octave up) per 2-bar phrase half the time.
    for (size_t i = 0; i < notes.size(); ++i)
    {
        auto& note = notes[i];
        const int step = note.gridTick / HiResTiming::kTicks1_16;
        const int bar = std::clamp(step / 16, 0, bars - 1);
        const int local = step % 16;
        const int degreePitch = fold(base + barDegree[static_cast<size_t>(bar)]);
        int pitch = degreePitch;
        if (bar != previousBar || previous < 0)
        {
            // The bar's first note is its degree, in the octave nearer the previous note.
            if (previous >= 0 && std::abs(degreePitch + 12 - previous) < std::abs(degreePitch - previous) && degreePitch + 12 <= 47)
                pitch = degreePitch + 12;
        }
        else
        {
            const float r = unit();
            if (r < 0.50f)
                pitch = previous;
            else if (r < 0.62f)
                pitch = previous + 12 <= 47 && (previous - 12 < 24 || unit() < 0.6f) ? previous + 12 : previous - 12;
            else if (r < 0.90f)
            {
                int interval = pick({ { 3, 0.36f }, { 4, 0.12f }, { 5, 0.12f }, { 2, 0.16f }, { -2, 0.12f }, { 7, 0.12f } });
                if (!inScale(barDegree[static_cast<size_t>(bar)] + interval))
                    interval += interval > 0 ? 1 : -1;
                pitch = fold(degreePitch + interval);
                // Now and then the move lands an octave up (a leap over the octave, 8 % in the packs).
                if (pitch + 12 <= 47 && std::abs(pitch + 12 - previous) > 12 && std::abs(pitch + 12 - previous) <= 19 && unit() < 0.40f)
                    pitch += 12;
            }
            else
                pitch = degreePitch != previous ? degreePitch : fold(degreePitch + 12);
        }
        // A pickup in the bar's last beat leads into the next bar's degree.
        const bool lastInBar = i + 1 >= notes.size() || (notes[i + 1].gridTick / HiResTiming::kTicks1_16) / 16 != bar;
        if (lastInBar && local >= 12 && bar + 1 < bars && barDegree[static_cast<size_t>(bar + 1)] != barDegree[static_cast<size_t>(bar)]
            && unit() < 0.45f)
            pitch = fold(base + barDegree[static_cast<size_t>(bar + 1)] + (previous >= 36 ? 12 : 0));
        note.pitch = std::clamp(pitch, 24, 47);
        note.glideToNext = false;
        note.isSlide = false;
        previous = note.pitch;
        previousBar = bar;
    }

    // Glides: a pitch change the 808 reaches by sliding (the note rings into the next one).
    for (size_t i = 0; i + 1 < notes.size(); ++i)
    {
        auto& a = notes[i];
        auto& b = notes[i + 1];
        const int gap = b.gridTick - (a.gridTick + a.lengthTicks);
        if (a.pitch == b.pitch || gap > 4 * HiResTiming::kTicks1_16 || std::abs(a.pitch - b.pitch) > 12)
            continue;
        if (unit() < (std::abs(a.pitch - b.pitch) == 12 ? 0.33f : 0.20f))
        {
            a.lengthTicks = std::max(a.lengthTicks, b.gridTick - a.gridTick);
            a.glideToNext = true;
            b.isSlide = true;
        }
    }
}

void ensureLowEndPhraseEnding(TrackState& sub, int bars)
{
    if (sub.notes.empty())
        return;

    const int finalBarStart = (bars - 1) * 16;
    const int finalBarStartTick = tickForStep(finalBarStart);
    const bool hasEnding = std::any_of(sub.notes.begin(), sub.notes.end(), [finalBarStartTick](const NoteEvent& n)
    {
        return n.gridTick >= finalBarStartTick + 12 * HiResTiming::kTicks1_16;
    });

    if (!hasEnding)
    {
        const int velocity = std::clamp(sub.notes.back().velocity, 84, 120);
        NoteEvent note;
        note.pitch = std::clamp(sub.notes.back().pitch, 24, 60);
        note.gridTick = tickForStep(finalBarStart + 14);
        note.lengthTicks = 2 * HiResTiming::kTicks1_16;
        note.velocity = velocity;
        sub.notes.push_back(note);
    }
}

bool hasStrongNoteAtStep(const TrackState* track, int step)
{
    if (track == nullptr)
        return false;

    return std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& n)
    {
        return !n.isGhost && nearestStepOf(n) == step;
    });
}

void moveSubOffBackbeat(TrackState& sub,
                        const TrackState* snare,
                        const TrackState* clap,
                        int bars)
{
    for (auto& note : sub.notes)
    {
        const int step = nearestStepOf(note);
        const bool backbeatCollision = hasStrongNoteAtStep(snare, step) || hasStrongNoteAtStep(clap, step);
        if (!backbeatCollision)
            continue;

        const int backStep = std::max(0, step - 1);
        const int fwdStep = std::min(bars * 16 - 1, step + 1);
        const bool backFree = !hasStrongNoteAtStep(snare, backStep) && !hasStrongNoteAtStep(clap, backStep);
        const bool fwdFree = !hasStrongNoteAtStep(snare, fwdStep) && !hasStrongNoteAtStep(clap, fwdStep);

        if (backFree)
            note.gridTick = tickForStep(backStep);
        else if (fwdFree)
            note.gridTick = tickForStep(fwdStep);
        else
            note.velocity = std::max(1, note.velocity - 12);
    }
}

void pruneHatFxOverLowEndAnchors(TrackState& hatFx,
                                 const TrackState* kick,
                                 const TrackState* sub)
{
    if (kick == nullptr || sub == nullptr)
        return;

    hatFx.notes.erase(std::remove_if(hatFx.notes.begin(), hatFx.notes.end(), [&](const NoteEvent& n)
    {
        const int step = nearestStepOf(n);
        const int stepInBar = ((step % 16) + 16) % 16;
        if (!(stepInBar == 0 || stepInBar == 8 || stepInBar == 10 || stepInBar >= 14))
            return false;

        const bool kickHere = std::any_of(kick->notes.begin(), kick->notes.end(), [&](const NoteEvent& k)
        {
            return !k.isGhost && nearestStepOf(k) == step;
        });
        const bool subHere = std::any_of(sub->notes.begin(), sub->notes.end(), [&](const NoteEvent& s)
        {
            return !s.isGhost && nearestStepOf(s) == step;
        });

        return kickHere && subHere && n.velocity >= 86;
    }), hatFx.notes.end());
}

std::optional<TrackType> trackTypeForTrapAlgebraLane(int lane)
{
    switch (lane)
    {
        case TrapAlgebraLanes::HiHat: return TrackType::HiHat;
        case TrapAlgebraLanes::HatAccent: return TrackType::HatFX;
        case TrapAlgebraLanes::OpenHat: return TrackType::OpenHat;
        case TrapAlgebraLanes::Snare: return TrackType::Snare;
        case TrapAlgebraLanes::ClapGhost: return TrackType::ClapGhostSnare;
        case TrapAlgebraLanes::Kick: return TrackType::Kick;
        case TrapAlgebraLanes::KickGhost: return TrackType::GhostKick;
        case TrapAlgebraLanes::Ride: return TrackType::Ride;
        case TrapAlgebraLanes::Cymbal: return TrackType::Cymbal;
        case TrapAlgebraLanes::Perc: return TrackType::Perc;
        case TrapAlgebraLanes::Sub808: return TrackType::Sub808;
        default: return std::nullopt;
    }
}

int pitchForTrapAlgebraLane(int lane)
{
    switch (lane)
    {
        case TrapAlgebraLanes::HiHat: return 42;
        case TrapAlgebraLanes::HatAccent: return 44;
        case TrapAlgebraLanes::OpenHat: return 46;
        case TrapAlgebraLanes::Snare: return 38;
        case TrapAlgebraLanes::ClapGhost: return 39;
        case TrapAlgebraLanes::Kick: return 36;
        case TrapAlgebraLanes::KickGhost: return 35;
        case TrapAlgebraLanes::Ride: return 51;
        case TrapAlgebraLanes::Cymbal: return 49;
        case TrapAlgebraLanes::Perc: return 50;
        case TrapAlgebraLanes::Sub808: return 36;
        default: return 36;
    }
}

int phrase808PitchForTrapAlgebraNote(const TrapAlgebraNote& note, const GeneratorParams& params)
{
    if (note.laneIndex != TrapAlgebraLanes::Sub808)
        return pitchForTrapAlgebraLane(note.laneIndex);

    // The root in octave 1 (C1-B1, 33-62 Hz), answers up to A2: the same window as the sample-led
    // 808 (SampleBassLineComposer). Since the lane plays notes as written (2026-10-13) the old
    // window (root E1-E2, answers to C3) sounded a fifth above trap references: 1289 pack 808
    // loops have a median of F1 (44 Hz), D1-A#1 between the quartiles; HPDG had C2 (65 Hz), and
    // keys C-D# put the whole line in octave 2 (the 808 sample sped up 2-4x: short, thin, clicky).
    const int root = ((params.keyRoot % 12) + 12) % 12;
    const int base = 24 + root;

    const bool major = params.scaleMode == 1;
    const int third = major ? 4 : 3;
    const int local = note.tick64 % 64;
    const int bar = note.barIndex % 4;

    int interval = 0;
    if (note.role == TrapAlgebraRole::BassAnswer)
        interval = ((note.tick64 / 8 + params.seed) % 3 == 0) ? 12 : 7;
    else if (note.role == TrapAlgebraRole::BassPickup)
        interval = 7;
    else if (note.role == TrapAlgebraRole::BassAnchor)
        interval = 0;
    else if (bar == 1 && (local == 24 || local == 36))
        interval = 7;
    else if (bar == 2 && local >= 32)
        interval = 7;
    else if (bar == 3 && local >= 56)
        interval = 12;
    else if (params.trapSubstyle == 5 && local == 40)
        interval = third;

    int pitch = base + interval;
    while (pitch > 45)
        pitch -= 12;
    while (pitch < 24)
        pitch += 12;
    return std::clamp(pitch, 24, 45);
}

TrapAlgebraSubstyle trapAlgebraSubstyleForProject(int trapSubstyle)
{
    switch (trapSubstyle)
    {
        case 1: return TrapAlgebraSubstyle::DarkTrap;
        case 2: return TrapAlgebraSubstyle::CloudTrap;
        case 3: return TrapAlgebraSubstyle::RageTrap;
        case 4: return TrapAlgebraSubstyle::MemphisTrap;
        case 5: return TrapAlgebraSubstyle::LuxuryTrap;
        case 0:
        default: return TrapAlgebraSubstyle::ATLClassic;
    }
}

juce::String semanticRoleForTrapAlgebraNote(const TrapAlgebraNote& note)
{
    return juce::String("trap_algebra_") + note.roleString;
}
}

TrapEngine::TrapEngine() = default;

void TrapEngine::generate(PatternProject& project)
{
    applyTrapStyleInfluence(project);
    const auto& style = getTrapProfile(project.params.trapSubstyle);

    std::unordered_set<TrackType> mutableTracks;
    for (auto& track : project.tracks)
    {
        if (track.locked || !track.enabled)
            continue;

        track.templateId += 1;
        track.variationId = 0;
        track.mutationDepth = 0.0f;
        track.subProfile = style.name;
        track.laneRole = roleForTrack(track.type);
        mutableTracks.insert(track.type);
    }

    TrapAlgebraParams algebraParams;
    algebraParams.seed = project.params.seed + 601; // seed + settings decide a Generate (RULE 13)
    algebraParams.bars = std::max(1, project.params.bars);
    algebraParams.bpm = project.params.bpm;
    algebraParams.density = project.params.densityAmount;
    algebraParams.swing = std::clamp(project.params.swingPercent / 100.0f, 0.50f, 0.60f);
    algebraParams.humanize = project.params.humanizeAmount;
    algebraParams.variation = std::max(project.params.velocityAmount, project.params.timingAmount);
    algebraParams.temperature = 0.40f;
    algebraParams.qMin = 0.62f;
    algebraParams.candidateCount = 32;
    algebraParams.substyle = trapAlgebraSubstyleForProject(project.params.trapSubstyle);

    const auto algebraPattern = TrapAlgebraEngine().generate(algebraParams);
    applyTrapAlgebraPattern(project, algebraPattern, mutableTracks);

    juce::ignoreUnused(style);
    validatePattern(project, mutableTracks);
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void TrapEngine::regenerateTrack(PatternProject& project, TrackType trackType)
{
    applyTrapStyleInfluence(project);
    regenerateTrackVariation(project, trackType);
}

void TrapEngine::generateTrackNew(PatternProject& project, TrackType trackType)
{
    applyTrapStyleInfluence(project);
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked)
        return;

    const auto& style = getTrapProfile(project.params.trapSubstyle);

    std::unordered_set<TrackType> mutableTracks { trackType };
    expandCoupledTracks(trackType, mutableTracks);
    const bool bassOnly = trackType == TrackType::Sub808;
    if (bassOnly)
        mutableTracks.erase(TrackType::Kick);
    for (auto it = mutableTracks.begin(); it != mutableTracks.end();)
    {
        const auto* coupled = findTrack(project, *it);
        if (*it != trackType && (coupled == nullptr || coupled->locked || !coupled->enabled))
            it = mutableTracks.erase(it);
        else
            ++it;
    }

    TrapAlgebraParams algebraParams;
    algebraParams.seed = project.params.seed + static_cast<int>(trackType) * 37 + project.generationCounter * 19 + 509;
    algebraParams.bars = std::max(1, project.params.bars);
    algebraParams.bpm = project.params.bpm;
    algebraParams.density = project.params.densityAmount;
    algebraParams.swing = std::clamp(project.params.swingPercent / 100.0f, 0.50f, 0.60f);
    algebraParams.humanize = project.params.humanizeAmount;
    algebraParams.variation = std::max(project.params.velocityAmount, project.params.timingAmount);
    algebraParams.temperature = 0.40f;
    algebraParams.qMin = 0.62f;
    algebraParams.candidateCount = 24;
    algebraParams.substyle = trapAlgebraSubstyleForProject(project.params.trapSubstyle);

    std::vector<TrapAlgebraPattern> candidates;
    const auto algebraPattern = TrapAlgebraEngine().generate(algebraParams, bassOnly ? &candidates : nullptr);
    const auto kicks = kickSteps(project);
    applyTrapAlgebraPattern(project, bassOnly ? closestToKicks(candidates, algebraPattern, kicks) : algebraPattern, mutableTracks);
    if (bassOnly)
        if (auto* sub = findTrack(project, TrackType::Sub808); sub != nullptr)
            snap808ToKicks(*sub, kicks);

    juce::ignoreUnused(style);
    validatePattern(project, mutableTracks);
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void TrapEngine::regenerateTrackVariation(PatternProject& project, TrackType trackType)
{
    auto* target = findTrack(project, trackType);
    if (target == nullptr || target->locked || !target->enabled)
        return;

    generateTrackNew(project, trackType);

    target = findTrack(project, trackType);
    if (target == nullptr)
        return;

    dedupeAndSort(target->notes);
    target->variationId += 1;
    target->mutationDepth = std::clamp(target->mutationDepth + 0.10f, 0.0f, 1.0f);

    PatternPerformanceTransformEngine::captureBasePatterns(project, { trackType });
}

void TrapEngine::mutatePattern(PatternProject& project)
{
    generate(project);
}

void TrapEngine::mutateTrack(PatternProject& project, TrackType trackType)
{
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked || !track->enabled)
        return;

    generateTrackNew(project, trackType);
}

void TrapEngine::validatePattern(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks) const
{
    const int bars = std::max(1, project.params.bars);

    const auto isEditable = [&mutableTracks](const TrackState* track)
    {
        return track != nullptr && !track->locked && mutableTracks.count(track->type) != 0;
    };

    for (auto& track : project.tracks)
    {
        if (!isEditable(&track))
            continue;

        dedupeAndSort(track.notes);
        for (auto& note : track.notes)
        {
            note.gridTick = std::clamp(note.gridTick, 0, bars * 16 * HiResTiming::kTicks1_16 - 1);
            note.lengthTicks = std::max(HiResTiming::kTicks1_16, note.lengthTicks);
            note.velocity = std::clamp(note.velocity, 1, 127);
            note.timingOffsetTicks = std::clamp(note.timingOffsetTicks, -120, 120);
        }

        int maxHits = bars * 10;
        if (track.type == TrackType::HiHat)
            maxHits = bars * 56;
        else if (track.type == TrackType::HatFX)
            maxHits = bars * 88;
        else if (track.type == TrackType::Sub808)
            maxHits = bars * 10;
        else if (track.type == TrackType::OpenHat)
            maxHits = bars * 4;

        if (static_cast<int>(track.notes.size()) > maxHits)
            track.notes.resize(static_cast<size_t>(maxHits));
    }

    auto* kick = findTrack(project, TrackType::Kick);
    const auto* snare = findTrack(project, TrackType::Snare);
    const auto* clap = findTrack(project, TrackType::ClapGhostSnare);
    if (isEditable(kick))
    {
        kick->notes.erase(std::remove_if(kick->notes.begin(), kick->notes.end(), [snare, clap](const NoteEvent& k)
        {
            const bool onSnare = snare != nullptr && std::any_of(snare->notes.begin(), snare->notes.end(), [&k](const NoteEvent& s)
            {
                return !s.isGhost && nearestStepOf(s) == nearestStepOf(k);
            });

            const bool onClap = clap != nullptr && std::any_of(clap->notes.begin(), clap->notes.end(), [&k](const NoteEvent& c)
            {
                return !c.isGhost && nearestStepOf(c) == nearestStepOf(k);
            });

            return onSnare || onClap;
        }), kick->notes.end());
    }

    auto* sub = findTrack(project, TrackType::Sub808);
    if (isEditable(sub))
    {
        dedupeAndSort(sub->notes);
        moveSubOffBackbeat(*sub, snare, clap, bars);
        const bool algebraSub = std::any_of(sub->notes.begin(), sub->notes.end(), [](const NoteEvent& note)
        {
            return note.semanticRole.startsWith("trap_algebra_");
        });
        clampLowEndStartsPerBar(*sub, bars, algebraSub ? 4 : 3);
        if (algebraSub)
            assignTrap808Line(*sub, project.params, bars);
        clampLowEndPitchChanges(*sub, bars, 3);
        if (!algebraSub)
            ensureLowEndPhraseEnding(*sub, bars);
        dedupeAndSort(sub->notes);
        sub->sub808Notes = toSub808NoteEvents(sub->notes);
    }

    auto* hatFx = findTrack(project, TrackType::HatFX);
    if (isEditable(hatFx))
    {
        pruneHatFxOverLowEndAnchors(*hatFx, kick, sub);
        dedupeAndSort(hatFx->notes);
    }
}

void TrapEngine::applyTrapAlgebraPattern(PatternProject& project,
                                         const TrapAlgebraPattern& pattern,
                                         const std::unordered_set<TrackType>& mutableTracks) const
{
    const int bars = std::max(1, project.params.bars);

    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0 || track.locked || !track.enabled)
            continue;

        track.notes.clear();
        if (track.type == TrackType::Sub808)
            track.sub808Notes.clear();
    }

    for (const auto& note : pattern.matrix.allNotes())
    {
        const auto type = trackTypeForTrapAlgebraLane(note.laneIndex);
        if (!type.has_value() || mutableTracks.count(*type) == 0)
            continue;

        auto* track = findTrack(project, *type);
        if (track == nullptr || track->locked || !track->enabled)
            continue;

        const int ppqTick = note.tick64 * HiResTiming::kTicks1_64 + note.microTimingTicks;
        const int lengthSteps = std::max(1, static_cast<int>(std::ceil(static_cast<float>(note.durationTicks) / 4.0f)));
        const bool isGhost = note.role == TrapAlgebraRole::Ghost || note.laneIndex == TrapAlgebraLanes::KickGhost || note.laneIndex == TrapAlgebraLanes::ClapGhost;

        HiResTiming::addNoteAtTick(*track,
                                   phrase808PitchForTrapAlgebraNote(note, project.params),
                                   ppqTick,
                                   note.velocity,
                                   isGhost,
                                   bars,
                                   lengthSteps);

        if (!track->notes.empty())
            track->notes.back().semanticRole = semanticRoleForTrapAlgebraNote(note);
    }

    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0)
            continue;

        dedupeAndSort(track.notes);
        if (track.type == TrackType::Sub808)
            track.sub808Notes = toSub808NoteEvents(track.notes);
    }
}
} // namespace bbg
