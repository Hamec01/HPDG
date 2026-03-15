#pragma once

namespace bbg
{
enum class GenreType
{
    BoomBap,
    Rap,
    Trap,
    Drill
};

struct GeneratorParams
{
    float bpm = 90.0f;
    float swingPercent = 56.0f;    // 50..75
    float velocityAmount = 0.5f;   // 0-1
    float timingAmount = 0.4f;     // 0-1
    float humanizeAmount = 0.3f;   // 0-1
    float densityAmount = 0.5f;    // 0-1

    int bars = 2;
    int seed = 1;
    int boombapSubstyle = 0;
    int rapSubstyle = 0;
    int trapSubstyle = 0;
    int drillSubstyle = 0;
    int keyRoot = 0; // 0=C, 1=C#, ... 11=B
    int scaleMode = 0; // 0=Minor, 1=Major, 2=Harmonic Minor
    int tempoInterpretationMode = 0; // 0=Auto, 1=Original, 2=Half-time Aware
    bool syncDawTempo = false;
    bool seedLock = false;

    GenreType genre = GenreType::BoomBap;
};
} // namespace bbg
