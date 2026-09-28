# HPDG — Trap Engine 2.0
## Scientific / mathematical generation specification

**Status:** engineering design specification  
**Purpose:** rebuild the shared Trap generation logic without rewriting the editor, transport, APVTS, sample loading or export systems.  
**Primary problem:** when the DAW is synchronized at a low tempo such as **85–86 BPM**, Trap currently behaves literally at 85–86 BPM and therefore sounds unnaturally slow. For Trap, the engine must understand octave-equivalent tempo: **86 BPM can be interpreted as 172 BPM** for the internal rhythmic clock while the DAW transport remains at 86 BPM.

---

# 1. First principle: Trap has two simultaneous clocks

Trap is frequently perceived ambiguously as a slow tempo and its double-time equivalent. A groove at 70–90 BPM can be rhythmically equivalent to a 140–180 BPM programming clock: sparse kick/808 and half-time backbeat coexist with fast hats and ornaments.

Therefore the engine must stop assuming:

```text
host BPM == rhythmic generation BPM
```

Use two independent concepts:

```text
TransportTempo = DAW BPM
StyleTempo     = octave-equivalent BPM used by Trap grammar
```

The DAW tempo is NEVER changed.

The plugin only changes how it interprets the rhythmic subdivision.

Research/production literature describes Trap as strongly dependent on this half-time/double-time ambiguity, with sparse low-end elements contrasted against much faster percussion and hi-hat activity.

---

# 2. Octave-equivalent tempo normalization

Let:

```text
H = host BPM
T = internal Trap style BPM
m = rhythmic clock multiplier
```

Allowed multipliers:

```text
m ∈ {1, 2}
T = H * m
```

For synced low-tempo Trap:

```text
H = 86
m = 2
T = 172
```

Do NOT hard-code only `if BPM == 86`.

Choose the octave-equivalent tempo nearest a profile reference tempo.

Mathematically:

```text
m* = argmin[m ∈ {1,2}] | log2((H*m) / Tref) |
T  = H * m*
```

Recommended shared Trap reference tempo for the first implementation:

```text
Tref ≈ 150–160 BPM
```

The exact reference can later be substyle-specific.

Example with `Tref = 155`:

```text
Host 75  -> 150
Host 80  -> 160
Host 85  -> 170
Host 86  -> 172
Host 90  -> 180
```

This is preferable to a crude “all BPM below X are slow” rule because tempo equivalence is logarithmic: doubling tempo is an octave-like transformation of periodicity.

Suggested implementation:

```cpp
struct TrapTempoContext
{
    double hostBpm = 120.0;
    double styleBpm = 120.0;
    double clockMultiplier = 1.0;
    bool doubleTime = false;
};

TrapTempoContext resolveTrapTempo(
    double hostBpm,
    const TrapStyleProfile& profile);
```

---

# 3. Critical rule: do not speed up the DAW timeline

For `86 -> 172`:

```text
host bar length remains a bar at 86 BPM
```

Do NOT alter:

- host transport;
- playhead;
- exported project length;
- DAW BPM;
- requested number of visible bars.

Instead increase the internal rhythmic clock density.

If:

```text
clockMultiplier = 2
```

then one internal Trap subdivision occupies half the host duration it would occupy at multiplier 1.

For an internal structural tick:

```text
hostPPQPerTrapTick =
    normalPPQPerTick / clockMultiplier
```

With:

```text
PPQ = 960
1 tick64 at normal clock = 960 / 16 = 60 PPQ
```

then at double-time interpretation:

```text
1 Trap tick64 = 30 host PPQ
```

Thus a 1/16 event on the 172-BPM style clock becomes equivalent in real time to a 1/32 host-grid event at 86 BPM.

This is exactly what we want.

---

# 4. Internal phrase mapping

If the UI requests:

```text
Bars = 4
Host BPM = 86
Style BPM = 172
Multiplier = 2
```

the output must still occupy **4 DAW bars**.

Internally, however, the rhythmic grammar may process:

```text
styleBars = hostBars * clockMultiplier
          = 4 * 2
          = 8 internal Trap bars
```

and map those style bars back into four host bars.

This explains an important musical consequence.

At high-tempo Trap:

```text
main half-time snare = beat 3 of each style bar
```

At `86 BPM -> 172 BPM`, two style bars fit into one host bar.

Therefore those backbeats map naturally to approximately:

