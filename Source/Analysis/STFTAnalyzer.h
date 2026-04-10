#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace bbg
{
struct SpectralFrame
{
    float rms = 0.0f;
    float lowEnergy = 0.0f;
    float midEnergy = 0.0f;
    float highEnergy = 0.0f;
    float spectralFlux = 0.0f;
    float centroid = 0.0f;
    float flatness = 0.0f;
};

struct SpectralAnalysis
{
    double sampleRate = 44100.0;
    int frameSize = 1024;
    int hopSize = 256;
    std::vector<SpectralFrame> frames;
};

class STFTAnalyzer
{
public:
    SpectralAnalysis analyze(const std::vector<float>& mono, double sampleRate) const
    {
        SpectralAnalysis analysis;
        analysis.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

        if (mono.empty())
            return analysis;

        juce::dsp::FFT fft(10);
        juce::dsp::WindowingFunction<float> window(static_cast<size_t>(analysis.frameSize),
                                                   juce::dsp::WindowingFunction<float>::hann,
                                                   true);

        const int frameSize = analysis.frameSize;
        const int hopSize = analysis.hopSize;
        const int halfSize = frameSize / 2;
        const double binHz = analysis.sampleRate / static_cast<double>(frameSize);
        const int frameCount = std::max(1, static_cast<int>((mono.size() + static_cast<size_t>(hopSize) - 1) / static_cast<size_t>(hopSize)));

        analysis.frames.reserve(static_cast<size_t>(frameCount));

        std::vector<float> previousMagnitudes(static_cast<size_t>(halfSize), 0.0f);
        std::vector<float> fftData(static_cast<size_t>(frameSize * 2), 0.0f);

        float maxRms = 0.0f;
        float maxLow = 0.0f;
        float maxMid = 0.0f;
        float maxHigh = 0.0f;
        float maxFlux = 0.0f;

        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            std::fill(fftData.begin(), fftData.end(), 0.0f);

            const size_t start = static_cast<size_t>(frameIndex * hopSize);
            const size_t available = start < mono.size() ? mono.size() - start : 0;
            const int copyCount = std::min(frameSize, static_cast<int>(available));

            float rmsSum = 0.0f;
            for (int sampleIndex = 0; sampleIndex < copyCount; ++sampleIndex)
            {
                const float sample = mono[start + static_cast<size_t>(sampleIndex)];
                fftData[static_cast<size_t>(sampleIndex)] = sample;
                rmsSum += sample * sample;
            }

            window.multiplyWithWindowingTable(fftData.data(), static_cast<size_t>(frameSize));
            fft.performFrequencyOnlyForwardTransform(fftData.data());

            SpectralFrame frame;
            frame.rms = std::sqrt(rmsSum / static_cast<float>(std::max(1, copyCount)));

            double centroidWeighted = 0.0;
            double magnitudeSum = 0.0;
            double logMagnitudeSum = 0.0;
            int magnitudeCount = 0;
            float flux = 0.0f;

            for (int bin = 1; bin < halfSize; ++bin)
            {
                const float magnitude = std::max(1.0e-6f, fftData[static_cast<size_t>(bin)]);
                const double frequencyHz = static_cast<double>(bin) * binHz;

                if (frequencyHz < 180.0)
                    frame.lowEnergy += magnitude;
                else if (frequencyHz < 2200.0)
                    frame.midEnergy += magnitude;
                else
                    frame.highEnergy += magnitude;

                const float positiveDiff = std::max(0.0f, magnitude - previousMagnitudes[static_cast<size_t>(bin)]);
                flux += positiveDiff;
                previousMagnitudes[static_cast<size_t>(bin)] = magnitude;

                centroidWeighted += frequencyHz * static_cast<double>(magnitude);
                magnitudeSum += static_cast<double>(magnitude);
                logMagnitudeSum += std::log(static_cast<double>(magnitude));
                ++magnitudeCount;
            }

            frame.spectralFlux = flux / static_cast<float>(std::max(1, halfSize - 1));
            if (magnitudeSum > 0.0)
                frame.centroid = static_cast<float>(centroidWeighted / magnitudeSum / (analysis.sampleRate * 0.5));

            if (magnitudeCount > 0)
            {
                const double geometricMean = std::exp(logMagnitudeSum / static_cast<double>(magnitudeCount));
                const double arithmeticMean = magnitudeSum / static_cast<double>(magnitudeCount);
                frame.flatness = arithmeticMean > 0.0 ? static_cast<float>(geometricMean / arithmeticMean) : 0.0f;
            }

            maxRms = std::max(maxRms, frame.rms);
            maxLow = std::max(maxLow, frame.lowEnergy);
            maxMid = std::max(maxMid, frame.midEnergy);
            maxHigh = std::max(maxHigh, frame.highEnergy);
            maxFlux = std::max(maxFlux, frame.spectralFlux);

            analysis.frames.push_back(frame);
        }

        const float invRms = maxRms > 1.0e-6f ? 1.0f / maxRms : 1.0f;
        const float invLow = maxLow > 1.0e-6f ? 1.0f / maxLow : 1.0f;
        const float invMid = maxMid > 1.0e-6f ? 1.0f / maxMid : 1.0f;
        const float invHigh = maxHigh > 1.0e-6f ? 1.0f / maxHigh : 1.0f;
        const float invFlux = maxFlux > 1.0e-6f ? 1.0f / maxFlux : 1.0f;

        for (auto& frame : analysis.frames)
        {
            frame.rms *= invRms;
            frame.lowEnergy *= invLow;
            frame.midEnergy *= invMid;
            frame.highEnergy *= invHigh;
            frame.spectralFlux *= invFlux;
            frame.centroid = std::clamp(frame.centroid, 0.0f, 1.0f);
            frame.flatness = std::clamp(frame.flatness, 0.0f, 1.0f);
        }

        return analysis;
    }
};
} // namespace bbg