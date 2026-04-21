#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace bbg
{
enum class EqBandShape
{
    LowCut = 0,
    Bell,
    HighCut
};

inline constexpr int kEqBandCount = 7;

struct EqBandState
{
    bool enabled = false;
    float freqHz = 180.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
    EqBandShape shape = EqBandShape::Bell;
};

struct EqState
{
    int selectedBand = 2;
    std::array<EqBandState, kEqBandCount> bands {};
};

enum class DrumCompressorCharacter
{
    Glue = 0,
    Punch
};

enum class DrumSaturationMode
{
    Warm = 0,
    Punch
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

    float inputTrimDb = 0.0f;
    float outputTrimDb = 0.0f;

    bool autoMakeup = false;
    DrumCompressorCharacter character = DrumCompressorCharacter::Glue;
    DrumSaturationMode saturationMode = DrumSaturationMode::Warm;
    float lastGainReductionDb = 0.0f;
};

struct DrumReverbState
{
    bool enabled = true;
    float mix = 0.22f;
    float predelayMs = 18.0f;
    float size = 0.26f;
    float erTail = 0.32f;
};

struct DrumTransientState
{
    float attack = 0.0f;
    float sustain = 0.25f;
    float gainDb = 0.0f;
    bool smooth = false;
    bool limit = false;
};

struct MonstaFxState
{
    bool enabled = false;
    int order = 0;

    float dry = 1.0f;
    float wet = 0.0f;

    std::uint32_t chaosSeed = 0;
    bool pendingChaosReseed = false;
};

inline EqState createDefaultEqState()
{
    EqState eq;
    eq.selectedBand = 2;
    eq.bands[0] = { false, 45.0f, 0.0f, 0.71f, EqBandShape::LowCut };
    eq.bands[1] = { false, 65.0f, 0.0f, 0.72f, EqBandShape::Bell };
    eq.bands[2] = { false, 180.0f, 0.0f, 0.95f, EqBandShape::Bell };
    eq.bands[3] = { false, 420.0f, 0.0f, 1.25f, EqBandShape::Bell };
    eq.bands[4] = { false, 3200.0f, 0.0f, 1.10f, EqBandShape::Bell };
    eq.bands[5] = { false, 9000.0f, 0.0f, 0.80f, EqBandShape::Bell };
    eq.bands[6] = { false, 14000.0f, 0.0f, 0.71f, EqBandShape::HighCut };
    return eq;
}

inline int clampEqBandIndex(int index)
{
    return std::clamp(index, 0, kEqBandCount - 1);
}

inline EqBandState& selectedEqBand(EqState& eq)
{
    return eq.bands[static_cast<size_t>(clampEqBandIndex(eq.selectedBand))];
}

inline const EqBandState& selectedEqBand(const EqState& eq)
{
    return eq.bands[static_cast<size_t>(clampEqBandIndex(eq.selectedBand))];
}

inline float legacyEqToneFromEqState(const EqState& eq)
{
    return std::clamp(selectedEqBand(eq).gainDb / 12.0f, -1.0f, 1.0f);
}

inline void applyLegacyEqToneToEqState(EqState& eq, float eqTone)
{
    eq = createDefaultEqState();
    auto& bodyBand = eq.bands[2];
    bodyBand.gainDb = std::clamp(eqTone * 12.0f, -12.0f, 12.0f);
    bodyBand.enabled = std::abs(bodyBand.gainDb) > 0.001f;
}

inline bool isEqStateDefault(const EqState& eq)
{
    const auto defaults = createDefaultEqState();
    if (eq.selectedBand != defaults.selectedBand)
        return false;

    for (size_t bandIndex = 0; bandIndex < eq.bands.size(); ++bandIndex)
    {
        const auto& band = eq.bands[bandIndex];
        const auto& defaultBand = defaults.bands[bandIndex];
        if (band.enabled != defaultBand.enabled
            || std::abs(band.freqHz - defaultBand.freqHz) > 0.001f
            || std::abs(band.gainDb - defaultBand.gainDb) > 0.001f
            || std::abs(band.q - defaultBand.q) > 0.001f
            || band.shape != defaultBand.shape)
        {
            return false;
        }
    }

    return true;
}

inline CompressorState createDefaultCompressorState()
{
    CompressorState compressor;
    compressor.enabled = true;
    compressor.order = 2;
    compressor.ratio = 3.2f;
    compressor.thresholdDb = -20.0f;
    compressor.mix = 0.58f;
    compressor.attackMs = 14.0f;
    compressor.releaseMs = 145.0f;
    compressor.saturation = 0.18f;
    compressor.inputTrimDb = 0.0f;
    compressor.outputTrimDb = 0.0f;
    compressor.autoMakeup = true;
    compressor.character = DrumCompressorCharacter::Glue;
    compressor.saturationMode = DrumSaturationMode::Warm;
    compressor.lastGainReductionDb = 0.0f;
    return compressor;
}

inline CompressorState createInactiveCompressorState()
{
    auto compressor = createDefaultCompressorState();
    compressor.enabled = false;
    compressor.mix = 0.0f;
    return compressor;
}

inline DrumReverbState createDefaultDrumReverbState()
{
    DrumReverbState reverb;
    reverb.enabled = true;
    reverb.mix = 0.22f;
    reverb.predelayMs = 18.0f;
    reverb.size = 0.26f;
    reverb.erTail = 0.32f;
    return reverb;
}

inline DrumReverbState createInactiveDrumReverbState()
{
    auto reverb = createDefaultDrumReverbState();
    reverb.enabled = false;
    reverb.mix = 0.0f;
    return reverb;
}

inline DrumTransientState createDefaultDrumTransientState()
{
    DrumTransientState transient;
    transient.attack = 0.0f;
    transient.sustain = 0.25f;
    transient.gainDb = 0.0f;
    transient.smooth = false;
    transient.limit = false;
    return transient;
}

inline MonstaFxState createDefaultMonstaFxState()
{
    MonstaFxState monstaFx;
    monstaFx.enabled = false;
    monstaFx.order = 0;
    monstaFx.dry = 1.0f;
    monstaFx.wet = 0.0f;
    monstaFx.chaosSeed = 0;
    monstaFx.pendingChaosReseed = false;
    return monstaFx;
}

inline DrumCompressorCharacter clampDrumCompressorCharacter(int value)
{
    return static_cast<DrumCompressorCharacter>(std::clamp(value, 0, 1));
}

inline DrumSaturationMode clampDrumSaturationMode(int value)
{
    return static_cast<DrumSaturationMode>(std::clamp(value, 0, 1));
}

inline void sanitizeCompressorState(CompressorState& compressor)
{
    compressor.order = std::clamp(compressor.order, 1, 2);
    compressor.ratio = std::clamp(compressor.ratio, 1.2f, 10.0f);
    compressor.thresholdDb = std::clamp(compressor.thresholdDb, -40.0f, 0.0f);
    compressor.mix = std::clamp(compressor.mix, 0.0f, 1.0f);
    compressor.attackMs = std::clamp(compressor.attackMs, 1.0f, 80.0f);
    compressor.releaseMs = std::clamp(compressor.releaseMs, 20.0f, 400.0f);
    compressor.saturation = std::clamp(compressor.saturation, 0.0f, 1.0f);
    compressor.inputTrimDb = std::clamp(compressor.inputTrimDb, -18.0f, 18.0f);
    compressor.outputTrimDb = std::clamp(compressor.outputTrimDb, -18.0f, 18.0f);
    compressor.character = clampDrumCompressorCharacter(static_cast<int>(compressor.character));
    compressor.saturationMode = clampDrumSaturationMode(static_cast<int>(compressor.saturationMode));
    compressor.lastGainReductionDb = std::clamp(compressor.lastGainReductionDb, 0.0f, 36.0f);
}

inline void sanitizeDrumReverbState(DrumReverbState& reverb)
{
    reverb.mix = std::clamp(reverb.mix, 0.0f, 1.0f);
    reverb.predelayMs = std::clamp(reverb.predelayMs, 0.0f, 60.0f);
    reverb.size = std::clamp(reverb.size, 0.0f, 1.0f);
    reverb.erTail = std::clamp(reverb.erTail, 0.0f, 1.0f);
    if (reverb.mix <= 0.001f)
        reverb.enabled = false;
}

inline void sanitizeDrumTransientState(DrumTransientState& transient)
{
    transient.attack = std::clamp(transient.attack, 0.0f, 1.0f);
    transient.sustain = std::clamp(transient.sustain, 0.0f, 1.0f);
    transient.gainDb = std::clamp(transient.gainDb, -6.0f, 12.0f);
}

inline void sanitizeMonstaFxState(MonstaFxState& monstaFx)
{
    monstaFx.order = std::clamp(monstaFx.order, 0, 1);
    monstaFx.dry = std::clamp(monstaFx.dry, 0.0f, 1.0f);
    monstaFx.wet = std::clamp(monstaFx.wet, 0.0f, 1.0f);
    monstaFx.enabled = std::abs(monstaFx.dry - 1.0f) > 0.001f || monstaFx.wet > 0.001f;
}

inline float legacyCompressionFromCompressorState(const CompressorState& compressor)
{
    return compressor.enabled ? std::clamp(compressor.mix, 0.0f, 1.0f) : 0.0f;
}

inline float legacyReverbFromDrumReverbState(const DrumReverbState& reverb)
{
    return reverb.enabled ? std::clamp(reverb.mix, 0.0f, 1.0f) : 0.0f;
}

inline float legacyTransientFromDrumTransientState(const DrumTransientState& transient)
{
    return std::clamp(transient.attack, 0.0f, 1.0f);
}

inline float legacyDriveFromDrumTransientState(const DrumTransientState& transient)
{
    return std::clamp(std::max(0.0f, transient.gainDb) / 12.0f, 0.0f, 1.0f);
}

inline bool isDrumTransientAudiblyActive(const DrumTransientState& transient)
{
    return transient.attack > 0.001f
        || std::abs(transient.sustain - 0.25f) > 0.001f
        || std::abs(transient.gainDb) > 0.05f
        || transient.limit;
}

inline bool isMonstaFxAudiblyActive(const MonstaFxState& monstaFx)
{
    return monstaFx.enabled
        && (std::abs(monstaFx.dry - 1.0f) > 0.001f || monstaFx.wet > 0.001f);
}

inline void applyLegacyCompressionToCompressorState(CompressorState& compressor, float legacyCompression)
{
    const float amount = std::clamp(legacyCompression, 0.0f, 1.0f);
    compressor = createDefaultCompressorState();

    if (amount <= 0.001f)
    {
        compressor.enabled = false;
        compressor.mix = 0.0f;
        return;
    }

    compressor.enabled = true;
    compressor.mix = amount;
    compressor.ratio = 1.6f + amount * 4.4f;
    compressor.thresholdDb = -10.0f - amount * 18.0f;
    compressor.attackMs = 12.0f - amount * 5.0f;
    compressor.releaseMs = 150.0f - amount * 35.0f;
    compressor.saturation = amount * 0.15f;
    compressor.character = amount >= 0.72f ? DrumCompressorCharacter::Punch : DrumCompressorCharacter::Glue;
    compressor.saturationMode = amount >= 0.78f ? DrumSaturationMode::Punch : DrumSaturationMode::Warm;
    sanitizeCompressorState(compressor);
}

inline void applyLegacyReverbToDrumReverbState(DrumReverbState& reverb, float legacyReverb)
{
    const float amount = std::clamp(legacyReverb, 0.0f, 1.0f);
    if (amount <= 0.001f)
    {
        reverb = createInactiveDrumReverbState();
        return;
    }

    reverb = createDefaultDrumReverbState();
    reverb.enabled = true;
    reverb.mix = amount;
    reverb.predelayMs = 12.0f + amount * 28.0f;
    reverb.size = 0.28f + amount * 0.60f;
    reverb.erTail = 0.35f + amount * 0.55f;
    sanitizeDrumReverbState(reverb);
}

inline void applyLegacyTransientToDrumTransientState(DrumTransientState& transient, float legacyTransient, float legacyDrive)
{
    transient = createDefaultDrumTransientState();
    transient.attack = std::clamp(legacyTransient, 0.0f, 1.0f);
    transient.sustain = std::clamp(0.25f + transient.attack * 0.50f, 0.0f, 1.0f);
    transient.gainDb = std::clamp(std::max(0.0f, legacyDrive) * 12.0f, -6.0f, 12.0f);
    transient.smooth = transient.attack < 0.35f;
    transient.limit = transient.gainDb > 5.5f;
    sanitizeDrumTransientState(transient);
}

inline bool isCompressorAudiblyActive(const CompressorState& compressor)
{
    return compressor.enabled && compressor.mix > 0.001f;
}

inline bool isDrumReverbAudiblyActive(const DrumReverbState& reverb)
{
    return reverb.enabled && reverb.mix > 0.001f;
}

struct SoundLayerState
{
    float pan = 0.0f;
    float width = 1.0f;
    bool stereoFieldEnabled = true;
    int stereoFieldOrder = 0;
    float stereoFieldFocus = 0.5f;
    float stereoFieldEdge = 0.0f;
    bool stereoFieldMonoSafe = true;
    float stereoFieldLowCenterProtect = 0.75f;
    float stereoFieldAirSpread = 0.25f;
    float eqTone = 0.0f;
    EqState eq = createDefaultEqState();
    float compression = 0.0f;
    CompressorState compressor = createInactiveCompressorState();
    DrumReverbState drumReverb = createInactiveDrumReverbState();
    DrumTransientState drumTransient = createDefaultDrumTransientState();
    MonstaFxState monstaFx = createDefaultMonstaFxState();
    float reverb = 0.0f;
    float gate = 0.0f;
    float transient = 0.0f;
    float drive = 0.0f;
};

inline void sanitizeStereoFieldSettings(SoundLayerState& sound)
{
    sound.pan = std::clamp(sound.pan, -1.0f, 1.0f);
    sound.width = std::clamp(sound.width, 0.0f, 2.0f);
    sound.stereoFieldOrder = std::clamp(sound.stereoFieldOrder, 0, 3);
    sound.stereoFieldFocus = std::clamp(sound.stereoFieldFocus, 0.0f, 1.0f);
    sound.stereoFieldEdge = std::clamp(sound.stereoFieldEdge, 0.0f, 1.0f);
    sound.stereoFieldLowCenterProtect = std::clamp(sound.stereoFieldLowCenterProtect, 0.0f, 1.0f);
    sound.stereoFieldAirSpread = std::clamp(sound.stereoFieldAirSpread, 0.0f, 1.0f);
}

inline bool isStereoFieldAudiblyActive(const SoundLayerState& sound)
{
    if (!sound.stereoFieldEnabled)
        return false;

    return std::abs(sound.pan) > 0.001f
        || std::abs(sound.width - 1.0f) > 0.001f
        || std::abs(sound.stereoFieldFocus - 0.5f) > 0.001f
        || sound.stereoFieldEdge > 0.001f;
}

inline void syncLegacySoundLayerState(SoundLayerState& sound)
{
    sound.eq.selectedBand = clampEqBandIndex(sound.eq.selectedBand);
    sanitizeStereoFieldSettings(sound);
    sanitizeCompressorState(sound.compressor);
    sanitizeDrumReverbState(sound.drumReverb);
    sanitizeDrumTransientState(sound.drumTransient);
    sanitizeMonstaFxState(sound.monstaFx);
    sound.eqTone = std::clamp(legacyEqToneFromEqState(sound.eq), -1.0f, 1.0f);
    sound.compression = legacyCompressionFromCompressorState(sound.compressor);
    sound.reverb = legacyReverbFromDrumReverbState(sound.drumReverb);
    sound.transient = legacyTransientFromDrumTransientState(sound.drumTransient);
    sound.drive = legacyDriveFromDrumTransientState(sound.drumTransient);
}

inline void reconcileLegacySoundLayerState(SoundLayerState& sound)
{
    sound.eq.selectedBand = clampEqBandIndex(sound.eq.selectedBand);
    sanitizeStereoFieldSettings(sound);
    sound.reverb = std::clamp(sound.reverb, 0.0f, 1.0f);

    if (std::abs(sound.eqTone - legacyEqToneFromEqState(sound.eq)) > 0.001f && isEqStateDefault(sound.eq))
        applyLegacyEqToneToEqState(sound.eq, sound.eqTone);

    sanitizeCompressorState(sound.compressor);

    if (std::abs(sound.compression - legacyCompressionFromCompressorState(sound.compressor)) > 0.001f)
        applyLegacyCompressionToCompressorState(sound.compressor, sound.compression);

    sanitizeDrumReverbState(sound.drumReverb);

    if (std::abs(sound.reverb - legacyReverbFromDrumReverbState(sound.drumReverb)) > 0.001f)
        applyLegacyReverbToDrumReverbState(sound.drumReverb, sound.reverb);

    sanitizeDrumTransientState(sound.drumTransient);

    sanitizeMonstaFxState(sound.monstaFx);

    if (std::abs(sound.transient - legacyTransientFromDrumTransientState(sound.drumTransient)) > 0.001f
        || std::abs(sound.drive - legacyDriveFromDrumTransientState(sound.drumTransient)) > 0.001f)
    {
        applyLegacyTransientToDrumTransientState(sound.drumTransient, sound.transient, sound.drive);
    }

    syncLegacySoundLayerState(sound);
}
} // namespace bbg