```text
host beat 2
host beat 4
```

This preserves the same physical backbeat interval.

The generator must therefore reason in **style-time**, then map to **transport-time**.

---

# 5. Event representation

Do NOT rewrite NoteEvent if the project already has:

```text
1/16 external step
+ PPQ microOffset
```

Use the existing format.

Internally preserve the distinction between:

```text
structural position
structural fast-grid remainder
microtiming
```

Do not confuse a structural 1/32 placement with “human timing”.

Recommended temporary representation:

```cpp
struct TrapTimingPosition
{
    int styleTick64 = 0;

    // exact structural location after tempo mapping
    int structuralPPQ = 0;

    // actual expressive displacement
    int pocketPPQ = 0;
    int jitterPPQ = 0;

    int finalPPQ() const
    {
        return structuralPPQ + pocketPPQ + jitterPPQ;
    }
};
```

When converted to the existing editor/event format:

```text
nearest step16
+
microOffset remainder
```

may be used if that is how the current project represents off-step events.

---

# 6. Important correction: triplets must NOT be approximated on tick64

A straight 1/64 grid is excellent for:

- 1/8;
- 1/16;
- 1/32;
- 1/64;
- straight bursts.

But 64 is not divisible by 3.

Therefore do NOT approximate triplets using fake patterns such as:

```text
0, 3, 5
```

unless deliberately creating a non-triplet rhythmic shape.

The project already uses:

```text
PPQ = 960
```

which is ideal because it is divisible by 3.

Exact values:

```text
quarter note        = 960 PPQ
eighth note         = 480 PPQ
sixteenth note      = 240 PPQ
thirty-second       = 120 PPQ
sixty-fourth        = 60 PPQ

eighth-note triplet     = 320 PPQ
sixteenth-note triplet  = 160 PPQ
thirty-second triplet   = 80 PPQ
```

Therefore:

```text
straight topology -> tick64
triplet topology  -> exact PPQ/rational subdivision
```

This is mandatory for convincing Trap hats.

---

# 7. Scientific model of “why Trap moves”

The engine should not maximize rhythmic complexity.

Studies of groove repeatedly find an **inverted-U relationship** between syncopation/complexity and the desire to move: too little rhythmic violation is boring, too much destroys meter, and intermediate complexity is most effective.

Model this explicitly.

Let:

```text
S(P) = normalized pattern syncopation
μs   = target syncopation for style
σs   = accepted width
```

Then:

```text
GrooveSync(P) =
    exp(
        -((S(P) - μs)^2)
        /(2 * σs^2)
    )
```

This means:

```text
too straight -> lower score
moderate syncopation -> maximum score
too chaotic -> lower score
```

Do NOT score:

```text
more syncopation = always better
```

The same principle should be applied to:

- hat entropy;
- kick irregularity;
- fill density;
- phrase variation.

Trap is controlled prediction violation.

---

# 8. Three rhythmic layers

Treat Trap as three interacting temporal layers.

## Layer A — Backbone

Highest perceptual stability:

```text
main snare/clap anchors
essential phrase kick anchors
major 808 anchors
```

Low entropy.

## Layer B — Propulsion

Provides motion:

```text
closed hats
kick answers
808 movement
open hats
```

Medium entropy.

## Layer C — Ornament

Creates surprise:

```text
hat rolls
triplets
1/32 bursts
1/64 bursts
ghost clap/snare
perc
transition hits
```

High local entropy but LOW global occupancy.

Rule:

```text
Backbone entropy < Propulsion entropy < Ornament local entropy
```

but:

```text
Ornament total time coverage must remain low.
```

This is how the pattern can sound complex without losing pulse.

---

# 9. Main snare / clap grammar

At the high style clock, the fundamental Trap backbeat is:

```text
beat 3 of each style bar
```

This is the low-entropy anchor.

Represent:

```text
S_b = required main snare event at style beat 3
```

Backbone loss:

```text
E_snare =
    Σ_b (1 - S_b)^2
```

For double-time host mapping, do NOT manually force arbitrary new snare positions.

Generate in style-time and map to host-time.

That automatically converts a `172 BPM` style skeleton into the appropriate low-host-grid backbeat.

Secondary claps/snare decorations are optional and must not obscure the main backbeat.

---

# 10. Hi-hat Engine — do NOT use independent Bernoulli notes

This is the biggest rhythmic upgrade.

