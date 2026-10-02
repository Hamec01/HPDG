#pragma once

#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace bbg
{
// The dropped sample, decoded once and kept in memory for everything that is not the analysis
// itself: the waveform behind the K/S/H dots, the trim editor (peaks, beat grid, transients,
// suggested fragment) and sample playback (trim audition, dot audition, play with HPDG).
// The analyzer still reads only the selected fragment from the file.
class SampleSourceAudio
{
public:
    static constexpr int kPeakBlock = 256;       // samples per cached min/max pair
    static constexpr int kOnsetHop = 512;        // samples per onset-envelope frame
    static constexpr double kMaxLoadSeconds = 600.0;

    // "Play with HPDG": where in the file the sample is at a given pattern position.
    // loopStartSeconds is the sample's beat 1 - it can lie before the file start when the loop
    // begins with a pickup (or a typed tempo puts beat 1 there). The analysed bars form a loop,
    // so a position before the file start wraps to the loop's end instead of playing silence.
    // Returns < 0 when nothing should sound.
    static double loopPlaybackSeconds(double loopStartSeconds, double barPosition, double barSeconds,
                                      double loopBars, double fileDurationSeconds)
    {
        const double loopLength = barSeconds * loopBars;
        double seconds = loopStartSeconds + barPosition * barSeconds;
        if (loopLength > 0.0)
        {
            if (seconds < 0.0)
                seconds += loopLength;
            else if (seconds >= fileDurationSeconds && seconds - loopLength >= 0.0)
                seconds -= loopLength;
        }
        return seconds >= 0.0 && seconds < fileDurationSeconds ? seconds : -1.0;
    }

    bool load(const juce::File& file, juce::String* errorMessage = nullptr);
    // Builds everything from a buffer already in memory (tests, generated audio).
    void loadFromBuffer(const juce::AudioBuffer<float>& source, double sampleRate, const juce::File& file = {});

    bool isLoaded() const { return audio != nullptr && audio->getNumSamples() > 0; }
    const juce::File& getFile() const { return file; }
    double getSampleRate() const { return sampleRate; }
    double getDurationSeconds() const { return durationSeconds; }
    std::shared_ptr<const juce::AudioBuffer<float>> getAudio() const { return audio; }

    // Min/max of the mono signal for `bins` equal slices of [t0, t1] (seconds).
    void getPeaks(double t0, double t1, int bins, std::vector<std::pair<float, float>>& out) const;

    // Detected transients (seconds) and the global tempo estimate for the beat grid.
    const std::vector<double>& getTransients() const { return transients; }
    double getEstimatedBpm() const { return estimatedBpm; }
    double getBeatPhaseSeconds() const { return beatPhaseSeconds; }  // time of a beat
    double getDownbeatSeconds() const { return downbeatSeconds; }    // time of a likely bar start
    float getTempoConfidence() const { return tempoConfidence; }

    // Nearest transient to t within maxDistance, or t itself.
    double snapToTransient(double t, double maxDistanceSeconds) const;
    // Nearest beat / bar line of a grid (bpm, anchor = a bar start) to t.
    static double snapToGrid(double t, double bpm, double anchorSeconds, int beatsPerStep);

    // The most drum-like fragment of `bars` bars at the estimated tempo: a steady, dense pulse of
    // transients. Starts on a likely bar line. Whole file when it is short.
    juce::Range<double> suggestRegion(int bars) const;

private:
    void buildPeaks(const std::vector<float>& mono);
    void buildOnsets(const std::vector<float>& mono);
    void estimateTempo();

    juce::File file;
    double sampleRate = 44100.0;
    double durationSeconds = 0.0;
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    std::vector<float> peakMin;
    std::vector<float> peakMax;
    std::vector<float> onsetEnv;
    std::vector<double> transients;
    double estimatedBpm = 0.0;
    double beatPhaseSeconds = 0.0;
    double downbeatSeconds = 0.0;
    float tempoConfidence = 0.0f;
};
} // namespace bbg
