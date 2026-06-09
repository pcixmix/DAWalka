#include "AudioExporter.h"

namespace dawalka {

bool AudioExporter::writeWav (const juce::File& outFile,
                              const juce::AudioBuffer<float>& sourceBuffer,
                              double sourceSampleRate)
{
    if (outFile.existsAsFile()) outFile.deleteFile();
    outFile.getParentDirectory().createDirectory();

    auto destSampleRate = 44100.0;
    auto destChannels   = 2;

    // Resample
    auto workingBuffer = sourceBuffer;
    if (std::abs (sourceSampleRate - destSampleRate) > 0.5)
    {
        const double ratio = destSampleRate / sourceSampleRate;
        const int newLength = static_cast<int> (workingBuffer.getNumSamples() * ratio);

        juce::AudioBuffer<float> resampled (workingBuffer.getNumChannels(), newLength);
        for (int ch = 0; ch < workingBuffer.getNumChannels(); ++ch)
        {
            const float* src = workingBuffer.getReadPointer (ch);
            float* dst = resampled.getWritePointer (ch);
            for (int i = 0; i < newLength; ++i)
            {
                const double pos = i / ratio;
                const int    p0  = static_cast<int> (pos);
                const float  a   = static_cast<float> (pos - p0);
                const float  s0  = src[juce::jlimit (0, workingBuffer.getNumSamples() - 1, p0)];
                const float  s1  = src[juce::jlimit (0, workingBuffer.getNumSamples() - 1, p0 + 1)];
                dst[i] = s0 + a * (s1 - s0);
            }
        }
        workingBuffer = std::move (resampled);
    }

    // Channel conversion: mono→stereo, multichannel→stereo
    juce::AudioBuffer<float> stereo (destChannels,
        workingBuffer.getNumSamples());
    if (workingBuffer.getNumChannels() == 1)
    {
        stereo.copyFrom (0, 0, workingBuffer, 0, 0, workingBuffer.getNumSamples());
        stereo.copyFrom (1, 0, workingBuffer, 0, 0, workingBuffer.getNumSamples());
    }
    else if (workingBuffer.getNumChannels() >= 2)
    {
        stereo.copyFrom (0, 0, workingBuffer, 0, 0, workingBuffer.getNumSamples());
        stereo.copyFrom (1, 0, workingBuffer, 1, 0, workingBuffer.getNumSamples());
    }

    // ── Write 32-bit float WAV ────────────────────────────────────────────
    std::unique_ptr<juce::FileOutputStream> out (outFile.createOutputStream());
    if (!out) return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (out.release(),
                             destSampleRate,
                             destChannels,
                             32,               // bits per sample
                             {},               // metadata
                             0));              // quality option
    if (!writer) return false;
    return writer->writeFromAudioSampleBuffer (stereo, 0, stereo.getNumSamples());
}

std::unique_ptr<juce::AudioBuffer<float>>
    AudioExporter::readWav (const juce::File& inFile, double* sampleRateOut)
{
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (mgr.createReaderFor (inFile));
    if (!reader) return nullptr;
    if (sampleRateOut) *sampleRateOut = reader->sampleRate;

    auto buffer = std::make_unique<juce::AudioBuffer<float>> (reader->numChannels,
                                                              static_cast<int> (reader->lengthInSamples));
    if (!reader->read (buffer.get(), 0, buffer->getNumSamples(), 0, true, true))
        return nullptr;
    return buffer;
}

double AudioExporter::getAudioDuration (const juce::File& inFile)
{
    if (! inFile.existsAsFile()) return 0.0;

    // Probe the file with a fresh AudioFormatManager.  The reader is
    // destroyed before we return, so we never hold any audio data in
    // memory.  We deliberately do NOT call readWav() here — that would
    // allocate (lengthInSamples × numChannels × 4) bytes and read the
    // entire file just to compute seconds.
    //
    // Wrap in try/catch because some AIFF/FLAC variants have been
    // known to throw from inside createReaderFor() on macOS — better
    // to display "0.00 s" than crash the host.
    try
    {
        juce::AudioFormatManager mgr;
        mgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (mgr.createReaderFor (inFile));
        if (! reader) return 0.0;
        if (reader->sampleRate <= 0.0) return 0.0;
        if (reader->lengthInSamples <= 0) return 0.0;

        double secs = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
        if (! std::isfinite (secs) || secs <= 0.0) return 0.0;
        return secs;
    }
    catch (...)
    {
        return 0.0;
    }
}

bool AudioExporter::reencode (const juce::File& in, const juce::File& out)
{
    double sr = 0.0;
    auto buf = readWav (in, &sr);
    if (!buf) return false;
    return writeWav (out, *buf, sr);
}

} // namespace dawalka