Bad architecture:

```cpp
for every possible tick:
    if random() < hatProbability:
        addHat();
```

This creates statistical noise, not Trap phrasing.

Use a hierarchical Hat Phrase Model.

```text
Base Pulse
   ↓
Motif
   ↓
Rate State
   ↓
Burst / Roll Event
   ↓
Accent Envelope
   ↓
Velocity Contour
   ↓
Microtiming
```

---

# 11. Hat rate as a state machine

Define:

```cpp
enum class HatRate
{
    Eighth,
    Sixteenth,
    ThirtySecond,
    SixtyFourth,
    EighthTriplet,
    SixteenthTriplet,
    ThirtySecondTriplet
};
```

Do not allow all states equally.

The normal state should occupy most of the phrase.

For example:

```text
Eighth / Sixteenth:
    dominant

ThirtySecond:
    short local burst

SixtyFourth:
    rare micro-burst only

Triplet:
    local transition state
```

Model rate evolution as a Markov chain:

```text
P(R_t | R_t-1)
```

High self-transition for normal pulse:

```text
P(1/16 -> 1/16) = high
```

Low transition into fast rolls:

```text
P(1/16 -> 1/32) = low
P(1/16 -> 1/64) = very low
```

Once a burst starts:

```text
1/32 -> 1/32
```

may remain likely for only a short bounded duration.

Then return to base pulse.

This creates “трещётки” rather than continuous machine-gun hats.

---

# 12. Hat bursts are objects

Create:

```cpp
struct HatBurst
{
    int startPPQ;
    HatRate rate;

    int noteCount;

    HatBurstRole role;

    VelocityContour contour;

    float strength;
};
```

Roles:

```cpp
enum class HatBurstRole
{
    PreSnareTension,
    PostSnareRelease,
    KickResponse,
    BarTransition,
    PhraseEnding,
    Surprise
};
```

A roll must have a reason.

Do NOT generate fast hats globally simply because `rollAmount` is high.

---

# 13. Hat burst probability is context-dependent

For candidate burst start `t`:

```text
Z_hat(t) =
      a1 * PreSnare(t)
    + a2 * PhraseBoundary(t)
    + a3 * EmptyPocket(t)
    + a4 * KickResponseOpportunity(t)
    + a5 * BarEnergy(t)
    - a6 * ExistingDensity(t)
    - a7 * RecentBurst(t)
    - a8 * Collision(t)
```

Convert to probability:

```text
P_burst(t) = sigmoid(Z_hat(t))
```

This creates contextual rolls.

A burst immediately following another burst receives a strong penalty.

---

# 14. Roll length distribution

Do NOT use uniform random note count.

Prefer a short-biased distribution.

Example conceptual prior:

```text
2 notes  -> common
3 notes  -> common
4 notes  -> moderate
5–6      -> uncommon
7+       -> rare / phrase fill only
```

A geometric-like distribution is suitable:

```text
P(L = k) ∝ (1-p)^(k-2) * p
```

with hard style bounds.

Thus long rolls are naturally rare.

---

# 15. Hat velocity is a contour, not random noise

For hat event `i`:

```text
V_i =
      μ
    + MetricalAccent_i
    + BurstEnvelope_i
    + PhraseEnergy_i
    + ε_i
```

where:

```text
ε_i ~ truncated N(0, σ²)
```

Support contours:

```text
Rising
Falling
Wave
StrongWeak
AccentEnd
```

Examples:

```text
Rising:
48 56 66 79 94

Falling:
94 78 65 54 46

StrongWeak:
82 55 78 52
```

Do not allow flat repeated 1/32/1/64 rolls unless specifically profile-authorized.

---

# 16. Hat entropy target

Define hat onset entropy or an equivalent normalized complexity metric:

```text
H_hat(P)
```

Do not maximize it.

Score it around a target:

```text
Q_hat_entropy =
    exp(
        -((H_hat - μ_hat)^2)
        /(2*σ_hat^2)
    )
```

Interpretation:

```text
too low:
    dead metronome

target:
    recognizable pulse + surprise

too high:
    random noise carpet
```

---

# 17. Kick + 808 must be generated JOINTLY

Do not generate:

```text
Kick pattern
then unrelated 808 pattern
```

Trap low end is a joint rhythmic system.

Let:

```text
K_t = kick onset
B_t = 808 onset
A_t = 808 active state
```

