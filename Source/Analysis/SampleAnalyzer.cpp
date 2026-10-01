#include "SampleAnalyzer.h"

#include <cstdint>
#include <cmath>

#include "../Core/TimingGrid.h"

namespace bbg
{
namespace
{
constexpr int kMinBarsForHinting = 2;

double applyTempoHandling(double bpm, SampleAnalysisRequest::TempoHandling handling)
{
    double out = juce::jlimit(60.0, 180.0, bpm);

    switch (handling)
    {
        case SampleAnalysisRequest::TempoHandling::PreferHalfTime:
            if (out >= 120.0)
                out *= 0.5;
            break;

        case SampleAnalysisRequest::TempoHandling::PreferDoubleTime:
            if (out <= 95.0)
                out *= 2.0;
            break;

        case SampleAnalysisRequest::TempoHandling::KeepDetected:
            break;

        case SampleAnalysisRequest::TempoHandling::Auto:
        default:
            // Hip-hop oriented auto-fit: fold very fast estimates to half-time first.
            if (out >= 150.0)
                out *= 0.5;
            break;
    }

    return juce::jlimit(60.0, 180.0, out);
}
}

SampleAnalysisResult SampleAnalyzer::analyzeBuffer(const juce::AudioBuffer<float>& input,
                                                   double sampleRate,
                                                   const SampleAnalysisRequest& request,
                                                   double hostBpm) const
{
    return analyzeBufferExtended(input, sampleRate, request, hostBpm).summary;
}

SampleAnalysisResult SampleAnalyzer::analyzeAudioFile(const juce::File& file,
                                                      const SampleAnalysisRequest& request,
                                                      double hostBpm,
                                                      juce::String* errorMessage) const
{
    return analyzeAudioFileExtended(file, request, hostBpm, errorMessage).summary;
}

SampleAnalysisBundle SampleAnalyzer::analyzeBufferExtended(const juce::AudioBuffer<float>& input,
                                                           double sampleRate,
                                                           const SampleAnalysisRequest& request,
                                                           double hostBpm) const
{
    SampleAnalysisBundle bundle;

    std::vector<float> mono;
    featureExtractor.downmixToMono(input, mono, request.downmixToMono);

    // Tempo (and, for drum breaks, the K/S/H events) come from the onset-level transcriber,
    // which works in seconds first, so the step grid below is built on a measured tempo.
    DrumBreakOptions breakOptions;
    breakOptions.hostBpm = hostBpm;
    breakOptions.quantizeAmount = request.breakQuantizeAmount;
    bundle.breakAnalysis = breakTranscriber.analyze(mono, sampleRate, breakOptions);

    // Key + bass line, on the original timeline. Segments are one beat of the tempo the
    // generator will use to place them: the sample's own when measured confidently, otherwise
    // the session tempo with beat 1 at the start of the file.
    // Onset-based tempo: trusted on drum loops at moderate confidence, on any other sample only
    // when very confident (a tonal sample's chord changes / swells can mislead a weak estimate).
    // A trusted tempo also becomes the generation tempo (unless DAW sync or BPM lock win).
    const auto& breakAnalysis = bundle.breakAnalysis;
    const bool sampleTempoTrusted = breakAnalysis.valid
        && breakAnalysis.bpm > 20.0
        && ((breakAnalysis.drumLoopConfidence >= 0.75f && breakAnalysis.tempoConfidence >= 0.5f)
            || breakAnalysis.tempoConfidence >= 0.8f);
    bundle.sampleBpm = sampleTempoTrusted ? breakAnalysis.bpm : 0.0;
    bundle.harmonyBpm = sampleTempoTrusted ? bundle.breakAnalysis.bpm : (hostBpm > 20.0 ? hostBpm : 90.0);
    bundle.harmonyOriginSeconds = sampleTempoTrusted ? bundle.breakAnalysis.originSeconds : 0.0;
    bundle.harmonyTempoFromSample = sampleTempoTrusted;
    bundle.harmony = harmonyAnalyzer.analyze(mono, sampleRate, 60.0 / bundle.harmonyBpm, bundle.harmonyOriginSeconds);

    // Every step-grid stage below counts sixteenths from sample 0, so start the signal on the
    // detected beat 1 (drop a lead-in, or pad a pickup) to keep those steps on the real grid.
    if (bundle.breakAnalysis.valid && bundle.breakAnalysis.tempoConfidence >= 0.35f)
    {
        const auto offset = static_cast<long long>(std::llround(bundle.breakAnalysis.originSeconds * sampleRate));
        if (offset > 0 && offset < static_cast<long long>(mono.size()) / 2)
            mono.erase(mono.begin(), mono.begin() + static_cast<std::ptrdiff_t>(offset));
        else if (offset < 0)
            mono.insert(mono.begin(), static_cast<size_t>(-offset), 0.0f);
    }

    featureExtractor.normalizeWorkingLevel(mono);

    bundle.summary = analyzePreparedMono(mono, sampleRate, request, hostBpm, bundle.breakAnalysis);
    if (!bundle.summary.valid)
        return bundle;

    if (bundle.summary.analyzedBars < kMinBarsForHinting)
        bundle.summary.phraseBoundaryBars.clear();

    bundle.featureMap = buildFeatureMap(bundle.summary);

    const bool needsLaneEvidence = request.buildLaneEvidence
        || request.buildTranscription
        || request.buildGenerationHints
        || request.detectBassline
        || request.detectDrumEvents;

    if (!needsLaneEvidence)
        return bundle;

    const auto spectral = stftAnalyzer.analyze(mono, sampleRate);
    const auto onsets = onsetDetector.detect(spectral);
    const auto separation = request.usePercussiveHarmonicSeparation
        ? percussiveHarmonicSeparator.separate(spectral, onsets)
        : PercussiveHarmonicSeparation { std::vector<SeparationFrame>(spectral.frames.size()) };

    bundle.laneEvidence = laneEventInferer.infer(bundle.featureMap,
                                                 bundle.summary,
                                                 spectral,
                                                 onsets,
                                                 separation);

    BasslineInferenceResult bassline;
    if (request.detectBassline)
    {
        bassline = basslineInferer.infer(mono,
                                         sampleRate,
                                         bundle.summary,
                                         bundle.laneEvidence);
    }

    if (request.buildTranscription || request.detectBassline || request.detectDrumEvents)
        bundle.transcription = sampleTranscriber.transcribe(bundle.laneEvidence, bassline, request);

    // Copy mode always uses the tick-accurate K/S/H transcription; guide mode does too when the
    // sample is clearly a drum loop (the step transcriber would invent ghost kicks and an 808
    // line out of the kicks).
    const bool clearlyDrumLoop = bundle.breakAnalysis.valid && bundle.breakAnalysis.drumLoopConfidence >= 0.75f;
    if (request.transcribeDrumBreak || (request.detectDrumEvents && clearlyDrumLoop))
        applyBreakTranscription(bundle);

    if (request.buildGenerationHints)
        bundle.hints = hintsBuilder.build(bundle.laneEvidence, bundle.transcription, bundle.summary);

    return bundle;
}

void SampleAnalyzer::applyBreakTranscription(SampleAnalysisBundle& bundle)
{
    const auto& analysis = bundle.breakAnalysis;
    if (!analysis.valid)
        return;

    // Drum copy owns only the three core lanes; ghosts, open hats, percs and 808 stay with the
    // genre engines, and a drum loop has no bassline to extract.
    bundle.transcription.drumEvents.clear();
    bundle.transcription.bassEvents.clear();
    bundle.transcription.hasDetectedBass = false;

    for (const auto& hit : analysis.hits)
    {
        TranscribedEvent event;
        event.lane = hit.lane;
        event.tick = hit.gridTick;
        event.timingOffsetTicks = hit.timingOffsetTicks;
        event.step = hit.gridTick / TimingGrid::Sixteenth;
        event.lengthSteps = 1;
        event.velocity = hit.velocity;
        event.pitch = hit.lane == TrackType::Kick ? 36 : hit.lane == TrackType::Snare ? 38 : 42;
        event.confidence = hit.confidence;
        bundle.transcription.drumEvents.push_back(event);
    }

    bundle.transcription.hasDetectedDrums = !bundle.transcription.drumEvents.empty();
    bundle.summary.analyzedBars = analysis.bars;
    bundle.summary.detectedBpm = analysis.bpm;
    bundle.summary.bpmReliable = analysis.tempoConfidence >= 0.35f;
    bundle.summary.phraseBoundaryBars.clear();
}

SampleAnalysisBundle SampleAnalyzer::analyzeAudioFileExtended(const juce::File& file,
                                                              const SampleAnalysisRequest& request,
                                                              double hostBpm,
                                                              juce::String* errorMessage) const
{
    if (!file.existsAsFile())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Audio file does not exist.";
        return {};
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
    {
        if (errorMessage != nullptr)
            *errorMessage = "Unsupported audio file format.";
        return {};
    }

    const int64_t lengthSamples = static_cast<int64_t>(reader->lengthInSamples);
    if (lengthSamples <= 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = "Audio file is empty.";
        return {};
    }

    const int channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    // Analysis runs on the host's message thread: at most 16 bars are ever used (64 s even at
    // 60 BPM), so longer files are cut there instead of freezing the DAW for many seconds.
    // A trimmed request reads only the selected fragment (the file itself is untouched).
    constexpr double kMaxAnalysisSeconds = 64.0;
    int64_t startSample = 0;
    int64_t endSample = lengthSamples;
    if (request.hasTrim())
    {
        startSample = juce::jlimit<int64_t>(0, lengthSamples - 1, static_cast<int64_t>(std::llround(request.trimStartSeconds * reader->sampleRate)));
        endSample = juce::jlimit<int64_t>(startSample + 1, lengthSamples, static_cast<int64_t>(std::llround(request.trimEndSeconds * reader->sampleRate)));
    }
    const int samplesToRead = static_cast<int>(juce::jmin<int64_t>(endSample - startSample,
                                                                   static_cast<int64_t>(reader->sampleRate * kMaxAnalysisSeconds)));
    juce::AudioBuffer<float> buffer(channels, samplesToRead);

    if (!reader->read(&buffer, 0, samplesToRead, startSample, true, channels > 1))
    {
        if (errorMessage != nullptr)
            *errorMessage = "Failed to read audio file samples.";
        return {};
    }

    auto bundle = analyzeBufferExtended(buffer, reader->sampleRate, request, hostBpm);

    if (errorMessage != nullptr && !bundle.summary.valid)
        *errorMessage = "Analysis completed with invalid result.";

    return bundle;
}

AudioFeatureMap SampleAnalyzer::buildFeatureMap(const SampleAnalysisResult& result) const
{
    AudioFeatureMap map;
    if (!result.valid)
        return map;

    map.bars = result.analyzedBars;
    map.stepsPerBar = result.stepsPerBar;
    map.barDensity = result.densityPerBar;
    map.steps.resize(static_cast<size_t>(result.totalSteps));

    for (int i = 0; i < result.totalSteps; ++i)
    {
        auto& step = map.steps[static_cast<size_t>(i)];
        step.energy = result.energyPerStep[static_cast<size_t>(i)];
        step.accent = result.accentPerStep[static_cast<size_t>(i)];
        step.onset = result.onsetStrengthPerStep[static_cast<size_t>(i)];
        step.low = result.lowBandPerStep[static_cast<size_t>(i)];
        step.mid = result.midBandPerStep[static_cast<size_t>(i)];
        step.high = result.highBandPerStep[static_cast<size_t>(i)];

        const int stepInBar = i % juce::jmax(1, result.stepsPerBar);
        step.isStrongBeat = (stepInBar % 4 == 0);
        step.isWeakBeat = (stepInBar % 2 == 0) && !step.isStrongBeat;
    }

    for (const int boundaryBar : result.phraseBoundaryBars)
    {
        const int start = juce::jmax(0, boundaryBar * result.stepsPerBar - 2);
        const int end = juce::jmin(result.totalSteps, boundaryBar * result.stepsPerBar + 3);
        for (int i = start; i < end; ++i)
            map.steps[static_cast<size_t>(i)].nearPhraseBoundary = true;
    }

    return map;
}

SampleAnalysisResult SampleAnalyzer::analyzePreparedMono(const std::vector<float>& mono,
                                                         double sampleRate,
                                                         const SampleAnalysisRequest& request,
                                                         double hostBpm,
                                                         const DrumBreakAnalysis& breakAnalysis) const
{
    SampleAnalysisResult result;

    // The sample's own tempo comes first: a sample at 88 BPM in a 92 BPM session is still at
    // 88. The host tempo is only a hint (already used by the transcriber) or a fallback.
    const bool fileTempoUsable = request.detectTempoFromFile
        && breakAnalysis.valid
        && breakAnalysis.tempoConfidence >= 0.35f;
    if (fileTempoUsable)
    {
        result.bpmFromHost = breakAnalysis.bpmMatchesHost;
        result.bpmReliable = true;
        featureExtractor.computeStepFeatures(mono, sampleRate, breakAnalysis.bpm, request, result);
        result.detectedBpm = breakAnalysis.bpm;
        return result;
    }

    const bool hostTempoUsable = request.useHostTempoIfAvailable && hostBpm > 20.0;
    const double initialBpm = hostTempoUsable ? hostBpm : 120.0;

    result.bpmFromHost = hostTempoUsable;
    result.bpmReliable = hostTempoUsable;

    featureExtractor.computeStepFeatures(mono, sampleRate, initialBpm, request, result);
    if (!result.valid)
        return result;

    if (!hostTempoUsable && request.detectTempoFromFile)
    {
        const double estimated = estimateBpmFromEnergy(result.energyPerStep, result.stepsPerBar, initialBpm);
        const double adjustedBpm = applyTempoHandling(estimated, request.tempoHandling);
        SampleAnalysisResult refined;
        refined.bpmFromHost = false;
        refined.bpmReliable = estimated > 0.0;

        featureExtractor.computeStepFeatures(mono, sampleRate, adjustedBpm, request, refined);
        if (refined.valid)
            result = refined;

        result.detectedBpm = adjustedBpm;
        result.bpmReliable = estimated > 0.0;
    }

    return result;
}

double SampleAnalyzer::estimateBpmFromEnergy(const std::vector<float>& energyPerStep,
                                             int stepsPerBar,
                                             double fallbackBpm) const
{
    if (energyPerStep.size() < static_cast<size_t>(stepsPerBar * 2))
        return fallbackBpm;

    // A simple periodicity probe over bar-sized lags keeps MVP deterministic.
    const int maxLag = juce::jlimit(stepsPerBar, stepsPerBar * 4, static_cast<int>(energyPerStep.size() / 2));
    float bestScore = -1.0f;
    int bestLag = stepsPerBar;

    for (int lag = juce::jmax(2, stepsPerBar / 2); lag <= maxLag; ++lag)
    {
        float score = 0.0f;
        for (size_t i = static_cast<size_t>(lag); i < energyPerStep.size(); ++i)
            score += energyPerStep[i] * energyPerStep[i - static_cast<size_t>(lag)];

        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }

    if (bestLag <= 0)
        return fallbackBpm;

    const double barsPerMinute = fallbackBpm / 4.0;
    const double lagRatio = static_cast<double>(stepsPerBar) / static_cast<double>(bestLag);
    const double estimated = juce::jlimit(60.0, 180.0, barsPerMinute * 4.0 * lagRatio);
    return estimated;
}
} // namespace bbg
