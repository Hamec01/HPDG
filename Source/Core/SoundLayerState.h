#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace bbg
{
enum class EqBandShape
{
    LowCut,
    Bell,
    HighCut
};

struct CompressorState
{
    bool enabled = true;
    int order = 2;
    float ratio = 2.0f;
    float thresholdDb = -18.0f;
    float mix = 1.0f;
    float attackMs = 10.0f;
    float releaseMs = 120.0f;
    float saturation = 0.0f;
};

struct ReverbState
{
    bool enabled = false;
    int order = 3;
    float mix = 0.15f;
    float predelayMs = 12.0f;
    float size = 0.45f;
    float erTail = 0.5f;
};

struct TransientState
{
    bool enabled = false;
    int order = 4;
    float attack = 0.0f;
    float sustain = 0.0f;
    float gainDb = 0.0f;
    bool smooth = true;
    bool limit = false;
};

struct EqBandState
{
    bool enabled = true;
    float freqHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
    EqBandShape shape = EqBandShape::Bell;
};

struct EqState
{
    bool enabled = true;
    int order = 1;
    std::array<EqBandState, 7> bands;
    int selectedBand = 0;
};

inline EqState makeDefaultEqState()
{
    EqState eq;
    eq.bands[0] = { true, 30.0f, 0.0f, 0.71f, EqBandShape::LowCut };
    eq.bands[1] = { true, 90.0f, 0.0f, 0.85f, EqBandShape::Bell };
    eq.bands[2] = { true, 180.0f, 0.0f, 0.95f, EqBandShape::Bell };
    eq.bands[3] = { true, 1000.0f, 0.0f, 1.0f, EqBandShape::Bell };
    eq.bands[4] = { true, 3500.0f, 0.0f, 0.9f, EqBandShape::Bell };
    eq.bands[5] = { true, 9000.0f, 0.0f, 0.8f, EqBandShape::Bell };
    eq.bands[6] = { true, 18000.0f, 0.0f, 0.71f, EqBandShape::HighCut };
    eq.selectedBand = 3;
    return eq;
}

struct SoundLayerState
{
    float pan = 0.0f;
    float width = 1.0f;

    CompressorState compressor;
    ReverbState reverbState;
    TransientState transientState;
    EqState eq = makeDefaultEqState();

    // Temporary flat compatibility fields. Old UI/DSP code still reads these directly
    // until the next Sound Module phases move over to the nested model.
    float eqTone = 0.0f;
    float compression = 0.0f;
    float reverb = 0.0f;
    float gate = 0.0f;
    float transient = 0.0f;
    float drive = 0.0f;

    void syncExpandedFromLegacy()
    {
        eqTone = std::clamp(eqTone, -1.0f, 1.0f);
        compression = std::clamp(compression, 0.0f, 1.0f);
        reverb = std::clamp(reverb, 0.0f, 1.0f);
        gate = std::clamp(gate, 0.0f, 1.0f);
        transient = std::clamp(transient, 0.0f, 1.0f);
        drive = std::clamp(drive, 0.0f, 1.0f);

        eq = makeDefaultEqState();
        eq.enabled = true;
        eq.order = 1;
        eq.selectedBand = 3;
        eq.bands[2].gainDb = -eqTone * 2.5f;
        eq.bands[3].gainDb = eqTone * 1.5f;
        eq.bands[4].gainDb = eqTone * 4.0f;
        eq.bands[5].gainDb = eqTone * 2.5f;

        compressor.enabled = compression > 0.001f;
        compressor.order = 2;
        compressor.mix = compression;
        compressor.ratio = 1.0f + compression * 3.0f;
        compressor.thresholdDb = -12.0f - compression * 18.0f;
        compressor.attackMs = 8.0f + (1.0f - compression) * 18.0f;
        compressor.releaseMs = 80.0f + compression * 160.0f;
        compressor.saturation = compression * 0.25f + drive * 0.35f;

        reverbState.enabled = reverb > 0.001f;
        reverbState.order = 3;
        reverbState.mix = reverb;
        reverbState.predelayMs = 8.0f + reverb * 28.0f;
        reverbState.size = 0.25f + reverb * 0.55f;
        reverbState.erTail = 0.3f + reverb * 0.5f;

        transientState.enabled = transient > 0.001f;
        transientState.order = 4;
        transientState.attack = transient;
        transientState.sustain = -transient * 0.2f;
        transientState.gainDb = transient * 3.0f + drive * 1.5f;
        transientState.smooth = transient < 0.7f;
        transientState.limit = transient > 0.75f;
    }

    void syncLegacyFromExpanded()
    {
        const float lowMid = eq.bands[2].gainDb;
        const float mid = eq.bands[3].gainDb;
        const float presence = eq.bands[4].gainDb;
        const float air = eq.bands[5].gainDb;
        eqTone = std::clamp(((presence * 0.18f + air * 0.1f + mid * 0.08f) - lowMid * 0.12f), -1.0f, 1.0f);
        compression = std::clamp(compressor.mix, 0.0f, 1.0f);
        reverb = std::clamp(reverbState.mix, 0.0f, 1.0f);
        transient = std::clamp(transientState.attack, 0.0f, 1.0f);
    }

    void sanitizeExpanded()
    {
        pan = std::clamp(pan, -1.0f, 1.0f);
        width = std::clamp(width, 0.0f, 2.0f);

        compressor.order = std::max(0, compressor.order);
        compressor.ratio = std::clamp(compressor.ratio, 1.0f, 20.0f);
        compressor.thresholdDb = std::clamp(compressor.thresholdDb, -60.0f, 6.0f);
        compressor.mix = std::clamp(compressor.mix, 0.0f, 1.0f);
        compressor.attackMs = std::clamp(compressor.attackMs, 0.1f, 250.0f);
        compressor.releaseMs = std::clamp(compressor.releaseMs, 5.0f, 2000.0f);
        compressor.saturation = std::clamp(compressor.saturation, 0.0f, 1.0f);

        reverbState.order = std::max(0, reverbState.order);
        reverbState.mix = std::clamp(reverbState.mix, 0.0f, 1.0f);
        reverbState.predelayMs = std::clamp(reverbState.predelayMs, 0.0f, 250.0f);
        reverbState.size = std::clamp(reverbState.size, 0.0f, 1.0f);
        reverbState.erTail = std::clamp(reverbState.erTail, 0.0f, 1.0f);

        transientState.order = std::max(0, transientState.order);
        transientState.attack = std::clamp(transientState.attack, -1.0f, 1.0f);
        transientState.sustain = std::clamp(transientState.sustain, -1.0f, 1.0f);
        transientState.gainDb = std::clamp(transientState.gainDb, -24.0f, 24.0f);

        eq.order = std::max(0, eq.order);
        eq.selectedBand = std::clamp(eq.selectedBand, 0, static_cast<int>(eq.bands.size()) - 1);
        for (auto& band : eq.bands)
        {
            band.freqHz = std::clamp(band.freqHz, 20.0f, 22000.0f);
            band.gainDb = std::clamp(band.gainDb, -24.0f, 24.0f);
            band.q = std::clamp(band.q, 0.1f, 12.0f);
        }

        eqTone = std::clamp(eqTone, -1.0f, 1.0f);
        compression = std::clamp(compression, 0.0f, 1.0f);
        reverb = std::clamp(reverb, 0.0f, 1.0f);
        gate = std::clamp(gate, 0.0f, 1.0f);
        transient = std::clamp(transient, 0.0f, 1.0f);
        drive = std::clamp(drive, 0.0f, 1.0f);
    }
};
} // namespace bbg