Use a joint conditional model:

```text
P(K_t, B_t | context)
```

Important relation classes:

```cpp
enum class LowEndRelation
{
    CoupledAttack,
    KickOnly,
    BassOnly,
    BassAnswer,
    KickPickup,
    SlideTarget,
    PhraseAnchor
};
```

---

# 18. Kick-808 coupling score

For each kick, find an 808 onset in a local window.

```text
C_KB =
    coupledKickCount
    / max(1, totalKickCount)
```

But DO NOT maximize coupling to 1.0 for every style.

If every kick always equals an 808 start, patterns become predictable.

Use a target band:

```text
C_target_low
C_target_high
```

Example engineering prior:

```text
~0.65–0.90
```

depending on style.

Score:

```text
Q_coupling =
    bandScore(C_KB, low, high)
```

---

# 19. Kick placement model

Kick must be conditional on:

```text
metrical strength
existing 808
previous kick distance
snare position
phrase role
negative space
syncopation target
```

Logit:

```text
zKick(t) =
      β0
    + βmetric * Metric(t)
    + βbass   * BassRelation(t)
    + βsync   * SyncopationOpportunity(t)
    + βphrase * PhraseRole(t)
    + βanswer * CallResponse(t)
    - βcrowd  * RecentKickDensity(t)
    - βsnare  * BadSnareCollision(t)
```

Then:

```text
P(K_t = 1) = sigmoid(zKick(t))
```

Do not treat candidate tick weights as the entire kick engine.

They are only priors.

---

# 20. Kick conversation

Classify kick events:

```text
Anchor
Pickup
Question
Answer
Turnaround
```

Generate relationships.

Example:

```text
Anchor
   ↓
possible Pickup
   ↓
main Snare
   ↓
possible Answer
```

This yields recognizable low-end phrases.

A random isolated kick with no metrical or conversational explanation receives a lower score.

---

# 21. 808 duration model

808 must be monophonic unless explicit slide/legato behavior is supported.

For consecutive 808 starts:

```text
t_i
t_i+1
```

choose duration as:

```text
D_i =
    min(
        α * (t_i+1 - t_i),
        Dmax
    )
```

where:

```text
α ∈ approximately 0.65–0.95
```

depending on profile.

This preserves breathing room before the next note.

For an anchor 808:

```text
longer duration allowed
```

For answer/stab:

```text
shorter duration preferred
```

For slide:

```text
controlled overlap / legato may be allowed
```

Do not allow uncontrolled bass-wall occupancy.

---

# 22. 808 negative-space model

Let:

```text
ρ808 =
    total active 808 time
    / phrase duration
```

Use a target band, not maximum duration.

Example conceptual target:

```text
low enough to create gaps
high enough to keep low-end authority
```

Score:

```text
Q_808_space =
    bandScore(
        ρ808,
        profile.min808Occupancy,
        profile.max808Occupancy
    )
```

If 808 is almost always active:

```text
mud penalty increases
```

If 808 is nearly absent:

```text
low-end identity score decreases
```

---

# 23. Low-frequency collision model

When Kick and 808 are separate sample layers, distinguish:

```text
intentional coupled attack
```

from:

```text
accidental overlapping low-end clutter
```

Approximate collision energy:

```text
E_low(t) =
    KickEnergy(t)
    + BassEnergy(t)
```

If audio-level envelope data are unavailable, estimate using:

```text
kick onset window
808 duration occupancy
sample role
```

Penalty should increase for:

```text
long 808 tail
+
new unrelated kick
+
no coupling role
```

Do not penalize intentional coupled attacks.

---

# 24. Why the low end matters

The low-end layer should be treated as a primary rhythmic perceptual channel, not decoration.

Research has linked low-frequency rhythmic energy with movement and metrical perception, and experimental work at live concerts found increased audience movement when very-low-frequency energy was present.

Engineering conclusion:

```text
Kick + 808 are part of the meter model.
```

They must participate directly in:

- anchor scoring;
- groove scoring;
- phrase scoring;
- negative-space scoring.

---

# 25. Phrase architecture

For four requested HOST bars:

```text
Host Bar 1:
    establish groove

Host Bar 2:
    reinforce + small mutation

Host Bar 3:
    development

Host Bar 4:
    turnaround / optional rare ending event
```

In double-time style mode the engine may internally contain twice as many style bars, but the high-level host phrase roles above remain intact.

