#pragma once

#include "Common.h"

namespace dawalka {

class AudioExporter
{
public:
    // Write a buffer to disk as 44.1 kHz / stereo / 32-bit float WAV.
    // If the buffer has a different sample rate or channel count it will be
    // resampled / upmixed automatically.
    static bool writeWav (const juce::File& outFile,
                          const juce::AudioBuffer<float>& buffer,
                          double sourceSampleRate);

    // Read a WAV into an AudioBuffer
    static std::unique_ptr<juce::AudioBuffer<float>>
        readWav (const juce::File& inFile, double* sampleRateOut = nullptr);

    // Probe a file for its duration in seconds WITHOUT reading any sample
    // data.  Returns 0.0 on error (missing file, unsupported format,
    // malformed header, etc.) — callers can safely use the return value
    // for display.  Multi-format (WAV / AIFF / FLAC / OGG) via
    // AudioFormatManager::registerBasicFormats().
    //
    // THIS IS THE SAFE WAY TO MEASURE DURATION.  Do not call readWav()
    // just to count samples — it allocates (length × channels × 4) bytes
    // and reads the entire file from disk, which will crash on large or
    // malformed AIFF files.
    static double getAudioDuration (const juce::File& inFile);

    // Re-encode any audio file to the canonical 44.1 kHz / stereo / float WAV
    static bool reencode (const juce::File& in, const juce::File& out);
};

} // namespace dawalka