Do NOT let double-time mapping cause:

```text
twice as many fills
twice as many transitions
```

Phrase logic belongs to HOST phrase scale.

Subdivision grammar belongs to STYLE clock.

This separation is mandatory.

---

# 26. Two-level phrase clock

Therefore the engine needs:

```text
MacroClock = host bars / phrase structure
MicroClock = normalized Trap style tempo
```

MacroClock controls:

```text
statement
development
turnaround
rare events
fills
dropouts
```

MicroClock controls:

```text
hat rate
roll spacing
kick/808 fine placement
triplets
ornaments
```

This prevents `86 -> 172` from accidentally doubling arrangement events.

---

# 27. Negative space

For each local window `w`, calculate event occupancy.

```text
ρ_w =
    active rhythmic events
    / possible event capacity
```

Do not target constant density.

Use local density budgets:

```text
kick budget
hat budget
ornament budget
low-end occupancy budget
```

When a hat roll appears:

```text
reduce nearby ordinary hats
```

When a dense kick/808 exchange appears:

```text
reduce percussion / extra accents
```

This keeps the rhythm readable.

---

# 28. Complexity budget

Define total local complexity:

```text
C_w =
      aH * HatComplexity_w
    + aK * KickComplexity_w
    + aB * BassComplexity_w
    + aP * PercComplexity_w
```

Use a target band:

```text
Cmin <= C_w <= Cmax
```

A roll consumes complexity budget.

A sliding 808 exchange consumes complexity budget.

A fill consumes complexity budget.

This stops several “interesting” subsystems from all firing at once.

---

# 29. Global Trap quality function

For pattern `P` and style profile `S`:

```text
Q(P | S) =
      wM * MeterScore(P)
    + wG * GrooveSync(P)
    + wH * HatPhraseScore(P)
    + wR * RollQuality(P)
    + wK * KickGrammar(P)
    + wB * BassGrammar(P)
    + wC * Kick808Coupling(P)
    + wN * NegativeSpace(P)
    + wV * VelocityContour(P)
    + wP * PhraseCoherence(P)
    + wT * TurnaroundQuality(P)

    - λ1 * HatSpam(P)
    - λ2 * LowEndMud(P)
    - λ3 * Collision(P)
    - λ4 * ExcessComplexity(P)
    - λ5 * Repetition(P)
    - λ6 * RandomnessWithoutRole(P)
```

All values should be normalized or at least scale-compatible.

---

# 30. Groove as controlled prediction error

Do not optimize for perfect repetition.

Do not optimize for maximum difference.

Use:

```text
predictable backbone
+
moderately unpredictable surface
```

Formal decomposition:

```text
Q_predict =
    BackbonePredictability
    *
    SurfaceSurprise
```

but both are bounded.

Better:

```text
Q_predict =
    bandScore(
        PredictionError(P),
        targetLow,
        targetHigh
    )
```

Too little prediction error:

```text
boring
```

Too much:

```text
chaotic
```

Middle:

```text
groove
```

---

# 31. Candidate generation

Keep candidate search.

Recommended:

```text
64 candidates
```

unless performance profiling proves this too expensive.

Pipeline:

```text
resolve TrapTempoContext
        ↓
generate Backbone
        ↓
generate Kick/808 joint motif
        ↓
generate Base Hat Motif
        ↓
insert contextual HatBursts
        ↓
add open hats / clap / perc
        ↓
apply phrase mutation
        ↓
apply macro rare event if selected
        ↓
apply microtiming
        ↓
repair
        ↓
hard validation
        ↓
score
```

---

# 32. Hard constraints

Hard validation should include:

```text
1. valid timing bounds

2. required main backbeat exists

3. no uncontrolled infinite 808 overlap

4. no structural event outside phrase

5. no persistent full-density 1/64 hat wall

6. triplets represented exactly in PPQ

7. deterministic seed

8. double-time mapping does not change host phrase length
```

Do NOT hard-fail:

```text
one 1/64 hat ornament
one unusual kick
one bass-only answer
one short triplet burst
```

These may be musically valid.

---

# 33. Hat-roll hard limits

A 1/64 rate is allowed only as a short burst.

Example initial rule:

```text
1/64 burst:
    2–5 notes normally

longer:
    only explicit fill/transition role
```

1/32 may be somewhat longer.

Triplet bursts are independently bounded.

Do not allow a full bar of uninterrupted 1/64 hats in normal Trap generation.

---

# 34. Rate-change quality

A convincing “трещётка” often comes from RATE CHANGE rather than simply “very fast hats”.

Example:

```text
1/16
1/16
1/16
1/32 1/32
1/16
triplet burst
1/16
```

Score transitions between rates.

Define:

```text
RateChangeScore =
    ContextFit
    + ReturnToBasePulse
    + VelocityContour
    - ExcessTransitions
```

A burst should generally resolve back to the base pulse.

---

# 35. Open hat

Open hat should usually function as:

```text
lift
response
transition
```

not constant decoration.

Candidate probability should increase:

```text
after selected kick
before/after phrase boundary
in empty high-frequency pocket
```

and decrease:

```text
inside dense closed-hat burst
near another open hat
during clutter
```

---

# 36. Clap / secondary snare

Separate:

```text
MainBackbeat
LayerClap
GhostClap
RollSnare
```

Do not use the same velocity/timing model for all.

LayerClap:

```text
close to main snare
supports attack
```

GhostClap:

```text
significantly quieter
contextual
```

RollSnare:

```text
transition-only
bounded sequence
```

---

# 37. Microtiming

Do not use one large random humanize amount for Trap.

Timing:

```text
finalPPQ =
    structuralPPQ
    + rolePocketPPQ
    + smallJitterPPQ
```

Trap generally relies more on precise grid/rate contrast than BoomBap-style broad pocket.

Therefore:

```text
jitter small
role timing deliberate
```

Fast rolls require especially small random timing because large jitter destroys the perceived rate.

As rate increases:

```text
allowed jitter should decrease.
```

Possible relation:

```text
σ_jitter(rate) =
    σ_base / sqrt(rateMultiplier)
```

---

# 38. Density knob semantics

The UI Density control must not simply multiply every note probability.

Instead map density to budgets:

```text
Density
    ↓
kickBudget
hatBaseBudget
hatBurstBudget
percBudget
openHatBudget
```

Keep non-linear response.

For example:

```text
hatBurstBudget grows slower than baseHatBudget
```

so turning Density up does not immediately create endless rolls.

Suggested nonlinear mapping:

```text
x = UI Density [0..1]

baseDensity   = x
ornamentLevel = x^1.5
extremeRoll   = x^2.5
```

Thus extreme ornaments remain rare until high settings.

---

# 39. Humanize knob semantics

Humanize must control:

```text
microtiming residual
velocity noise
small motif variation
```

but NOT:

```text
structural roll positions
triplet grid
double-time mapping
```

Do not let Humanize destroy precise fast hat topology.

---

# 40. Variation without destroying quality

Use hidden groove archetypes inside every Trap substyle.

Possible shared archetypes:

```text
Balanced
SparseLowEnd
HatDriven
KickConversation
RollAccent
Turnaround
```

These are NOT UI styles.

They create different valid solutions inside the same substyle.

Substyle defines the language.

Archetype defines the temporary performance strategy.

---

# 41. Rare events

For four host bars, optionally apply:

```text
HatRateFill
SnareFill
Kick808Turnaround
HatDropout
BassDropout
OpenHatLift
BreakStop
```

Default total probability:

```text
approximately 5–10%
```

Do not trigger by generation counter.

Use deterministic seed-derived selection.

Same seed must always create the same result.

---

# 42. Near-best candidate selection

Do not always choose strict `argmax(Q)`.

After scoring:

```text
Qbest = max Q
```

build:

```text
NearBest =
    candidates with
    Q >= Qbest - tolerance
```

and above a hard musical quality floor.

Then deterministic weighted selection:

```text
P(i) =
    exp(Q_i / T)
    / Σ exp(Q_j / T)
```

with low temperature.

This increases diversity without choosing bad patterns.

---

# 43. Quality floor

A candidate must NEVER win merely because it is unusual.

Require:

```text
Q >= QualityFloor
```

before novelty or weighted selection.

Calibrate QualityFloor against the current scorer distribution.

---

# 44. Novelty score

Only after candidate passes QualityFloor:

```text
SelectionScore =
    MusicalQuality
    + smallNoveltyWeight * Novelty
```

Novelty may measure:

```text
kick topology
hat rate sequence
roll placement
808 relation
phrase ending
```

Do NOT define novelty as:

```text
number of random differences
```

---

# 45. Trap tempo acceptance tests

Mandatory tests:

```text
Host = 86 BPM
Expected style tempo = approximately 172 BPM
Expected multiplier = 2
```

```text
Host = 85 BPM
Expected style tempo = approximately 170 BPM
```

Verify:

```text
DAW transport remains 85/86
output phrase still occupies requested host bars
fast hats are physically equivalent to high-tempo programming
backbeat remains correct
fills are not doubled at macro level
```

Also test around the tempo-normalization boundary to avoid abrupt broken behavior.

---

# 46. Timing unit tests

PPQ=960 tests:

```text
1/16 = 240 PPQ
1/32 = 120 PPQ
1/64 = 60 PPQ

1/8 triplet  = 320 PPQ
1/16 triplet = 160 PPQ
1/32 triplet = 80 PPQ
```

Do not accept approximate triplet timing if exact PPQ timing is available.

---

# 47. Statistical batch audit

Run at least:

```text
1000 seeds per Trap substyle
```

Record:

```text
host BPM
style BPM
clock multiplier

kick count
808 count
kick/808 coupling

808 occupancy

hat base rate
hat note count
burst count
burst rate distribution
burst mean length
1/64 burst count
triplet burst count

hat velocity variance

syncopation
negative space
complexity

rare event
repairs

total score
component scores

generation time
```

---

# 48. Failure metrics

Audit must detect:

```text
double-time failed at low host BPM

snare backbone missing

full-bar 1/64 hat wall

triplets quantized incorrectly

flat-velocity hat rolls

too many rolls

808 always active

kick and 808 entirely unrelated

low-end collision overload

four identical bars

four chaotic unrelated bars

macro fills occurring too often

same-seed nondeterminism
```

---

# 49. Fundamental implementation rule

Trap is NOT:

```text
fast hats + 808 + random kicks
```

The engine must encode:

```text
slow macro perception
+
fast micro clock
+
stable half-time anchor
+
moderate syncopation
+
low-frequency metrical force
+
bounded high-frequency entropy
+
contextual rate changes
+
negative space
```

This is the shared Trap grammar.

---

# 50. High-level formula

The engine is searching for:

```text
P* = argmax[P ∈ Ωtrap] Q(P | style)
```

where:

```text
Ωtrap =
    patterns satisfying Trap hard constraints
```

and:

```text
Q(P | style) =
      Meter
    + ModerateSyncopation
    + Kick808Relationship
    + HatPhraseCoherence
    + RollQuality
    + LowEndMovement
    + NegativeSpace
    + PhraseDevelopment
    + VelocityLife
    - Spam
    - Mud
    - Collision
    - UnexplainedRandomness
```

A useful compact conceptual form:

```text
TrapGroove
≈
StablePrediction
×
ControlledViolation
×
LowEndSalience
×
HighFrequencyMotion
×
NegativeSpace
```

If any factor approaches zero, the result weakens.

---

# 51. Recommended implementation order

## PHASE 1 — Tempo semantics

1. Trace host BPM synchronization.
2. Add `TrapTempoContext`.
3. Implement octave-equivalent style tempo.
4. Confirm `86 -> 172`.
5. Keep host phrase length unchanged.

## PHASE 2 — Structural time

6. Map style tick64 to host PPQ using `clockMultiplier`.
7. Preserve macro host-bar phrase clock.
8. Add exact PPQ triplet subdivisions.

## PHASE 3 — Hats

9. Replace independent roll-note generation with HatBurst objects.
10. Add HatRate state machine.
11. Add exact triplet bursts.
12. Add short-biased burst-length distribution.
13. Add velocity contours.
14. Add rate-change scoring.
15. Add roll-density budget.

## PHASE 4 — Kick + 808

16. Generate them jointly.
17. Add low-end relation roles.
18. Add kick conversation roles.
19. Add target coupling band.
20. Add 808 duration/occupancy model.
21. Add low-end collision/mud scoring.

## PHASE 5 — Global quality

22. Add inverted-U syncopation target.
23. Add local complexity budget.
24. Add negative-space score.
25. Add quality floor.
26. Add near-best deterministic selection.
27. Add low-weight novelty.

## PHASE 6 — QA

28. Run 1000-seed audit per profile.
29. Test low-host synced BPM.
30. Test triplet exactness.
31. Test determinism.
32. Report all distributions.

---

# 52. Acceptance criteria

The task is complete only if:

1. At DAW 86 BPM, Trap internally behaves approximately as 172-BPM rhythmic programming.

2. DAW transport remains 86 BPM.

3. Requested 4 host bars remain exactly 4 host bars.

4. Macro fills/turnarounds do not occur twice as often merely because the micro clock doubled.

5. Straight 1/64 structural timing is supported.

6. Triplets use exact PPQ subdivisions, not fake 1/64 approximations.

7. Hi-hat rolls are generated as bounded contextual bursts.

8. 1/64 hats occur as short accents, not continuous walls.

9. Hat rolls have velocity contours.

10. Hat rate can move between normal, double and triplet states.

11. Kick and 808 are generated as a related low-end system.

12. 808 remains monophonic except intentional slide/legato behavior.

13. 808 has negative space.

14. Kick/808 coupling is target-banded rather than blindly maximized.

15. Moderate syncopation scores above both extremely straight and extremely chaotic patterns.

16. Local complexity is bounded.

17. Same seed/settings are deterministic.

18. Near-best selection increases diversity without violating QualityFloor.

19. Existing UI/editor/APVTS/sample loading/export are not broken.

20. Batch audit demonstrates that the engine does not collapse into one safe pattern family.

---

# 53. Agent report required after implementation

Return:

1. Files changed.
2. Existing BPM-sync path found.
3. Why 85–86 BPM previously sounded too slow.
4. Exact `TrapTempoContext` implementation.
5. Exact mapping for 86 -> 172.
6. How style tick64 maps to host PPQ.
7. How macro phrase time remains at host scale.
8. Exact triplet implementation.
9. Previous hi-hat roll logic.
10. New HatRate/HatBurst logic.
11. Burst probability model.
12. Burst-length distribution.
13. Velocity contour implementation.
14. Kick/808 coupling implementation.
15. 808 duration/occupancy logic.
16. Low-end collision model.
17. New scorer components.
18. QualityFloor / near-best selection.
19. 1000-seed audit summary.
20. Performance P50/P95/worst.
21. Build result.
22. Any implementation constraint that prevented the requested design.

Do not silently substitute a simpler random solution.

---

# Scientific / production basis

This engineering model intentionally separates empirically supported groove principles from genre-specific engineering priors.

Relevant references:

- Witek et al. (2014), *Syncopation, Body-Movement and Pleasure in Groove Music*, PLOS ONE. Medium syncopation produced the strongest movement/pleasure ratings:  
  https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0094446

- Seeberg et al. (2025), *Beyond syncopation: The number of rhythmic layers shapes the pleasurable urge to move to music*. Rhythmic complexity/layering also exhibits an intermediate-complexity optimum:  
  https://pubmed.ncbi.nlm.nih.gov/40373735/

- Current Biology (2022), *Undetectable very-low frequency sound increases dancing at a live concert*. Very-low-frequency energy increased measured audience movement:  
  https://www.sciencedirect.com/science/article/pii/S0960982222015354

- MusicRadar, *The beginner's guide to trap*. Practical description of half-time tempo ambiguity, faster-than-1/16 hi-hat programming, triplets and historical use of double-tempo sequencing:  
  https://www.musicradar.com/news/beginners-guide-to-trap

- MusicRadar, *10 tricks every trap producer should know*. Practical description of 140–160 BPM programming with half-time backbeat, fast percussion, and 808-centered production:  
  https://www.musicradar.com/tuition/tech/10-tricks-every-trap-producer-should-know-638684

- MusicRadar, *How to get the perfect 808s*. Practical discussion of tuned 808 bass, monophonic use, decay shaping and kick/808 low-frequency interaction:  
  https://www.musicradar.com/how-to/808-kick-guide

---

## Final engineering statement

The most important change is not “make hats faster”.

It is:

```text
TRANSPORT CLOCK != TRAP RHYTHMIC CLOCK
```

At synchronized 86 BPM:

```text
TransportTempo = 86
StyleTempo     = 172
Macro phrase   = still 4 host bars
Micro grammar  = double-time
```

Once that is correct, the characteristic Trap result should come from a hierarchical generator:

```text
stable half-time backbone
        +
joint kick/808 grammar
        +
hat motif
        +
short contextual rate changes
        +
exact triplet/straight subdivisions
        +
bounded complexity
        +
negative space
        +
candidate optimization
```

—not from multiplying random note probability.
